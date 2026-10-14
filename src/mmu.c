#include <limits.h>
#include <stdlib.h>
#include <string.h>

#include "mmu.h"

const char *algoritmoNombre(Algoritmo alg)
{
    switch (alg) {
    case ALG_OPT:  return "OPT";
    case ALG_FIFO: return "FIFO";
    case ALG_SC:   return "SC";
    case ALG_LRU:  return "LRU";
    default:       return "LFU";
    }
}

/* ---------------------------------------------------------------
   Inicio y liberacion
   --------------------------------------------------------------- */

/* OPT necesita conocer el futuro: para cada puntero se guardan, en
   orden, los indices de las operaciones use que lo referencian. */
static int prepararOpt(Mmu *m)
{
    const Secuencia *s = m->sec;
    int n = s->cantidadPtrs;

    m->numUsos = calloc((size_t)n + 1, sizeof(int));
    m->usos = calloc((size_t)n + 1, sizeof(int *));

    if (m->numUsos == NULL || m->usos == NULL) {
        return 0;
    }

    for (int i = 0; i < s->cantidad; i++) {
        int ptr = s->ops[i].ptr;

        if (s->ops[i].tipo == OP_USE && 1 <= ptr && ptr <= n) {
            m->numUsos[ptr]++;
        }
    }

    for (int ptr = 1; ptr <= n; ptr++) {
        if (0 < m->numUsos[ptr]) {
            m->usos[ptr] = malloc((size_t)m->numUsos[ptr] * sizeof(int));

            if (m->usos[ptr] == NULL) {
                return 0;
            }

            m->numUsos[ptr] = 0;
        }
    }

    for (int i = 0; i < s->cantidad; i++) {
        int ptr = s->ops[i].ptr;

        if (s->ops[i].tipo == OP_USE && 1 <= ptr && ptr <= n) {
            m->usos[ptr][m->numUsos[ptr]] = i;
            m->numUsos[ptr]++;
        }
    }

    return 1;
}

int mmuIniciar(Mmu *m, const Secuencia *s, Algoritmo alg)
{
    memset(m, 0, sizeof(Mmu));

    m->alg = alg;
    m->sec = s;

    for (int f = 0; f < MARCOS; f++) {
        m->marcos[f] = -1;
    }

    m->ptrs = calloc((size_t)s->cantidadPtrs + 1, sizeof(Puntero));
    m->estadoPid = calloc((size_t)s->maxPid + 1, 1);

    if (m->ptrs == NULL || m->estadoPid == NULL) {
        mmuLiberar(m);

        return 0;
    }

    if (alg == ALG_OPT && !prepararOpt(m)) {
        mmuLiberar(m);

        return 0;
    }

    return 1;
}

void mmuLiberar(Mmu *m)
{
    if (m->ptrs != NULL) {
        for (int p = 0; p <= m->sec->cantidadPtrs; p++) {
            free(m->ptrs[p].paginas);
        }
    }

    if (m->usos != NULL) {
        for (int p = 0; p <= m->sec->cantidadPtrs; p++) {
            free(m->usos[p]);
        }
    }

    free(m->ptrs);
    free(m->estadoPid);
    free(m->usos);
    free(m->numUsos);
    free(m->paginas);
    free(m->vista);

    memset(m, 0, sizeof(Mmu));
}

/* ---------------------------------------------------------------
   OPT
   --------------------------------------------------------------- */

/* Primera operacion use del ptr con indice >= desde, o -1 si no hay. */
static int proximoUsoDesde(const Mmu *m, int ptr, int desde)
{
    if (m->usos == NULL || ptr < 1 || m->sec->cantidadPtrs < ptr) {
        return -1;
    }

    const int *u = m->usos[ptr];
    int bajo = 0;
    int alto = m->numUsos[ptr];

    while (bajo < alto) {
        int medio = (bajo + alto) / 2;

        if (u[medio] < desde) {
            bajo = medio + 1;
        }
        else {
            alto = medio;
        }
    }

    return bajo < m->numUsos[ptr] ? u[bajo] : -1;
}

int mmuProximoUso(const Mmu *m, int ptr)
{
    return proximoUsoDesde(m, ptr, m->opActual);
}

/* ---------------------------------------------------------------
   Seleccion de la pagina victima
   --------------------------------------------------------------- */

/* Devuelve 1 si la pagina a debe salir antes que la b. */
static int saleAntes(const Mmu *m, const Pagina *a, const Pagina *b, int opIndice)
{
    switch (m->alg) {
    case ALG_LRU:
        return a->ultimoUso < b->ultimoUso;

    case ALG_LFU:
        if (a->contador != b->contador) {
            return a->contador < b->contador;
        }

        return a->ordenCarga < b->ordenCarga;

    case ALG_OPT: {
        /* Sale la que se va a usar mas tarde; nunca = infinito */
        int pa = proximoUsoDesde(m, a->ptr, opIndice + 1);
        int pb = proximoUsoDesde(m, b->ptr, opIndice + 1);
        long ka = pa < 0 ? LONG_MAX : pa;
        long kb = pb < 0 ? LONG_MAX : pb;

        if (ka != kb) {
            return ka > kb;
        }

        return a->ordenCarga < b->ordenCarga;
    }

    default:
        /* FIFO y SC: la que llego primero */
        return a->ordenCarga < b->ordenCarga;
    }
}

