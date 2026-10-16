#include <stdio.h>
#include <math.h>
#include <stdlib.h>

#include "interfaz.h"

#define ALTO_FILA        22
#define ALTO_ENCABEZADO  26
#define COLUMNAS_TABLA   8

static const char *TITULOS_TABLA[COLUMNAS_TABLA] = {
    "PAGE ID", "PID", "LOADED", "L-ADDR", "M-ADDR", "D-ADDR", "LOADED-T", "MARK"
};

enum {
    E_PROCESOS, E_TIEMPO,
    E_RAM_KB, E_RAM_PCT, E_VRAM_KB, E_VRAM_PCT,
    E_CARGADAS, E_DESCARGADAS, E_THRASHING, E_FRAGMENTACION,
    TOTAL_ESTADISTICAS
};

/* Una mitad de la ventana: un algoritmo con su RAM, su MMU y sus datos. */
typedef struct {
    Mmu mmu;
    GtkWidget *barraRam;
    GtkWidget *tabla;
    GtkAdjustment *ajuste;
    int filasVisibles;
    GtkWidget *valores[TOTAL_ESTADISTICAS];
} Panel;

typedef struct {
    Aplicacion *app;
    GtkWidget *ventana;
    Panel panel[2];          /* 0 = OPT, 1 = algoritmo elegido */
    GtkWidget *botonPausa;
    GtkWidget *etiquetaPaso;
    guint temporizador;
    guint intervalo;
    int terminado;
} Simulacion;

/* ---------------------------------------------------------------
   Colores
   --------------------------------------------------------------- */

void colorProceso(int pid, double *r, double *g, double *b)
{
    /* Tono distinto por PID usando la razon aurea; saturacion baja
       para que el texto negro se lea encima */
    double h = fmod(pid * 0.618033988749895, 1.0) * 6.0;
    double s = 0.40;
    double v = 0.97;
    int i = (int)h;
    double f = h - i;
    double p = v * (1 - s);
    double q = v * (1 - s * f);
    double t = v * (1 - s * (1 - f));

    switch (i % 6) {
    case 0:  *r = v; *g = t; *b = p; break;
    case 1:  *r = q; *g = v; *b = p; break;
    case 2:  *r = p; *g = v; *b = t; break;
    case 3:  *r = p; *g = q; *b = v; break;
    case 4:  *r = t; *g = p; *b = v; break;
    default: *r = v; *g = p; *b = q; break;
    }
}

/* ---------------------------------------------------------------
   Dibujo de la RAM: 100 marcos coloreados por proceso
   --------------------------------------------------------------- */

static void dibujarRam(GtkDrawingArea *area, cairo_t *cr, int ancho, int alto, gpointer datos)
{
    (void)area;

    Panel *panel = datos;
    double celda = (double)(ancho - 1) / MARCOS;

    cairo_set_line_width(cr, 1.0);

    for (int f = 0; f < MARCOS; f++) {
        int pi = panel->mmu.marcos[f];
        double r = 1.0, g = 1.0, b = 1.0;

        if (0 <= pi) {
            colorProceso(panel->mmu.paginas[pi].pid, &r, &g, &b);
        }

        cairo_rectangle(cr, 0.5 + f * celda, 0.5, celda, alto - 1);
        cairo_set_source_rgb(cr, r, g, b);
        cairo_fill_preserve(cr);
        cairo_set_source_rgb(cr, 0.55, 0.55, 0.55);
        cairo_stroke(cr);
    }
}

/* ---------------------------------------------------------------
   Dibujo de la tabla de la MMU (solo las filas visibles)
   --------------------------------------------------------------- */

static void textoCentrado(cairo_t *cr, PangoLayout *layout, const char *texto,
                          double x, double y, double ancho, double alto)
{
    int tw = 0;
    int th = 0;

    pango_layout_set_text(layout, texto, -1);
    pango_layout_get_pixel_size(layout, &tw, &th);
    cairo_move_to(cr, x + (ancho - tw) / 2.0, y + (alto - th) / 2.0);
    pango_cairo_show_layout(cr, layout);
}

