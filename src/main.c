#include <gtk/gtk.h>

#include "interfaz.h"

static void alActivar(GtkApplication *gtkApp, gpointer datos)
{
    Aplicacion *app = datos;

    app->app = gtkApp;
    ventanaPreparacionMostrar(app);
}

int main(int argc, char *argv[])
{
    Aplicacion app;

    memset(&app, 0, sizeof(app));
    secuenciaIniciar(&app.secuencia);

    GtkApplication *gtkApp = gtk_application_new("cr.ac.itcr.paginacion",
                                                 G_APPLICATION_DEFAULT_FLAGS);

    g_signal_connect(gtkApp, "activate", G_CALLBACK(alActivar), &app);

    int estado = g_application_run(G_APPLICATION(gtkApp), argc, argv);

    g_object_unref(gtkApp);
    secuenciaLiberar(&app.secuencia);

    return estado;
}
