#include <stdlib.h>
#include <string.h>

/* GtkFileChooserNative existe desde GTK 4.0 y funciona en todas las 4.x;
   a partir de 4.10 se marca como obsoleto (la alternativa, GtkFileDialog,
   no existe antes de 4.10 y Debian 12 trae GTK 4.8). */
#define GDK_DISABLE_DEPRECATION_WARNINGS

#include "interfaz.h"

static const char *ALGORITMOS[] = { "FIFO", "SC", "LRU", "LFU", NULL };
static const Algoritmo ALGORITMO_DE[] = { ALG_FIFO, ALG_SC, ALG_LRU, ALG_LFU };
static const char *PROCESOS[] = { "10", "50", "100", NULL };
static const char *OPERACIONES[] = { "500", "1000", "5000", NULL };

static void ponerEstado(Aplicacion *app, const char *texto)
{
    gtk_label_set_text(GTK_LABEL(app->etiquetaEstado), texto);
}

/* Cualquier cambio en los datos de entrada invalida las operaciones
   preparadas; el algoritmo no, porque no cambia la secuencia. */
static void invalidar(Aplicacion *app)
{
    if (!app->secuenciaLista) {
        return;
    }

    app->secuenciaLista = 0;
    secuenciaLiberar(&app->secuencia);
    gtk_widget_set_sensitive(app->botonDescargar, FALSE);
    gtk_widget_set_sensitive(app->botonIniciar, FALSE);
    ponerEstado(app, "Los datos cambiaron: vuelva a preparar las operaciones.");
}

static void alCambiarEntrada(GtkEditable *editable, gpointer datos)
{
    (void)editable;
    invalidar(datos);
}

static void alCambiarLista(GObject *objeto, GParamSpec *pspec, gpointer datos)
{
    (void)objeto;
    (void)pspec;
    invalidar(datos);
}

/* ---------------------------------------------------------------
   Preparar: cargar el archivo o generar las operaciones
   --------------------------------------------------------------- */

static void alPreparar(GtkButton *boton, gpointer datos)
{
    (void)boton;

    Aplicacion *app = datos;
    const char *ruta = gtk_editable_get_text(GTK_EDITABLE(app->entradaArchivo));
    char mensaje[512];

    secuenciaLiberar(&app->secuencia);
    app->secuenciaLista = 0;

    if (ruta != NULL && ruta[0] != '\0') {
        char error[256];

        if (!secuenciaCargar(ruta, &app->secuencia, error, sizeof(error))) {
            snprintf(mensaje, sizeof(mensaje), "Error en el archivo. %s", error);
            ponerEstado(app, mensaje);
            gtk_widget_set_sensitive(app->botonDescargar, FALSE);
            gtk_widget_set_sensitive(app->botonIniciar, FALSE);

            return;
        }

        snprintf(mensaje, sizeof(mensaje),
                 "Archivo cargado: %d operaciones de %d procesos.",
                 app->secuencia.cantidad, secuenciaContarProcesos(&app->secuencia));
    }
    else {
        const char *textoSemilla = gtk_editable_get_text(GTK_EDITABLE(app->entradaSemilla));
        char *fin = NULL;
        unsigned long long semilla = strtoull(textoSemilla, &fin, 10);

        if (textoSemilla[0] == '\0' || fin == NULL || *fin != '\0') {
            ponerEstado(app, "La semilla debe ser un numero entero positivo.");

            return;
        }

        guint iP = gtk_drop_down_get_selected(GTK_DROP_DOWN(app->listaProcesos));
        guint iN = gtk_drop_down_get_selected(GTK_DROP_DOWN(app->listaOperaciones));
        int procesos = atoi(PROCESOS[iP]);
        int operaciones = atoi(OPERACIONES[iN]);

        if (!secuenciaGenerar(&app->secuencia, (uint64_t)semilla, procesos, operaciones)) {
            ponerEstado(app, "No se pudieron generar las operaciones.");

            return;
        }

        snprintf(mensaje, sizeof(mensaje),
                 "Operaciones generadas con la semilla %llu: %d operaciones de %d procesos.",
                 semilla, app->secuencia.cantidad, procesos);
    }

    app->secuenciaLista = 1;
    gtk_widget_set_sensitive(app->botonDescargar, TRUE);
    gtk_widget_set_sensitive(app->botonIniciar, TRUE);
    ponerEstado(app, mensaje);
}

/* ---------------------------------------------------------------
   Archivo de entrada y descarga
   --------------------------------------------------------------- */

