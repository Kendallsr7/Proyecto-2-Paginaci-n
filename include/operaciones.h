#ifndef OPERACIONES_H
#define OPERACIONES_H

#include <stddef.h>
#include <stdint.h>

typedef enum {
    OP_NEW,
    OP_USE,
    OP_DELETE,
    OP_KILL
} TipoOperacion;

/* Una instruccion de la sesion.
   new(pid, tam)  -> pid y tam; ptr es el puntero que le toca
   use(ptr)       -> ptr; pid es el dueno del puntero
   delete(ptr)    -> ptr; pid es el dueno del puntero
   kill(pid)      -> pid */
typedef struct {
    TipoOperacion tipo;
    int pid;
    int tam;
    int ptr;
} Operacion;

/* Secuencia completa de operaciones de la sesion. */
typedef struct {
    Operacion *ops;
    int cantidad;
    int capacidad;
    int maxPid;        /* PID mas alto que aparece */
    int cantidadPtrs;  /* cantidad de operaciones new = punteros asignados */
} Secuencia;

void secuenciaIniciar(Secuencia *s);
void secuenciaLiberar(Secuencia *s);

/* Lee un archivo de operaciones con el formato del anexo 1.
   Acepta las operaciones separadas por saltos de linea o por comas.
   Devuelve 1 si tuvo exito; si no, deja el motivo en 'error'. */
int secuenciaCargar(const char *ruta, Secuencia *s, char *error, size_t tamError);

/* Genera una sesion aleatoria con P procesos y N operaciones que cumple
   las reglas del enunciado. La misma semilla produce la misma sesion. */
int secuenciaGenerar(Secuencia *s, uint64_t semilla, int procesos, int operaciones);

/* Escribe la secuencia en el formato del anexo 1. */
int secuenciaGuardar(const Secuencia *s, const char *ruta);

/* Cantidad de procesos distintos que aparecen en la secuencia. */
int secuenciaContarProcesos(const Secuencia *s);

/* Texto de una operacion, por ejemplo "new(1,500)". */
void operacionTexto(const Operacion *op, char *texto, size_t tamTexto);

#endif
