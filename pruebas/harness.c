#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "mmu.h"
#include "prng.h"

static int checkInv(const Mmu *m, const char *ctx) {
    int ram = 0, virt = 0, ok = 1;
    long frag = 0;
    for (int i = 0; i < m->numPaginas; i++) {
        const Pagina *p = &m->paginas[i];
        if (!p->viva) { if (p->enRam) { printf("%s: pagina muerta en RAM\n", ctx); ok = 0; } continue; }
        if (p->enRam) { ram++; if (m->marcos[p->marco] != i) { printf("%s: marco inconsistente\n", ctx); ok = 0; } }
        else virt++;
    }
    for (int p = 1; p <= m->sec->cantidadPtrs; p++)
        if (m->ptrs[p].vivo) frag += (long)m->ptrs[p].numPaginas * TAM_PAGINA - m->ptrs[p].tam;
    int marcosUsados = 0;
    for (int f = 0; f < MARCOS; f++) if (m->marcos[f] >= 0) marcosUsados++;
    if (ram != m->paginasRam || virt != m->paginasVirtual || ram > MARCOS || marcosUsados != ram) { printf("%s: contadores ram=%d/%d virt=%d/%d marcos=%d\n", ctx, ram, m->paginasRam, virt, m->paginasVirtual, marcosUsados); ok = 0; }
    if (frag != m->fragmentacion) { printf("%s: frag %ld vs %ld\n", ctx, frag, m->fragmentacion); ok = 0; }
    if (m->tiempo != m->hits * COSTO_HIT + m->fallos * COSTO_FALLO) { printf("%s: tiempo\n", ctx); ok = 0; }
    if (m->thrashing != m->fallos * COSTO_FALLO) { printf("%s: thrash\n", ctx); ok = 0; }
    return ok;
}

/* valida reglas del enunciado sobre la secuencia */
static int validar(const Secuencia *s, int P, int N) {
    int ok = 1;
    int *killed = calloc(P + 2, sizeof(int));
    char *vivo = calloc(s->cantidadPtrs + 2, 1);
    int *dueno = calloc(s->cantidadPtrs + 2, sizeof(int));
    int *opsPid = calloc(P + 2, sizeof(int));
    if (s->cantidad > N) { printf("mas de N ops\n"); ok = 0; }
    for (int i = 0; i < s->cantidad; i++) {
        const Operacion *o = &s->ops[i];
        if (o->pid < 1 || o->pid > P) { printf("pid fuera de rango %d\n", o->pid); ok = 0; continue; }
        if (killed[o->pid]) { printf("op despues de kill pid %d\n", o->pid); ok = 0; }
        opsPid[o->pid]++;
        if (o->tipo == OP_NEW) { vivo[o->ptr] = 1; dueno[o->ptr] = o->pid; }
        else if (o->tipo == OP_USE || o->tipo == OP_DELETE) {
            if (!vivo[o->ptr] || dueno[o->ptr] != o->pid) { printf("ptr invalido op %d\n", i); ok = 0; }
            if (o->tipo == OP_DELETE) vivo[o->ptr] = 0;
        } else killed[o->pid]++;
    }
    for (int p = 1; p <= P; p++) if (killed[p] != 1) { printf("pid %d kill=%d\n", p, killed[p]); ok = 0; }
    free(killed); free(vivo); free(dueno); free(opsPid);
    return ok;
}

