#!/bin/sh
# Pruebas de la logica (MMU, algoritmos, generador), sin interfaz grafica.
# Uso:  sh pruebas/correr_pruebas.sh
# Revisa en 1350 corridas (P x N x 30 semillas x 5 algoritmos) que la RAM
# nunca pase de 100 marcos, que los contadores sean consistentes, que el
# tiempo sea hits*1 + fallos*5, que OPT nunca tenga mas fallos que otro
# algoritmo y que una misma semilla repita el mismo escenario.
set -e
cd "$(dirname "$0")/.."
SAL=/tmp/prueba_paginacion
if ! gcc -g -fsanitize=address,undefined -Wall -Wextra -Iinclude \
     pruebas/harness.c src/mmu.c src/operaciones.c src/prng.c -o "$SAL" -lm 2>/dev/null; then
    gcc -g -Wall -Wextra -Iinclude \
        pruebas/harness.c src/mmu.c src/operaciones.c src/prng.c -o "$SAL" -lm
fi
ASAN_OPTIONS=detect_leaks=0 "$SAL" pruebas/anexo1.txt
