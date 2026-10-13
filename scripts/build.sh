#!/usr/bin/env bash
# Compila os binarios do filtro bilateral (Linux / macOS / MSYS2).
# Equivalente ao Makefile, para quem nao tem "make" no PATH.
set -euo pipefail
cd "$(dirname "$0")/.."

gcc -O3 -Wall -o bilateral_seq.exe src/bilateral_seq.c src/bilateral_common.c -lm
gcc -O3 -Wall -fopenmp -o bilateral_omp.exe src/bilateral_omp.c src/bilateral_common.c -lm
gcc -O3 -Wall -o ruido.exe src/ruido.c src/bilateral_common.c -lm

echo "OK: bilateral_seq.exe  bilateral_omp.exe  ruido.exe"