/* Columna MARK: el dato que usa cada algoritmo para decidir */
static void textoMarca(const Mmu *m, const Pagina *p, char *texto, size_t tam)
{
    texto[0] = '\0';

    if (!p->enRam) {
        return;
    }

    switch (m->alg) {
    case ALG_OPT: {
        int prox = mmuProximoUso(m, p->ptr);

        if (prox < 0) {
            snprintf(texto, tam, "nunca");
        }
        else {
            snprintf(texto, tam, "op %d", prox + 1);
        }
        break;
    }
    case ALG_SC:
        snprintf(texto, tam, "R=%d", p->bitR);
        break;
    case ALG_LRU:
        snprintf(texto, tam, "%lds", p->tiempoUltimoUso);
        break;
    case ALG_LFU:
        snprintf(texto, tam, "%d", p->contador);
        break;
    default:
        break;
    }
}

static void dibujarTabla(GtkDrawingArea *area, cairo_t *cr, int ancho, int alto, gpointer datos)
{
    Panel *panel = datos;
    const Mmu *m = &panel->mmu;
    double col = (double)ancho / COLUMNAS_TABLA;

    PangoLayout *layout = gtk_widget_create_pango_layout(GTK_WIDGET(area), NULL);
    PangoFontDescription *normal = pango_font_description_from_string("Sans 9");
    PangoFontDescription *negrita = pango_font_description_from_string("Sans Bold 9");

    cairo_set_line_width(cr, 1.0);

    /* Fondo blanco */
    cairo_set_source_rgb(cr, 1, 1, 1);
    cairo_paint(cr);

    /* Encabezado */
    cairo_set_source_rgb(cr, 0.88, 0.88, 0.88);
    cairo_rectangle(cr, 0, 0, ancho, ALTO_ENCABEZADO);
    cairo_fill(cr);

    pango_layout_set_font_description(layout, negrita);
    cairo_set_source_rgb(cr, 0, 0, 0);

    for (int c = 0; c < COLUMNAS_TABLA; c++) {
        textoCentrado(cr, layout, TITULOS_TABLA[c], c * col, 0, col, ALTO_ENCABEZADO);
    }

    /* Filas */
    pango_layout_set_font_description(layout, normal);

    int primera = (int)gtk_adjustment_get_value(panel->ajuste);
    char celdas[COLUMNAS_TABLA][32];

    for (int fila = 0; ; fila++) {
        int indice = primera + fila;
        double y = ALTO_ENCABEZADO + fila * ALTO_FILA;

        if (m->numVista <= indice || alto <= y) {
            break;
        }

        const Pagina *p = &m->paginas[m->vista[indice]];
        double r, g, b;

        colorProceso(p->pid, &r, &g, &b);
        cairo_set_source_rgb(cr, r, g, b);
        cairo_rectangle(cr, 0, y, ancho, ALTO_FILA);
        cairo_fill(cr);

        snprintf(celdas[0], 32, "%d", p->id);
        snprintf(celdas[1], 32, "%d", p->pid);
        snprintf(celdas[2], 32, "%s", p->enRam ? "X" : "");
        snprintf(celdas[3], 32, "%d", p->ptr);
        snprintf(celdas[4], 32, "%s", "");
        snprintf(celdas[5], 32, "%s", "");
        snprintf(celdas[6], 32, "%s", "");

        if (p->enRam) {
            snprintf(celdas[4], 32, "%d", p->marco);
            snprintf(celdas[6], 32, "%lds", p->tiempoCarga);
        }
        else {
            snprintf(celdas[5], 32, "%d", p->dirDisco);
        }

        textoMarca(m, p, celdas[7], 32);

        cairo_set_source_rgb(cr, 0, 0, 0);

        for (int c = 0; c < COLUMNAS_TABLA; c++) {
            textoCentrado(cr, layout, celdas[c], c * col, y, col, ALTO_FILA);
        }

        cairo_set_source_rgb(cr, 0.75, 0.75, 0.75);
        cairo_move_to(cr, 0, y + ALTO_FILA - 0.5);
        cairo_line_to(cr, ancho, y + ALTO_FILA - 0.5);
        cairo_stroke(cr);
    }

    /* Lineas verticales y borde */
    cairo_set_source_rgb(cr, 0.6, 0.6, 0.6);

    for (int c = 1; c < COLUMNAS_TABLA; c++) {
        cairo_move_to(cr, floor(c * col) + 0.5, 0);
        cairo_line_to(cr, floor(c * col) + 0.5, alto);
    }

    cairo_rectangle(cr, 0.5, 0.5, ancho - 1, alto - 1);
    cairo_stroke(cr);

    pango_font_description_free(normal);
    pango_font_description_free(negrita);
    g_object_unref(layout);
}

