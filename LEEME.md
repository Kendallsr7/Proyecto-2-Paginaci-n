# Simulador de paginación — Proyecto 2 (Sistemas Operativos)

Simula la MMU con 100 marcos de 4 KB y compara el algoritmo óptimo (OPT)
contra FIFO, Second Chance (SC), LRU o LFU, en dos paneles coordinados.
Hecho en C con GTK 4.

## Dependencias (Debian 12 "bookworm" o Debian 13 "trixie")

En un Debian limpio no hay compilador, `make` ni GTK, así que hay que instalarlos:

    sudo apt update
    sudo apt install -y build-essential pkg-config libgtk-4-dev

Eso es todo: no se necesita nada más. `libgtk-4-dev` arrastra por sí solo
las bibliotecas que usa el programa (GLib, Pango, Cairo, GdkPixbuf, Graphene,
HarfBuzz). Funciona con GTK 4.8 o superior:
Debian 12 trae GTK 4.8 y Debian 13 trae GTK 4.18.

## Compilar y ejecutar

    make
    ./paginacion

Necesita una sesión gráfica (X11 o Wayland). Por SSH: `ssh -X usuario@host`.
`make clean` borra lo compilado.

## Uso

1. Indique la semilla (misma semilla = mismo escenario) y el algoritmo.
2. O bien cargue un archivo de operaciones con "Examinar...", o bien escoja
   P (10, 50, 100) y N (500, 1000, 5000) para que se genere uno.
3. "Preparar operaciones" y, si se desea, "Descargar archivo..." para guardarlo.
4. "Iniciar simulacion": se abre la ventana con OPT y el algoritmo elegido.
   "Pausar" y "Reanudar" detienen y continúan la simulación.

Formato del archivo (una operación por línea, también acepta comas):

    new(1,500)
    use(1)
    delete(1)
    kill(1)

## Pruebas de la lógica

    sh pruebas/correr_pruebas.sh

## Qué se probó y dónde

- GTK 4.8.3 (la versión de Debian 12), compilado desde su código fuente
  sobre Ubuntu 24.04: compila sin avisos, abre, genera, carga y guarda
  archivos, y corre la simulación completa.
- GTK 4.14.5 (Ubuntu 24.04): lo mismo.
- No se pudo probar en una instalación real de Debian.
