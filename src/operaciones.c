#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "operaciones.h"
#include "prng.h"

void secuenciaIniciar(Secuencia *s)
{
    memset(s, 0, sizeof(Secuencia));
}

void secuenciaLiberar(Secuencia *s)
{
    free(s->ops);
    secuenciaIniciar(s);
}

static int agregar(Secuencia *s, Operacion op)
{
    if (s->cantidad == s->capacidad) {
        int nueva = s->capacidad == 0 ? 256 : s->capacidad * 2;
        Operacion *mayor = realloc(s->ops, (size_t)nueva * sizeof(Operacion));

        if (mayor == NULL) {
            return 0;
        }

        s->ops = mayor;
        s->capacidad = nueva;
    }

    s->ops[s->cantidad] = op;
    s->cantidad++;

    return 1;
}

/* Asigna a cada new el numero de puntero segun su orden de aparicion
   (la primera new produce el puntero 1, la segunda el 2, ...) y a cada
   use/delete el PID del dueno de su puntero. */
static void resolverPunteros(Secuencia *s)
{
    int siguientePtr = 0;

    s->maxPid = 0;

    for (int i = 0; i < s->cantidad; i++) {
        if (s->ops[i].tipo == OP_NEW) {
            siguientePtr++;
            s->ops[i].ptr = siguientePtr;
        }
    }

    s->cantidadPtrs = siguientePtr;

    int *dueno = calloc((size_t)siguientePtr + 1, sizeof(int));

    for (int i = 0; i < s->cantidad; i++) {
        Operacion *op = &s->ops[i];

        if (op->tipo == OP_NEW && dueno != NULL) {
            dueno[op->ptr] = op->pid;
        }
        else if ((op->tipo == OP_USE || op->tipo == OP_DELETE) && dueno != NULL) {
            op->pid = (1 <= op->ptr && op->ptr <= siguientePtr) ? dueno[op->ptr] : 0;
        }

        if (s->maxPid < op->pid) {
            s->maxPid = op->pid;
        }
    }

    free(dueno);
}

/* ---------------------------------------------------------------
   Lectura del archivo
   --------------------------------------------------------------- */

static const char *saltarSeparadores(const char *p, int *linea)
{
    while (*p != '\0' && (isspace((unsigned char)*p) || *p == ',' || *p == ';')) {
        if (*p == '\n') {
            (*linea)++;
        }

        p++;
    }

    return p;
}

static const char *leerEntero(const char *p, int *valor)
{
    while (*p == ' ' || *p == '\t') {
        p++;
    }

    if (!isdigit((unsigned char)*p)) {
        return NULL;
    }

    long v = 0;

    while (isdigit((unsigned char)*p)) {
        v = v * 10 + (*p - '0');

        if (2000000000L < v) {
            return NULL;
        }

        p++;
    }

    while (*p == ' ' || *p == '\t') {
        p++;
    }

    *valor = (int)v;

    return p;
}