/* La tabla puede tener miles de filas: se dibuja solo la parte visible
   y una barra de desplazamiento indica que fila va primero. */
static void actualizarAjuste(Panel *panel)
{
    double total = panel->mmu.numVista;
    double pagina = panel->filasVisibles;
    double valor = gtk_adjustment_get_value(panel->ajuste);
    double maximo = total - pagina;

    if (maximo < 0) {
        maximo = 0;
    }

    if (maximo < valor) {
        valor = maximo;
    }

    gtk_adjustment_configure(panel->ajuste, valor, 0, total < pagina ? pagina : total,
                             1, pagina, pagina);
}

static void alRedimensionarTabla(GtkDrawingArea *area, int ancho, int alto, gpointer datos)
{
    (void)area;
    (void)ancho;

    Panel *panel = datos;
    int filas = (alto - ALTO_ENCABEZADO) / ALTO_FILA;

    panel->filasVisibles = filas < 1 ? 1 : filas;
    actualizarAjuste(panel);
}

static gboolean alDesplazarTabla(GtkEventControllerScroll *control, double dx, double dy,
                                 gpointer datos)
{
    (void)control;
    (void)dx;

    Panel *panel = datos;

    gtk_adjustment_set_value(panel->ajuste, gtk_adjustment_get_value(panel->ajuste) + dy * 3);

    return TRUE;
}

static void alCambiarAjuste(GtkAdjustment *ajuste, gpointer datos)
{
    (void)ajuste;

    Panel *panel = datos;

    gtk_widget_queue_draw(panel->tabla);
}

/* ---------------------------------------------------------------
   Estadisticas
   --------------------------------------------------------------- */

static void ponerValor(Panel *panel, int cual, const char *texto)
{
    gtk_label_set_text(GTK_LABEL(panel->valores[cual]), texto);
}

static void actualizarEstadisticas(Panel *panel)
{
    const Mmu *m = &panel->mmu;
    char texto[128];

    snprintf(texto, sizeof(texto), "%d", m->procesosCorriendo);
    ponerValor(panel, E_PROCESOS, texto);

    snprintf(texto, sizeof(texto), "%ld s", m->tiempo);
    ponerValor(panel, E_TIEMPO, texto);

    /* 100 marcos de 4 KB: el porcentaje de RAM coincide con los marcos usados */
    snprintf(texto, sizeof(texto), "%d KB", m->paginasRam * (TAM_PAGINA / 1024));
    ponerValor(panel, E_RAM_KB, texto);

    snprintf(texto, sizeof(texto), "%.0f %%", 100.0 * m->paginasRam / MARCOS);
    ponerValor(panel, E_RAM_PCT, texto);

    snprintf(texto, sizeof(texto), "%d KB", m->paginasVirtual * (TAM_PAGINA / 1024));
    ponerValor(panel, E_VRAM_KB, texto);

    /* V-RAM como porcentaje de la RAM: 500 KB sobre 400 KB = 125 % */
    snprintf(texto, sizeof(texto), "%.0f %%", 100.0 * m->paginasVirtual / MARCOS);
    ponerValor(panel, E_VRAM_PCT, texto);

    snprintf(texto, sizeof(texto), "%d", m->paginasRam);
    ponerValor(panel, E_CARGADAS, texto);

    snprintf(texto, sizeof(texto), "%d", m->paginasVirtual);
    ponerValor(panel, E_DESCARGADAS, texto);

    double porcentaje = m->tiempo == 0 ? 0.0 : 100.0 * m->thrashing / m->tiempo;

    if (50.0 < porcentaje) {
        snprintf(texto, sizeof(texto),
                 "<span foreground=\"#C00000\" weight=\"bold\">%ld s (%.1f %%)</span>",
                 m->thrashing, porcentaje);
    }
    else {
        snprintf(texto, sizeof(texto), "%ld s (%.1f %%)", m->thrashing, porcentaje);
    }

    gtk_label_set_markup(GTK_LABEL(panel->valores[E_THRASHING]), texto);

    snprintf(texto, sizeof(texto), "%.2f KB", m->fragmentacion / 1024.0);
    ponerValor(panel, E_FRAGMENTACION, texto);
}