static void alEscogerArchivo(GtkNativeDialog *dialogo, int respuesta, gpointer datos)
{
    Aplicacion *app = datos;

    if (respuesta == GTK_RESPONSE_ACCEPT) {
        GFile *archivo = gtk_file_chooser_get_file(GTK_FILE_CHOOSER(dialogo));

        if (archivo != NULL) {
            char *ruta = g_file_get_path(archivo);

            if (ruta != NULL) {
                gtk_editable_set_text(GTK_EDITABLE(app->entradaArchivo), ruta);
                g_free(ruta);
            }

            g_object_unref(archivo);
        }
    }

    g_object_unref(dialogo);
}

static void alExaminar(GtkButton *boton, gpointer datos)
{
    (void)boton;

    Aplicacion *app = datos;
    GtkFileChooserNative *dialogo = gtk_file_chooser_native_new(
        "Archivo de operaciones", app->ventanaPreparacion,
        GTK_FILE_CHOOSER_ACTION_OPEN, "Abrir", "Cancelar");

    gtk_native_dialog_set_modal(GTK_NATIVE_DIALOG(dialogo), TRUE);
    g_signal_connect(dialogo, "response", G_CALLBACK(alEscogerArchivo), app);
    gtk_native_dialog_show(GTK_NATIVE_DIALOG(dialogo));
}

static void alGuardar(GtkNativeDialog *dialogo, int respuesta, gpointer datos)
{
    Aplicacion *app = datos;

    if (respuesta == GTK_RESPONSE_ACCEPT) {
        GFile *archivo = gtk_file_chooser_get_file(GTK_FILE_CHOOSER(dialogo));

        if (archivo != NULL) {
            char *ruta = g_file_get_path(archivo);

            if (ruta != NULL && app->secuenciaLista) {
                char mensaje[512];

                if (secuenciaGuardar(&app->secuencia, ruta)) {
                    snprintf(mensaje, sizeof(mensaje), "Operaciones guardadas en %s", ruta);
                }
                else {
                    snprintf(mensaje, sizeof(mensaje), "No se pudo guardar en %s", ruta);
                }

                ponerEstado(app, mensaje);
            }

            g_free(ruta);
            g_object_unref(archivo);
        }
    }

    g_object_unref(dialogo);
}

static void alDescargar(GtkButton *boton, gpointer datos)
{
    (void)boton;

    Aplicacion *app = datos;
    GtkFileChooserNative *dialogo = gtk_file_chooser_native_new(
        "Guardar archivo de operaciones", app->ventanaPreparacion,
        GTK_FILE_CHOOSER_ACTION_SAVE, "Guardar", "Cancelar");

    gtk_native_dialog_set_modal(GTK_NATIVE_DIALOG(dialogo), TRUE);
    gtk_file_chooser_set_current_name(GTK_FILE_CHOOSER(dialogo), "operaciones.txt");
    g_signal_connect(dialogo, "response", G_CALLBACK(alGuardar), app);
    gtk_native_dialog_show(GTK_NATIVE_DIALOG(dialogo));
}

static void alIniciar(GtkButton *boton, gpointer datos)
{
    (void)boton;

    Aplicacion *app = datos;

    if (!app->secuenciaLista) {
        return;
    }

    guint i = gtk_drop_down_get_selected(GTK_DROP_DOWN(app->listaAlgoritmo));

    ventanaSimulacionAbrir(app, ALGORITMO_DE[i]);
}

/* ---------------------------------------------------------------
   Construccion de la ventana
   --------------------------------------------------------------- */

static GtkWidget *etiquetaCampo(const char *texto)
{
    GtkWidget *etiqueta = gtk_label_new(texto);

    gtk_label_set_xalign(GTK_LABEL(etiqueta), 0.0);

    return etiqueta;
}

