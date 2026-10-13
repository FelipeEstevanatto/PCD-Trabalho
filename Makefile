# Filtro bilateral (sequencial + OpenMP) e gerador de ruido.
#
#   make            # compila os tres binarios
#   make clean
#
# O sequencial e o paralelo sao dois fontes quase identicos (relatorio,
# Sec. 4.2): src/bilateral_seq.c e src/bilateral_omp.c. A unica diferenca
# funcional e a diretiva OpenMP, marcada em src/bilateral_omp.c; o codigo
# comum fica em src/bilateral_common.c.

CC      ?= gcc
CFLAGS  ?= -O3 -Wall
LDLIBS  ?= -lm

SRC     := src/bilateral_common.c
HDR     := src/bilateral_common.h

# MinGW produz .exe; no Linux, nao.
ifeq ($(OS),Windows_NT)
EXE := .exe
else
EXE :=
endif

SEQ := bilateral_seq$(EXE)
OMP := bilateral_omp$(EXE)
RUI := ruido$(EXE)

.PHONY: all clean
all: $(SEQ) $(OMP) $(RUI)

$(SEQ): src/bilateral_seq.c $(SRC) $(HDR)
	$(CC) $(CFLAGS) -o $@ src/bilateral_seq.c $(SRC) $(LDLIBS)

$(OMP): src/bilateral_omp.c $(SRC) $(HDR)
	$(CC) $(CFLAGS) -fopenmp -o $@ src/bilateral_omp.c $(SRC) $(LDLIBS)

$(RUI): src/ruido.c $(SRC) $(HDR)
	$(CC) $(CFLAGS) -o $@ src/ruido.c $(SRC) $(LDLIBS)

clean:
	rm -f bilateral_seq bilateral_seq.exe bilateral_omp bilateral_omp.exe ruido ruido.exe