static void refrescar(Simulacion *sim)
{
    for (int i = 0; i < 2; i++) {
        actualizarAjuste(&sim->panel[i]);
        actualizarEstadisticas(&sim->panel[i]);
        gtk_widget_queue_draw(sim->panel[i].barraRam);
        gtk_widget_queue_draw(sim->panel[i].tabla);
    }

    const Secuencia *s = &sim->app->secuencia;
    int hechas = sim->panel[0].mmu.opActual;
    char texto[160];

    if (hechas == 0) {
        snprintf(texto, sizeof(texto), "Operacion 0 de %d", s->cantidad);
    }
    else {
        char op[64];

        operacionTexto(&s->ops[hechas - 1], op, sizeof(op));
        snprintf(texto, sizeof(texto), "Operacion %d de %d: %s%s", hechas, s->cantidad, op,
                 sim->terminado ? "   —   Simulacion terminada" : "");
    }

    gtk_label_set_text(GTK_LABEL(sim->etiquetaPaso), texto);
}

/* ---------------------------------------------------------------
   Avance de la simulacion
   --------------------------------------------------------------- */

static gboolean alAvanzar(gpointer datos)
{
    Simulacion *sim = datos;

    /* Ambos algoritmos ejecutan la misma operacion en cada paso, asi
       que avanzan coordinados y terminan al mismo tiempo */
    mmuPaso(&sim->panel[0].mmu);
    mmuPaso(&sim->panel[1].mmu);

    if (sim->app->secuencia.cantidad <= sim->panel[0].mmu.opActual) {
        sim->terminado = 1;
        sim->temporizador = 0;
        gtk_widget_set_sensitive(sim->botonPausa, FALSE);
        refrescar(sim);

        return G_SOURCE_REMOVE;
    }

    refrescar(sim);

    return G_SOURCE_CONTINUE;
}

static void alPausar(GtkButton *boton, gpointer datos)
{
    Simulacion *sim = datos;

    if (sim->terminado) {
        return;
    }

    if (sim->temporizador != 0) {
        g_source_remove(sim->temporizador);
        sim->temporizador = 0;
        gtk_button_set_label(boton, "Reanudar");
    }
    else {
        sim->temporizador = g_timeout_add(sim->intervalo, alAvanzar, sim);
        gtk_button_set_label(boton, "Pausar");
    }
}

static void alCerrar(GtkWidget *ventana, gpointer datos)
{
    (void)ventana;

    Simulacion *sim = datos;

    if (sim->temporizador != 0) {
        g_source_remove(sim->temporizador);
    }

    mmuLiberar(&sim->panel[0].mmu);
    mmuLiberar(&sim->panel[1].mmu);

    gtk_widget_set_visible(GTK_WIDGET(sim->app->ventanaPreparacion), TRUE);
    gtk_window_present(sim->app->ventanaPreparacion);

    g_free(sim);
}

/* ---------------------------------------------------------------
   Construccion de la ventana
   --------------------------------------------------------------- */

static GtkWidget *etiquetaTitulo(const char *texto)
{
    GtkWidget *etiqueta = gtk_label_new(NULL);
    char *marcado = g_markup_printf_escaped("<b>%s</b>", texto);

    gtk_label_set_markup(GTK_LABEL(etiqueta), marcado);
    gtk_label_set_xalign(GTK_LABEL(etiqueta), 0.0);
    g_free(marcado);

    return etiqueta;
}