/* Mejor candidato entre los marcos ocupados. Las paginas fijadas (las de
   la operacion en curso) solo se consideran si no hay ninguna otra. */
static int mejorCandidato(const Mmu *m, int opIndice)
{
    for (int pasada = 0; pasada < 2; pasada++) {
        int mejor = -1;

        for (int f = 0; f < MARCOS; f++) {
            int pi = m->marcos[f];

            if (pi < 0 || (pasada == 0 && m->paginas[pi].fijada)) {
                continue;
            }

            if (mejor < 0 ||
                saleAntes(m, &m->paginas[pi], &m->paginas[m->marcos[mejor]], opIndice)) {
                mejor = f;
            }
        }

        if (0 <= mejor) {
            return mejor;
        }
    }

    return -1;
}

static int elegirVictima(Mmu *m, int opIndice)
{
    if (m->alg != ALG_SC) {
        return mejorCandidato(m, opIndice);
    }

    /* Second Chance: se recorre en orden FIFO; si la pagina tiene el
       bit R en 1 se le borra y pasa al final de la cola. Tras una
       vuelta completa todas tienen R en 0, asi que el ciclo termina. */
    for (int vuelta = 0; vuelta <= 2 * MARCOS; vuelta++) {
        int f = mejorCandidato(m, opIndice);

        if (f < 0) {
            return -1;
        }

        Pagina *p = &m->paginas[m->marcos[f]];

        if (p->bitR == 0) {
            return f;
        }

        p->bitR = 0;
        m->reloj++;
        p->ordenCarga = m->reloj;
    }

    return mejorCandidato(m, opIndice);
}

/* ---------------------------------------------------------------
   Movimiento de paginas
   --------------------------------------------------------------- */

/* Lleva una pagina a memoria real. Si no hay marco libre, saca una
   victima a memoria virtual segun el algoritmo. */
static void cargarEnRam(Mmu *m, int pi, int opIndice, int esNueva)
{
    int f = -1;

    for (int k = 0; k < MARCOS; k++) {
        if (m->marcos[k] < 0) {
            f = k;
            break;
        }
    }

    if (f < 0) {
        f = elegirVictima(m, opIndice);

        Pagina *victima = &m->paginas[m->marcos[f]];

        victima->enRam = 0;
        victima->marco = -1;
        m->siguienteDisco++;
        victima->dirDisco = m->siguienteDisco;
        m->marcos[f] = -1;
        m->paginasRam--;
        m->paginasVirtual++;
    }

    Pagina *p = &m->paginas[pi];

    if (!esNueva) {
        m->paginasVirtual--;
    }

    p->enRam = 1;
    p->marco = f;
    p->dirDisco = -1;
    p->tiempoCarga = m->tiempo;

    m->reloj++;
    p->ordenCarga = m->reloj;
    p->ultimoUso = m->reloj;
    p->tiempoUltimoUso = m->tiempo;
    p->bitR = 1;
    p->contador = 1;

    m->marcos[f] = pi;
    m->paginasRam++;
}

static void registrarFallo(Mmu *m)
{
    m->tiempo += COSTO_FALLO;
    m->thrashing += COSTO_FALLO;
    m->fallos++;
}

static void liberarPuntero(Mmu *m, int ptr)
{
    Puntero *q = &m->ptrs[ptr];

    for (int k = 0; k < q->numPaginas; k++) {
        Pagina *p = &m->paginas[q->paginas[k]];

        if (p->enRam) {
            m->marcos[p->marco] = -1;
            m->paginasRam--;
        }
        else {
            m->paginasVirtual--;
        }

        p->viva = 0;
        p->enRam = 0;
        p->marco = -1;
        p->dirDisco = -1;
    }

    m->fragmentacion -= (long)q->numPaginas * TAM_PAGINA - q->tam;
    q->vivo = 0;
}

/* ---------------------------------------------------------------
   Instrucciones
   --------------------------------------------------------------- */

static int punteroValido(const Mmu *m, int ptr)
{
    return 1 <= ptr && ptr <= m->sec->cantidadPtrs && m->ptrs[ptr].vivo;
}