int secuenciaCargar(const char *ruta, Secuencia *s, char *error, size_t tamError)
{
    secuenciaIniciar(s);

    FILE *archivo = fopen(ruta, "rb");

    if (archivo == NULL) {
        snprintf(error, tamError, "No se pudo abrir el archivo: %s", ruta);

        return 0;
    }

    fseek(archivo, 0, SEEK_END);
    long tam = ftell(archivo);
    rewind(archivo);

    char *texto = tam < 0 ? NULL : malloc((size_t)tam + 1);

    if (texto == NULL || fread(texto, 1, (size_t)tam, archivo) != (size_t)tam) {
        fclose(archivo);
        free(texto);
        snprintf(error, tamError, "No se pudo leer el archivo");

        return 0;
    }

    texto[tam] = '\0';
    fclose(archivo);

    int linea = 1;
    const char *p = saltarSeparadores(texto, &linea);

    while (*p != '\0') {
        char nombre[16];
        int largo = 0;

        while (isalpha((unsigned char)*p) && largo < 15) {
            nombre[largo++] = (char)tolower((unsigned char)*p);
            p++;
        }

        nombre[largo] = '\0';

        while (*p == ' ' || *p == '\t') {
            p++;
        }

        if (largo == 0 || *p != '(') {
            snprintf(error, tamError, "Linea %d: se esperaba una operacion como new(1,500)", linea);
            free(texto);
            secuenciaLiberar(s);

            return 0;
        }

        p++;

        Operacion op;
        memset(&op, 0, sizeof(op));

        int a = 0;
        int b = 0;

        p = leerEntero(p, &a);

        if (p != NULL && strcmp(nombre, "new") == 0) {
            if (*p != ',') {
                p = NULL;
            }
            else {
                p = leerEntero(p + 1, &b);
            }

            op.tipo = OP_NEW;
            op.pid = a;
            op.tam = b;
        }
        else if (p != NULL && strcmp(nombre, "use") == 0) {
            op.tipo = OP_USE;
            op.ptr = a;
        }
        else if (p != NULL && strcmp(nombre, "delete") == 0) {
            op.tipo = OP_DELETE;
            op.ptr = a;
        }
        else if (p != NULL && strcmp(nombre, "kill") == 0) {
            op.tipo = OP_KILL;
            op.pid = a;
        }
        else if (p != NULL) {
            snprintf(error, tamError, "Linea %d: operacion desconocida \"%s\"", linea, nombre);
            free(texto);
            secuenciaLiberar(s);

            return 0;
        }

        if (p == NULL || *p != ')') {
            snprintf(error, tamError, "Linea %d: argumentos invalidos en \"%s\"", linea, nombre);
            free(texto);
            secuenciaLiberar(s);

            return 0;
        }

        if (op.tipo == OP_NEW && op.tam <= 0) {
            snprintf(error, tamError, "Linea %d: el tamano de new debe ser mayor que 0", linea);
            free(texto);
            secuenciaLiberar(s);

            return 0;
        }

        if (!agregar(s, op)) {
            snprintf(error, tamError, "Memoria insuficiente");
            free(texto);
            secuenciaLiberar(s);

            return 0;
        }

        p = saltarSeparadores(p + 1, &linea);
    }

    free(texto);

    if (s->cantidad == 0) {
        snprintf(error, tamError, "El archivo no contiene operaciones");

        return 0;
    }

    resolverPunteros(s);

    return 1;
}

/* ---------------------------------------------------------------
   Generacion aleatoria
   --------------------------------------------------------------- */

typedef struct {
    int restantes;    /* operaciones que le quedan al proceso */
    int *vivos;       /* punteros en su tabla de simbolos */
    int numVivos;
    int capVivos;
} EstadoProceso;

static int tamanoAleatorio(Prng *g)
{
    int r = (int)prngRango(g, 100);

    if (r < 70) {
        return prngEntre(g, 1, 4096);          /* 1 pagina */
    }

    if (r < 95) {
        return prngEntre(g, 4097, 12288);      /* 2 o 3 paginas */
    }

    return prngEntre(g, 12289, 32768);         /* 4 a 8 paginas */
}

