#ifndef MMU_H
#define MMU_H

#include <stdint.h>

#include "operaciones.h"

#define MARCOS       100     /* memoria real: 100 paginas = 400 KB */
#define TAM_PAGINA   4096    /* 4 KB */
#define COSTO_HIT    1       /* segundos por pagina en memoria real */
#define COSTO_FALLO  5       /* segundos por pagina traida de disco */

typedef enum {
    ALG_OPT,
    ALG_FIFO,
    ALG_SC,
    ALG_LRU,
    ALG_LFU
} Algoritmo;

typedef struct {
    int id;            /* PAGE ID */
    int pid;
    int ptr;           /* L-ADDR: puntero logico al que pertenece */
    int viva;          /* 0 si ya se borro con delete o kill */
    int enRam;         /* bandera: 1 memoria real, 0 memoria virtual */
    int marco;         /* M-ADDR: marco en memoria real, -1 si no esta */
    int dirDisco;      /* D-ADDR: posicion en disco, -1 si esta en RAM */
    long tiempoCarga;  /* LOADED-T: reloj cuando entro a memoria real */
    int fijada;        /* no puede salir mientras se atiende su operacion */

    /* Datos de los algoritmos */
    uint64_t ordenCarga;   /* FIFO y SC: orden de llegada a memoria real */
    uint64_t ultimoUso;    /* LRU: orden del ultimo acceso */
    long tiempoUltimoUso;  /* LRU: reloj del ultimo acceso (para mostrar) */
    int bitR;              /* SC: bit de referencia */
    int contador;          /* LFU: accesos desde que entro a memoria real */
} Pagina;

typedef struct {
    int pid;
    int tam;           /* bytes solicitados */
    int vivo;
    int numPaginas;
    int *paginas;      /* indices en Mmu.paginas, en orden */
} Puntero;

typedef struct {
    Algoritmo alg;
    const Secuencia *sec;
    int opActual;           /* indice de la siguiente operacion */

    Pagina *paginas;        /* todas las paginas creadas, por ID */
    int numPaginas;
    int capPaginas;

    Puntero *ptrs;          /* mapa de memoria: indice = numero de ptr */
    int marcos[MARCOS];     /* indice de pagina en cada marco, -1 si libre */
    char *estadoPid;        /* 0 sin iniciar, 1 corriendo, 2 terminado */

    /* OPT: para cada ptr, los indices de las operaciones use que lo usan */
    int **usos;
    int *numUsos;

    uint64_t reloj;         /* contador logico para ordenar eventos */
    int siguienteDisco;

    /* Estadisticas */
    long tiempo;            /* reloj de la simulacion, en segundos */
    long thrashing;         /* segundos gastados en fallos de pagina */
    int paginasRam;
    int paginasVirtual;
    long fragmentacion;     /* bytes desperdiciados dentro de las paginas */
    int procesosCorriendo;
    long fallos;
    long hits;

    /* Paginas vivas en orden de ID, para la tabla de la MMU */
    int *vista;
    int numVista;
} Mmu;

int mmuIniciar(Mmu *m, const Secuencia *s, Algoritmo alg);
void mmuLiberar(Mmu *m);

/* Ejecuta la siguiente operacion. Devuelve 0 si ya no quedan. */
int mmuPaso(Mmu *m);

/* OPT: indice de la proxima operacion que usara el ptr despues de la
   operacion actual, o -1 si no se vuelve a usar. */
int mmuProximoUso(const Mmu *m, int ptr);

const char *algoritmoNombre(Algoritmo alg);

#endif
