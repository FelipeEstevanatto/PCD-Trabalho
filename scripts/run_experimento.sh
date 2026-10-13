#!/usr/bin/env bash
# Reproduz o protocolo experimental do artigo (Sec. 4.4) em Linux/macOS/MSYS2.
# Ver scripts/run_experimento.ps1 para a descricao completa.
#
# Uso:
#   ./scripts/run_experimento.sh
#   REPS=2 RADII="3" THREADS="4 8" NOPCM=1 ./scripts/run_experimento.sh   # teste rapido
set -euo pipefail
cd "$(dirname "$0")/.."

REPS="${REPS:-10}"
RADII="${RADII:-3 5 7}"
THREADS="${THREADS:-1 2 4 8 16}"
IMAGES="${IMAGES:-dados/entrada/montanha_4k.ppm dados/entrada/montanha2_4k.ppm}"
SIGMA="${SIGMA:-25}"
NOPCM="${NOPCM:-0}"

SEQ=./bilateral_seq.exe
OMP=./bilateral_omp.exe
RUI=./ruido.exe
PCM=./tools/pcm.exe

if [ ! -x "$SEQ" ] || [ ! -x "$OMP" ] || [ ! -x "$RUI" ]; then
    echo "Binarios ausentes; compilando..."; ./scripts/build.sh
fi

usepcm=1
if [ "$NOPCM" = "1" ] || [ ! -x "$PCM" ]; then usepcm=0; echo "AVISO: sem PCM; apenas tempos."; fi

run_once() { # <exe> <threads> <raio> <imagem>
    if [ "$usepcm" = "1" ]; then "$PCM" -- "$1" "$2" "$3" "$4" "$SIGMA" 2>&1
    else "$1" "$2" "$3" "$4" "$SIGMA" 2>&1; fi
}

for image in $IMAGES; do
    [ -f "$image" ] || { echo "imagem nao encontrada: $image" >&2; exit 1; }
    noisy="${image%.*}_ruido.ppm"
    if [ ! -f "$noisy" ]; then echo "Gerando $noisy"; "$RUI" "$image" "$noisy" "$SIGMA" 20260920 >/dev/null 2>&1; fi

    base="$(basename "$image")"; base="${base%.*}"
    outdir="dados/resultados/$base"; mkdir -p "$outdir"
    echo ""; echo "Imagem: $image"

    for radius in $RADII; do
        for cfg in seq $THREADS; do
            if [ "$cfg" = "seq" ]; then exe="$SEQ"; th=1; else exe="$OMP"; th="$cfg"; fi
            log="$outdir/${base}_K${radius}_${cfg}.txt"
            echo "# protocolo: imagem=$image raio=$radius config=$cfg sigma=$SIGMA reps=$REPS" >"$log"
            echo "  raio=$radius  cfg=$cfg"

            echo "    aquecimento (descartado)"
            run_once "$exe" "$th" "$radius" "$image" >/dev/null

            for ((r = 1; r <= REPS; r++)); do
                echo "    rep $r/$REPS"
                if [ "$usepcm" = "1" ]; then
                    printf '\n.\\pcm.exe -- %s %s %s %s %s\n' "$exe" "$th" "$radius" "$image" "$SIGMA" >>"$log"
                else
                    printf '\n%s %s %s %s %s\n' "$exe" "$th" "$radius" "$image" "$SIGMA" >>"$log"
                fi
                run_once "$exe" "$th" "$radius" "$image" >>"$log"
            done
        done
    done
done

echo ""
echo "Logs em dados/resultados/. Consolide com: python scripts/analisar.py"