int secuenciaGenerar(Secuencia *s, uint64_t semilla, int procesos, int operaciones)
{
    secuenciaIniciar(s);

    /* Cada proceso necesita al menos un new y su kill */
    if (procesos < 1 || operaciones < 2 * procesos) {
        return 0;
    }

    Prng g;
    prngIniciar(&g, semilla);

    EstadoProceso *est = calloc((size_t)procesos + 1, sizeof(EstadoProceso));
    int *peso = calloc((size_t)procesos + 1, sizeof(int));
    int *activos = calloc((size_t)procesos, sizeof(int));

    if (est == NULL || peso == NULL || activos == NULL) {
        free(est);
        free(peso);
        free(activos);

        return 0;
    }

    /* Reparto de las N operaciones: 2 fijas por proceso y el resto segun
       un peso aleatorio, para que los procesos tengan largos distintos */
    int pesoTotal = 0;

    for (int p = 1; p <= procesos; p++) {
        est[p].restantes = 2;
        peso[p] = prngEntre(&g, 1, 10);
        pesoTotal += peso[p];
    }

    for (int k = 0; k < operaciones - 2 * procesos; k++) {
        int r = (int)prngRango(&g, (uint32_t)pesoTotal);
        int p = 1;

        while (peso[p] <= r) {
            r -= peso[p];
            p++;
        }

        est[p].restantes++;
    }

    int numActivos = procesos;

    for (int p = 1; p <= procesos; p++) {
        activos[p - 1] = p;
    }

    int siguientePtr = 0;
    int exito = 1;

    /* Intercalado libre: en cada paso avanza un proceso al azar */
    while (0 < numActivos && exito) {
        int idx = (int)prngRango(&g, (uint32_t)numActivos);
        int pid = activos[idx];
        EstadoProceso *e = &est[pid];
        Operacion op;

        memset(&op, 0, sizeof(op));
        op.pid = pid;

        if (e->restantes == 1) {
            /* kill: una sola vez y siempre la ultima del proceso */
            op.tipo = OP_KILL;
            activos[idx] = activos[numActivos - 1];
            numActivos--;
        }
        else {
            int r = (int)prngRango(&g, 100);

            /* use y delete solo con al menos un puntero vivo.
               Proporciones: 20 % new, 60 % use, 20 % delete */
            if (e->numVivos == 0 || r < 20) {
                op.tipo = OP_NEW;
                op.tam = tamanoAleatorio(&g);
                siguientePtr++;
                op.ptr = siguientePtr;

                if (e->numVivos == e->capVivos) {
                    int nueva = e->capVivos == 0 ? 8 : e->capVivos * 2;
                    int *mayor = realloc(e->vivos, (size_t)nueva * sizeof(int));

                    if (mayor == NULL) {
                        exito = 0;
                        break;
                    }

                    e->vivos = mayor;
                    e->capVivos = nueva;
                }

                e->vivos[e->numVivos] = op.ptr;
                e->numVivos++;
            }
            else if (r < 80) {
                op.tipo = OP_USE;
                op.ptr = e->vivos[prngRango(&g, (uint32_t)e->numVivos)];
            }
            else {
                int j = (int)prngRango(&g, (uint32_t)e->numVivos);

                op.tipo = OP_DELETE;
                op.ptr = e->vivos[j];
                e->vivos[j] = e->vivos[e->numVivos - 1];
                e->numVivos--;
            }
        }

        e->restantes--;

        if (!agregar(s, op)) {
            exito = 0;
        }
    }

    for (int p = 1; p <= procesos; p++) {
        free(est[p].vivos);
    }

    free(est);
    free(peso);
    free(activos);

    if (!exito) {
        secuenciaLiberar(s);

        return 0;
    }

    resolverPunteros(s);

    return 1;
}

/* ---------------------------------------------------------------
   Escritura y utilidades
   --------------------------------------------------------------- */

void operacionTexto(const Operacion *op, char *texto, size_t tamTexto)
{
    switch (op->tipo) {
    case OP_NEW:
        snprintf(texto, tamTexto, "new(%d,%d)", op->pid, op->tam);
        break;
    case OP_USE:
        snprintf(texto, tamTexto, "use(%d)", op->ptr);
        break;
    case OP_DELETE:
        snprintf(texto, tamTexto, "delete(%d)", op->ptr);
        break;
    default:
        snprintf(texto, tamTexto, "kill(%d)", op->pid);
        break;
    }
}

int secuenciaGuardar(const Secuencia *s, const char *ruta)
{
    FILE *archivo = fopen(ruta, "w");

    if (archivo == NULL) {
        return 0;
    }

    char texto[64];

    for (int i = 0; i < s->cantidad; i++) {
        operacionTexto(&s->ops[i], texto, sizeof(texto));
        fprintf(archivo, "%s\n", texto);
    }

    return fclose(archivo) == 0;
}

int secuenciaContarProcesos(const Secuencia *s)
{
    if (s->maxPid <= 0) {
        return 0;
    }

    char *visto = calloc((size_t)s->maxPid + 1, 1);
    int cuenta = 0;

    if (visto == NULL) {
        return 0;
    }

    for (int i = 0; i < s->cantidad; i++) {
        int pid = s->ops[i].pid;

        if (0 < pid && !visto[pid]) {
            visto[pid] = 1;
            cuenta++;
        }
    }

    free(visto);

    return cuenta;
}
