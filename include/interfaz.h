#ifndef INTERFAZ_H
#define INTERFAZ_H

#include <gtk/gtk.h>

#include "mmu.h"
#include "operaciones.h"

/* Estado compartido de la aplicacion. */
typedef struct {
    GtkApplication *app;
    GtkWindow *ventanaPreparacion;

    GtkWidget *entradaSemilla;
    GtkWidget *listaAlgoritmo;
    GtkWidget *entradaArchivo;
    GtkWidget *listaProcesos;
    GtkWidget *listaOperaciones;
    GtkWidget *botonDescargar;
    GtkWidget *botonIniciar;
    GtkWidget *etiquetaEstado;

    Secuencia secuencia;
    int secuenciaLista;
} Aplicacion;

/* Ventana inicial: parametros de la simulacion. */
void ventanaPreparacionMostrar(Aplicacion *app);

/* Ventana de simulacion: OPT y el algoritmo elegido en paralelo. */
void ventanaSimulacionAbrir(Aplicacion *app, Algoritmo algoritmo);

/* Color pastel estable para un PID (mismo color en RAM y en la MMU). */
void colorProceso(int pid, double *r, double *g, double *b);

#endif
