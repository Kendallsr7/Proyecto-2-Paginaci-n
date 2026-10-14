#ifndef PRNG_H
#define PRNG_H

#include <stdint.h>

/* Generador pseudoaleatorio propio (xorshift64*), sembrado con splitmix64.
   No se usa rand() porque su secuencia depende de la biblioteca de C:
   con este generador la misma semilla produce el mismo escenario en
   cualquier maquina. */
typedef struct {
    uint64_t estado;
} Prng;

void prngIniciar(Prng *g, uint64_t semilla);
uint64_t prngSiguiente(Prng *g);

/* Entero uniforme en [0, n). n debe ser mayor que 0. */
uint32_t prngRango(Prng *g, uint32_t n);

/* Entero uniforme en [minimo, maximo]. */
int prngEntre(Prng *g, int minimo, int maximo);

#endif
