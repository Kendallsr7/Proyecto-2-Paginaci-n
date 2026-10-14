#include "prng.h"

void prngIniciar(Prng *g, uint64_t semilla)
{
    /* splitmix64: reparte bien los bits aun con semillas pequenas */
    uint64_t z = semilla + 0x9E3779B97F4A7C15ULL;

    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    z = z ^ (z >> 31);

    /* xorshift no admite estado 0 */
    g->estado = z != 0 ? z : 0x2545F4914F6CDD1DULL;
}

uint64_t prngSiguiente(Prng *g)
{
    uint64_t x = g->estado;

    x ^= x >> 12;
    x ^= x << 25;
    x ^= x >> 27;
    g->estado = x;

    return x * 0x2545F4914F6CDD1DULL;
}

uint32_t prngRango(Prng *g, uint32_t n)
{
    /* Rechazo para que el resultado no quede sesgado por el modulo */
    uint64_t limite = UINT64_MAX - (UINT64_MAX % n);
    uint64_t x;

    do {
        x = prngSiguiente(g);
    } while (limite <= x);

    return (uint32_t)(x % n);
}

int prngEntre(Prng *g, int minimo, int maximo)
{
    return minimo + (int)prngRango(g, (uint32_t)(maximo - minimo + 1));
}
