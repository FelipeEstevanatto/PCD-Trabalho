#!/usr/bin/env bash
# Roda seq e omp intercalados (sem sleep) e grava o CSV.
# Uso: ./run_experimento.sh
#      REPS=1 ./run_experimento.sh

set -euo pipefail
cd "$(dirname "$0")"

REPS="${REPS:-10}"
RADIUS="${RADIUS:-9}"
IMAGE="${IMAGE:-imagens/montanha_4k.ppm}"
SIGMA="${SIGMA:-25}"

SEQ=./bilateral_seq
OMP=./bilateral_omp
[[ -x ./bilateral_seq.exe ]] && SEQ=./bilateral_seq.exe
[[ -x ./bilateral_omp.exe ]] && OMP=./bilateral_omp.exe

# stdout do binario: tempo_s=...
t() { "$@" 2>/dev/null | sed -n 's/^tempo_s=//p'; }

echo "Aquecimento..."
t "$SEQ" "$RADIUS" "$IMAGE" "$SIGMA" --save >/dev/null
t "$OMP" 8 "$RADIUS" "$IMAGE" "$SIGMA" --save >/dev/null

CSV=imagens/tempos_10reps.csv
echo "rep,config,threads,tempo_s" >"$CSV"

for ((r = 1; r <= REPS; r++)); do
  echo "rep $r/$REPS  seq"
  printf "%d,seq,0,%s\n" "$r" "$(t "$SEQ" "$RADIUS" "$IMAGE" "$SIGMA")" >>"$CSV"
  for th in 1 2 4 8 16 32; do
    echo "rep $r/$REPS  $th threads"
    printf "%d,omp,%d,%s\n" "$r" "$th" "$(t "$OMP" "$th" "$RADIUS" "$IMAGE" "$SIGMA")" >>"$CSV"
  done
done

t "$SEQ" "$RADIUS" "$IMAGE" "$SIGMA" --save >/dev/null
t "$OMP" 8 "$RADIUS" "$IMAGE" "$SIGMA" --save >/dev/null

echo "CSV: $CSV"
echo "Saidas: imagens/saida_sequencial.ppm  imagens/saida_paralela.ppm"
