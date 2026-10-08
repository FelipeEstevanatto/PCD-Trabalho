# Filtro bilateral (sequencial + OpenMP)

Dois programas, mesma definição do filtro:

| Arquivo | Binário | Papel |
| :--- | :--- | :--- |
| `bilateral_seq.c` | `bilateral_seq.exe` | versão **sequencial** (sem OpenMP) |
| `bilateral_omp.c` | `bilateral_omp.exe` | versão **paralela** (`#pragma omp parallel for`) |
| `bilateral_common.c` | (linkado nos dois) | I/O PPM, ruído, PSNR, pixel do filtro |

O protocolo de 10 reps **não** roda um binário de cada vez em lote (isso deixa o CPU esfriar). Use o harness: em cada volta chama `seq`, depois `1, 2, 4, 8, 16, 32` threads **em sequência imediata**.

## Compilar

```bash
gcc -O3 -o bilateral_seq.exe bilateral_seq.c bilateral_common.c -lm
gcc -O3 -fopenmp -o bilateral_omp.exe bilateral_omp.c bilateral_common.c -lm
```

## Protocolo completo (recomendado)

PowerShell (Windows):

```powershell
.\run_experimento.ps1
.\run_experimento.ps1 -Quick
.\run_experimento.ps1 -Reps 10 -Radius 9 -Image imagens/montanha_4k.ppm -Sigma 25
```

Bash (lab / Linux):

```bash
./run_experimento.sh
QUICK=1 ./run_experimento.sh
```

Saída: tabela (média, desvio, speedup, eficiência, proxy E) e `imagens/tempos_10reps.csv`.

## Rodar um binário isolado

Útil para um teste pontual. Cada um imprime `tempo_s=...` no stdout.

```bash
./bilateral_seq.exe 9 imagens/montanha_4k.ppm 25 --save
./bilateral_omp.exe 8 9 imagens/montanha_4k.ppm 25 --save
```

| Argumento | Exemplo | Significado |
| ---: | ---: | :--- |
| (omp) 1 | `8` | número de **threads** OpenMP |
| seguinte | `9` | **raio** do kernel (aqui 19×19) |
| seguinte | `imagens/montanha_4k.ppm` | arquivo de entrada |
| seguinte | `25` | **σ do ruído Gaussiano** (AWGN). `0` = sem ruído |
| flag | `--save` | grava PPMs em `imagens/` |

## Interpretar

| Coluna | Significado |
| :--- | :--- |
| `media(s)` | Tempo do filtro (menor = mais rápido) |
| `speedup` | \(T_\text{seq} / T_p\) (maior = melhor) |
| `eficiencia` | speedup / threads (1,0 = escalou linear) |
| `proxy E` | tempo × threads (indicador **indireto** de energia; menor = melhor) |

Neste CPU (8 núcleos / 16 lógicos), o **menor tempo** tende a 16–32 threads; a **melhor eficiência** tende a **8**. Se o paralelo bater com o sequencial (`erro máximo ≈ 0` ao usar `--save` nos dois) e o PSNR subir, o filtro está correto e o ruído saiu.

Análise em `resumo_experimento.md`.

## Origem do código

Implementação **própria** da fórmula de Tomasi e Manduchi (1998), paralela com OpenMP (Chapman et al., 2007). **Não** é cópia do FFmpeg nem do OpenCV (o `bilateral` do FFmpeg é o algoritmo recursivo de Yang, 2012).

- Filtro: Tomasi & Manduchi, ICCV 1998
- Ruído Gaussiano: Box & Muller, 1958
- LCG: Press et al., *Numerical Recipes*, 2007
- PSNR / AWGN: Gonzalez & Woods, 2018
- Foto: Wikimedia Commons, CC0 — ver `imagens/FONTE.txt`

Lista completa em `resumo_experimento.md`.