void ventanaPreparacionMostrar(Aplicacion *app)
{
    GtkWidget *ventana = gtk_application_window_new(app->app);

    app->ventanaPreparacion = GTK_WINDOW(ventana);
    gtk_window_set_title(GTK_WINDOW(ventana), "Simulador de paginacion");
    gtk_window_set_default_size(GTK_WINDOW(ventana), 640, 380);

    GtkWidget *rejilla = gtk_grid_new();

    gtk_grid_set_row_spacing(GTK_GRID(rejilla), 12);
    gtk_grid_set_column_spacing(GTK_GRID(rejilla), 12);
    gtk_widget_set_margin_top(rejilla, 24);
    gtk_widget_set_margin_bottom(rejilla, 24);
    gtk_widget_set_margin_start(rejilla, 24);
    gtk_widget_set_margin_end(rejilla, 24);

    app->entradaSemilla = gtk_entry_new();
    gtk_editable_set_text(GTK_EDITABLE(app->entradaSemilla), "1");
    gtk_widget_set_hexpand(app->entradaSemilla, TRUE);

    app->listaAlgoritmo = gtk_drop_down_new_from_strings(ALGORITMOS);
    app->listaProcesos = gtk_drop_down_new_from_strings(PROCESOS);
    app->listaOperaciones = gtk_drop_down_new_from_strings(OPERACIONES);

    app->entradaArchivo = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(app->entradaArchivo),
                                   "Opcional: si se indica, se ignoran P y N");
    gtk_widget_set_hexpand(app->entradaArchivo, TRUE);

    GtkWidget *botonExaminar = gtk_button_new_with_label("Examinar...");
    GtkWidget *filaArchivo = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);

    gtk_box_append(GTK_BOX(filaArchivo), app->entradaArchivo);
    gtk_box_append(GTK_BOX(filaArchivo), botonExaminar);

    gtk_grid_attach(GTK_GRID(rejilla), etiquetaCampo("Semilla"), 0, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(rejilla), app->entradaSemilla, 1, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(rejilla), etiquetaCampo("Algoritmo"), 0, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(rejilla), app->listaAlgoritmo, 1, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(rejilla), etiquetaCampo("Archivo de operaciones"), 0, 2, 1, 1);
    gtk_grid_attach(GTK_GRID(rejilla), filaArchivo, 1, 2, 1, 1);
    gtk_grid_attach(GTK_GRID(rejilla), etiquetaCampo("Procesos (P)"), 0, 3, 1, 1);
    gtk_grid_attach(GTK_GRID(rejilla), app->listaProcesos, 1, 3, 1, 1);
    gtk_grid_attach(GTK_GRID(rejilla), etiquetaCampo("Operaciones (N)"), 0, 4, 1, 1);
    gtk_grid_attach(GTK_GRID(rejilla), app->listaOperaciones, 1, 4, 1, 1);

    GtkWidget *botonPreparar = gtk_button_new_with_label("Preparar operaciones");

    app->botonDescargar = gtk_button_new_with_label("Descargar archivo...");
    app->botonIniciar = gtk_button_new_with_label("Iniciar simulacion");
    gtk_widget_set_sensitive(app->botonDescargar, FALSE);
    gtk_widget_set_sensitive(app->botonIniciar, FALSE);

    GtkWidget *filaBotones = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);

    gtk_widget_set_hexpand(botonPreparar, TRUE);
    gtk_widget_set_hexpand(app->botonDescargar, TRUE);
    gtk_widget_set_hexpand(app->botonIniciar, TRUE);
    gtk_box_append(GTK_BOX(filaBotones), botonPreparar);
    gtk_box_append(GTK_BOX(filaBotones), app->botonDescargar);
    gtk_box_append(GTK_BOX(filaBotones), app->botonIniciar);
    gtk_grid_attach(GTK_GRID(rejilla), filaBotones, 0, 5, 2, 1);

    app->etiquetaEstado = gtk_label_new("Indique los datos y presione Preparar operaciones.");
    gtk_label_set_xalign(GTK_LABEL(app->etiquetaEstado), 0.0);
    gtk_label_set_wrap(GTK_LABEL(app->etiquetaEstado), TRUE);
    gtk_grid_attach(GTK_GRID(rejilla), app->etiquetaEstado, 0, 6, 2, 1);

    g_signal_connect(botonExaminar, "clicked", G_CALLBACK(alExaminar), app);
    g_signal_connect(botonPreparar, "clicked", G_CALLBACK(alPreparar), app);
    g_signal_connect(app->botonDescargar, "clicked", G_CALLBACK(alDescargar), app);
    g_signal_connect(app->botonIniciar, "clicked", G_CALLBACK(alIniciar), app);

    g_signal_connect(app->entradaSemilla, "changed", G_CALLBACK(alCambiarEntrada), app);
    g_signal_connect(app->entradaArchivo, "changed", G_CALLBACK(alCambiarEntrada), app);
    g_signal_connect(app->listaProcesos, "notify::selected", G_CALLBACK(alCambiarLista), app);
    g_signal_connect(app->listaOperaciones, "notify::selected", G_CALLBACK(alCambiarLista), app);

    gtk_window_set_child(GTK_WINDOW(ventana), rejilla);
    gtk_window_present(GTK_WINDOW(ventana));
}