int main(int argc, char **argv) {
    const char *nombres[] = {"OPT","FIFO","SC","LRU","LFU"};
    int Ps[] = {10, 50, 100}, Ns[] = {500, 1000, 5000};
    int fallosTotales = 0, optPeor = 0, corridas = 0;

    /* 1. Ejemplo del anexo 1 */
    if (argc > 1) {
        Secuencia s; char err[256];
        if (!secuenciaCargar(argv[1], &s, err, sizeof err)) { printf("Error carga: %s\n", err); return 1; }
        printf("Anexo1: %d ops, %d procesos, %d ptrs\n", s.cantidad, secuenciaContarProcesos(&s), s.cantidadPtrs);
        for (int a = 0; a < 5; a++) {
            Mmu m; mmuIniciar(&m, &s, a);
            while (mmuPaso(&m)) if (!checkInv(&m, nombres[a])) fallosTotales++;
            printf("  %-5s tiempo=%ld thr=%ld hits=%ld fallos=%ld ram=%d virt=%d frag=%ld proc=%d\n", nombres[a], m.tiempo, m.thrashing, m.hits, m.fallos, m.paginasRam, m.paginasVirtual, m.fragmentacion, m.procesosCorriendo);
            mmuLiberar(&m);
        }
        secuenciaLiberar(&s);
    }

    /* 2. Todas las combinaciones P x N con varias semillas */
    for (int ip = 0; ip < 3; ip++) for (int in = 0; in < 3; in++) {
        long acum[5] = {0}; long thr[5] = {0}; int nseeds = 0;
        for (unsigned long long seed = 1; seed <= 30; seed++) {
            Secuencia s;
            if (!secuenciaGenerar(&s, seed, Ps[ip], Ns[in])) { printf("fallo generar P=%d N=%d seed=%llu\n", Ps[ip], Ns[in], seed); fallosTotales++; continue; }
            if (!validar(&s, Ps[ip], Ns[in])) { printf("secuencia invalida P=%d N=%d seed=%llu\n", Ps[ip], Ns[in], seed); fallosTotales++; }
            long faultsOpt = 0;
            for (int a = 0; a < 5; a++) {
                Mmu m; mmuIniciar(&m, &s, a);
                while (mmuPaso(&m)) { if (!checkInv(&m, nombres[a])) { fallosTotales++; break; } }
                if (m.procesosCorriendo != 0) { printf("procesos corriendo al final: %d\n", m.procesosCorriendo); fallosTotales++; }
                if (m.paginasRam != 0 || m.paginasVirtual != 0 || m.fragmentacion != 0) { printf("residuos al final %s ram=%d virt=%d frag=%ld\n", nombres[a], m.paginasRam, m.paginasVirtual, m.fragmentacion); fallosTotales++; }
                if (a == 0) faultsOpt = m.fallos; else if (m.fallos < faultsOpt) { optPeor++; printf("OPT peor que %s: P=%d N=%d seed=%llu opt=%ld %s=%ld\n", nombres[a], Ps[ip], Ns[in], seed, faultsOpt, nombres[a], m.fallos); }
                acum[a] += m.tiempo; thr[a] += m.thrashing;
                mmuLiberar(&m); corridas++;
            }
            secuenciaLiberar(&s); nseeds++;
        }
        printf("P=%3d N=%4d | tiempo medio: ", Ps[ip], Ns[in]);
        for (int a = 0; a < 5; a++) printf("%s=%ld(%.0f%%) ", nombres[a], acum[a]/nseeds, 100.0*thr[a]/acum[a]);
        printf("\n");
    }

    /* 3. Repetibilidad: misma semilla -> misma secuencia; semillas distintas -> distinta */
    Secuencia a, b, c;
    secuenciaGenerar(&a, 42, 50, 1000); secuenciaGenerar(&b, 42, 50, 1000); secuenciaGenerar(&c, 43, 50, 1000);
    int igual = a.cantidad == b.cantidad && memcmp(a.ops, b.ops, a.cantidad * sizeof(Operacion)) == 0;
    int distinta = a.cantidad != c.cantidad || memcmp(a.ops, c.ops, a.cantidad * sizeof(Operacion)) != 0;
    printf("Repetibilidad misma semilla: %s | semilla distinta difiere: %s\n", igual ? "OK" : "FALLA", distinta ? "OK" : "FALLA");
    /* guardar/cargar roundtrip */
    secuenciaGuardar(&a, "/tmp/rt.txt");
    Secuencia r; char err[256];
    int rt = secuenciaCargar("/tmp/rt.txt", &r, err, sizeof err) && r.cantidad == a.cantidad && memcmp(r.ops, a.ops, a.cantidad * sizeof(Operacion)) == 0;
    printf("Roundtrip guardar/cargar: %s\n", rt ? "OK" : "FALLA");
    printf("corridas=%d fallos_invariantes=%d opt_peor=%d\n", corridas, fallosTotales, optPeor);
    return fallosTotales != 0;
}