static int nuevaPagina(Mmu *m, int pid, int ptr)
{
    if (m->numPaginas == m->capPaginas) {
        int nueva = m->capPaginas == 0 ? 512 : m->capPaginas * 2;
        Pagina *mayor = realloc(m->paginas, (size_t)nueva * sizeof(Pagina));

        if (mayor == NULL) {
            return -1;
        }

        m->paginas = mayor;
        m->capPaginas = nueva;
    }

    Pagina *p = &m->paginas[m->numPaginas];

    memset(p, 0, sizeof(Pagina));
    p->id = m->numPaginas + 1;
    p->pid = pid;
    p->ptr = ptr;
    p->viva = 1;
    p->marco = -1;
    p->dirDisco = -1;

    m->numPaginas++;

    return m->numPaginas - 1;
}

static void ejecutarNew(Mmu *m, const Operacion *op, int opIndice)
{
    Puntero *q = &m->ptrs[op->ptr];
    int paginas = (op->tam + TAM_PAGINA - 1) / TAM_PAGINA;

    q->paginas = malloc((size_t)paginas * sizeof(int));

    if (q->paginas == NULL) {
        return;
    }

    q->pid = op->pid;
    q->tam = op->tam;
    q->numPaginas = 0;
    q->vivo = 1;

    /* Toda pagina nueva cuenta como fallo */
    for (int k = 0; k < paginas; k++) {
        int pi = nuevaPagina(m, op->pid, op->ptr);

        if (pi < 0) {
            break;
        }

        q->paginas[q->numPaginas] = pi;
        q->numPaginas++;
        m->paginas[pi].fijada = 1;

        registrarFallo(m);
        cargarEnRam(m, pi, opIndice, 1);
    }

    for (int k = 0; k < q->numPaginas; k++) {
        m->paginas[q->paginas[k]].fijada = 0;
    }

    m->fragmentacion += (long)q->numPaginas * TAM_PAGINA - q->tam;
}

static void ejecutarUse(Mmu *m, const Operacion *op, int opIndice)
{
    Puntero *q = &m->ptrs[op->ptr];

    for (int k = 0; k < q->numPaginas; k++) {
        m->paginas[q->paginas[k]].fijada = 1;
    }

    for (int k = 0; k < q->numPaginas; k++) {
        int pi = q->paginas[k];
        Pagina *p = &m->paginas[pi];

        if (p->enRam) {
            m->tiempo += COSTO_HIT;
            m->hits++;

            m->reloj++;
            p->ultimoUso = m->reloj;
            p->tiempoUltimoUso = m->tiempo;
            p->bitR = 1;
            p->contador++;
        }
        else {
            registrarFallo(m);
            cargarEnRam(m, pi, opIndice, 0);
        }
    }

    for (int k = 0; k < q->numPaginas; k++) {
        m->paginas[q->paginas[k]].fijada = 0;
    }
}

static void marcarCorriendo(Mmu *m, int pid)
{
    if (0 < pid && pid <= m->sec->maxPid && m->estadoPid[pid] == 0) {
        m->estadoPid[pid] = 1;
        m->procesosCorriendo++;
    }
}

static void reconstruirVista(Mmu *m)
{
    if (m->numPaginas == 0) {
        m->numVista = 0;
        return;
    }

    int *nueva = realloc(m->vista, (size_t)m->numPaginas * sizeof(int));

    if (nueva == NULL) {
        return;
    }

    m->vista = nueva;
    m->numVista = 0;

    for (int i = 0; i < m->numPaginas; i++) {
        if (m->paginas[i].viva) {
            m->vista[m->numVista] = i;
            m->numVista++;
        }
    }
}

int mmuPaso(Mmu *m)
{
    if (m->sec->cantidad <= m->opActual) {
        return 0;
    }

    int i = m->opActual;
    const Operacion *op = &m->sec->ops[i];
    int pid = op->pid;
    int terminado = 0 < pid && pid <= m->sec->maxPid && m->estadoPid[pid] == 2;

    /* Las operaciones que violan las reglas (un puntero inexistente o
       borrado, o un proceso ya terminado) se ignoran */
    switch (op->tipo) {
    case OP_NEW:
        if (!terminado && 0 < pid) {
            marcarCorriendo(m, pid);
            ejecutarNew(m, op, i);
        }
        break;

    case OP_USE:
        if (!terminado && punteroValido(m, op->ptr)) {
            marcarCorriendo(m, pid);
            ejecutarUse(m, op, i);
        }
        break;

    case OP_DELETE:
        if (!terminado && punteroValido(m, op->ptr)) {
            marcarCorriendo(m, pid);
            liberarPuntero(m, op->ptr);
        }
        break;

    case OP_KILL:
        if (!terminado && 0 < pid && pid <= m->sec->maxPid) {
            for (int ptr = 1; ptr <= m->sec->cantidadPtrs; ptr++) {
                if (m->ptrs[ptr].vivo && m->ptrs[ptr].pid == pid) {
                    liberarPuntero(m, ptr);
                }
            }

            if (m->estadoPid[pid] == 1) {
                m->procesosCorriendo--;
            }

            m->estadoPid[pid] = 2;
        }
        break;
    }

    m->opActual++;
    reconstruirVista(m);

    return 1;
}
