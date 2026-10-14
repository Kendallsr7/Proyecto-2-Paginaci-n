CC      = gcc
CFLAGS  = -Wall -Wextra -O2 -Iinclude $(shell pkg-config --cflags gtk4)
LDLIBS  = $(shell pkg-config --libs gtk4) -lm

OBJ = build/prng.o build/operaciones.o build/mmu.o \
      build/ventana_preparacion.o build/ventana_simulacion.o build/main.o

paginacion: $(OBJ)
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

build/%.o: src/%.c | build
	$(CC) $(CFLAGS) -c $< -o $@

build:
	mkdir -p build

clean:
	rm -rf build paginacion

.PHONY: clean