static GtkWidget *celdaEstadistica(const char *titulo, GtkWidget **valor)
{
    GtkWidget *marco = gtk_frame_new(titulo);

    *valor = gtk_label_new("-");
    gtk_widget_set_margin_top(*valor, 4);
    gtk_widget_set_margin_bottom(*valor, 4);
    gtk_frame_set_child(GTK_FRAME(marco), *valor);

    return marco;
}

static GtkWidget *construirEstadisticas(Panel *panel)
{
    GtkWidget *rejilla = gtk_grid_new();

    gtk_grid_set_row_spacing(GTK_GRID(rejilla), 6);
    gtk_grid_set_column_spacing(GTK_GRID(rejilla), 6);
    gtk_grid_set_column_homogeneous(GTK_GRID(rejilla), TRUE);

    gtk_grid_attach(GTK_GRID(rejilla),
                    celdaEstadistica("Procesos", &panel->valores[E_PROCESOS]), 0, 0, 2, 1);
    gtk_grid_attach(GTK_GRID(rejilla),
                    celdaEstadistica("Tiempo de simulacion", &panel->valores[E_TIEMPO]), 2, 0, 2, 1);

    gtk_grid_attach(GTK_GRID(rejilla),
                    celdaEstadistica("RAM KB", &panel->valores[E_RAM_KB]), 0, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(rejilla),
                    celdaEstadistica("RAM %", &panel->valores[E_RAM_PCT]), 1, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(rejilla),
                    celdaEstadistica("V-RAM KB", &panel->valores[E_VRAM_KB]), 2, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(rejilla),
                    celdaEstadistica("V-RAM %", &panel->valores[E_VRAM_PCT]), 3, 1, 1, 1);

    gtk_grid_attach(GTK_GRID(rejilla),
                    celdaEstadistica("Paginas cargadas", &panel->valores[E_CARGADAS]), 0, 2, 1, 1);
    gtk_grid_attach(GTK_GRID(rejilla),
                    celdaEstadistica("Paginas descargadas", &panel->valores[E_DESCARGADAS]), 1, 2, 1, 1);
    gtk_grid_attach(GTK_GRID(rejilla),
                    celdaEstadistica("Thrashing", &panel->valores[E_THRASHING]), 2, 2, 1, 1);
    gtk_grid_attach(GTK_GRID(rejilla),
                    celdaEstadistica("Fragmentacion", &panel->valores[E_FRAGMENTACION]), 3, 2, 1, 1);

    return rejilla;
}

static GtkWidget *construirBarraRam(Panel *panel, const char *titulo)
{
    GtkWidget *caja = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);

    panel->barraRam = gtk_drawing_area_new();
    gtk_widget_set_size_request(panel->barraRam, -1, 30);
    gtk_widget_set_hexpand(panel->barraRam, TRUE);
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(panel->barraRam), dibujarRam, panel, NULL);

    gtk_box_append(GTK_BOX(caja), etiquetaTitulo(titulo));
    gtk_box_append(GTK_BOX(caja), panel->barraRam);

    return caja;
}

static GtkWidget *construirMitad(Panel *panel, const char *titulo)
{
    GtkWidget *caja = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);

    panel->ajuste = gtk_adjustment_new(0, 0, 1, 1, 1, 1);
    panel->filasVisibles = 1;

    panel->tabla = gtk_drawing_area_new();
    gtk_widget_set_hexpand(panel->tabla, TRUE);
    gtk_widget_set_vexpand(panel->tabla, TRUE);
    gtk_widget_set_size_request(panel->tabla, 520, 200);
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(panel->tabla), dibujarTabla, panel, NULL);
    g_signal_connect(panel->tabla, "resize", G_CALLBACK(alRedimensionarTabla), panel);

    GtkEventController *rueda =
        gtk_event_controller_scroll_new(GTK_EVENT_CONTROLLER_SCROLL_VERTICAL);

    g_signal_connect(rueda, "scroll", G_CALLBACK(alDesplazarTabla), panel);
    gtk_widget_add_controller(panel->tabla, rueda);

    g_signal_connect(panel->ajuste, "value-changed", G_CALLBACK(alCambiarAjuste), panel);

    GtkWidget *barra = gtk_scrollbar_new(GTK_ORIENTATION_VERTICAL, panel->ajuste);
    GtkWidget *filaTabla = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);

    gtk_box_append(GTK_BOX(filaTabla), panel->tabla);
    gtk_box_append(GTK_BOX(filaTabla), barra);
    gtk_widget_set_vexpand(filaTabla, TRUE);

    gtk_box_append(GTK_BOX(caja), etiquetaTitulo(titulo));
    gtk_box_append(GTK_BOX(caja), filaTabla);
    gtk_box_append(GTK_BOX(caja), construirEstadisticas(panel));

    return caja;
}

void ventanaSimulacionAbrir(Aplicacion *app, Algoritmo algoritmo)
{
    Simulacion *sim = g_new0(Simulacion, 1);

    sim->app = app;

    if (!mmuIniciar(&sim->panel[0].mmu, &app->secuencia, ALG_OPT) ||
        !mmuIniciar(&sim->panel[1].mmu, &app->secuencia, algoritmo)) {
        mmuLiberar(&sim->panel[0].mmu);
        mmuLiberar(&sim->panel[1].mmu);
        g_free(sim);

        return;
    }

    /* Que la animacion dure alrededor de un minuto sin importar N */
    guint intervalo = 60000 / (guint)app->secuencia.cantidad;

    sim->intervalo = intervalo < 10 ? 10 : (200 < intervalo ? 200 : intervalo);

    const char *nombre = algoritmoNombre(algoritmo);
    char titulo[128];

    sim->ventana = gtk_application_window_new(app->app);
    snprintf(titulo, sizeof(titulo), "Simulacion: OPT y %s", nombre);
    gtk_window_set_title(GTK_WINDOW(sim->ventana), titulo);
    gtk_window_set_default_size(GTK_WINDOW(sim->ventana), 1300, 760);

    GtkWidget *caja = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);

    gtk_widget_set_margin_top(caja, 12);
    gtk_widget_set_margin_bottom(caja, 12);
    gtk_widget_set_margin_start(caja, 12);
    gtk_widget_set_margin_end(caja, 12);

    /* Controles */
    GtkWidget *filaControles = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);

    sim->botonPausa = gtk_button_new_with_label("Pausar");
    sim->etiquetaPaso = gtk_label_new("");
    gtk_label_set_xalign(GTK_LABEL(sim->etiquetaPaso), 0.0);
    gtk_widget_set_hexpand(sim->etiquetaPaso, TRUE);
    gtk_box_append(GTK_BOX(filaControles), sim->botonPausa);
    gtk_box_append(GTK_BOX(filaControles), sim->etiquetaPaso);
    gtk_box_append(GTK_BOX(caja), filaControles);

    /* RAM de ambos algoritmos */
    char texto[64];

    gtk_box_append(GTK_BOX(caja), construirBarraRam(&sim->panel[0], "RAM - OPT"));
    snprintf(texto, sizeof(texto), "RAM - %s", nombre);
    gtk_box_append(GTK_BOX(caja), construirBarraRam(&sim->panel[1], texto));

    /* MMU y estadisticas lado a lado */
    GtkWidget *filaMitades = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 16);

    gtk_box_set_homogeneous(GTK_BOX(filaMitades), TRUE);
    gtk_widget_set_vexpand(filaMitades, TRUE);
    gtk_box_append(GTK_BOX(filaMitades), construirMitad(&sim->panel[0], "MMU - OPT"));
    snprintf(texto, sizeof(texto), "MMU - %s", nombre);
    gtk_box_append(GTK_BOX(filaMitades), construirMitad(&sim->panel[1], texto));
    gtk_box_append(GTK_BOX(caja), filaMitades);

    gtk_window_set_child(GTK_WINDOW(sim->ventana), caja);

    g_signal_connect(sim->botonPausa, "clicked", G_CALLBACK(alPausar), sim);
    g_signal_connect(sim->ventana, "destroy", G_CALLBACK(alCerrar), sim);

    refrescar(sim);

    gtk_widget_set_visible(GTK_WIDGET(app->ventanaPreparacion), FALSE);
    gtk_window_present(GTK_WINDOW(sim->ventana));

    sim->temporizador = g_timeout_add(sim->intervalo, alAvanzar, sim);
}
