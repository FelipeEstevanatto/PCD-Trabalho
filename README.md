# Filtro bilateral (OpenMP)

`bilateral_openmp.c` aplica um **filtro bilateral** em uma foto 4K RGB (com ruído Gaussiano) e mede tempo da versão **sequencial** vs **1, 2, 4, 8, 16 e 32 threads**.

## Compilar e rodar

```bash
gcc -O3 -fopenmp -o bilateral_openmp.exe bilateral_openmp.c
./bilateral_openmp.exe
```

Sem argumentos: 1 aquecimento + **10 repetições**. Teste rápido:

```bash
./bilateral_openmp.exe 8
./bilateral_openmp.exe 8 9 imagens/montanha_4k.ppm 25
```

| Posição | Exemplo | Significado |
| ---: | ---: | :--- |
| 1 | `8` | número de **threads** OpenMP |
| 2 | `9` | **raio** do kernel (aqui 19×19) |
| 3 | `imagens/montanha_4k.ppm` | arquivo de entrada |
| 4 | `25` | **σ do ruído Gaussiano** (AWGN). `0` = sem ruído |

## Interpretar

| Coluna | Significado |
| :--- | :--- |
| `media(s)` | Tempo do filtro (menor = mais rápido) |
| `speedup` | \(T_\text{seq} / T_p\) (maior = melhor) |
| `eficiencia` | speedup / threads (1,0 = escalou linear) |
| `proxy E` | tempo × threads (indicador **indireto** de energia; menor = melhor) |

Neste CPU (8 núcleos / 16 lógicos), o **menor tempo** tende a 16–32 threads; a **melhor eficiência** tende a **8**. Se o paralelo bater com o sequencial (`erro máximo = 0`) e o PSNR subir, o filtro está correto e o ruído saiu.

Saídas em `imagens/` (`entrada.ppm`, `saida_paralela.ppm`, `tempos_10reps.csv`). Análise em `resumo_experimento.md`.

## Origem do código

Implementação **própria** da fórmula de Tomasi e Manduchi (1998), paralela com OpenMP (Chapman et al., 2007). **Não** é cópia do FFmpeg nem do OpenCV (o `bilateral` do FFmpeg é o algoritmo recursivo de Yang, 2012).

- Filtro: Tomasi & Manduchi, ICCV 1998
- Ruído Gaussiano: Box & Muller, 1958
- LCG: Press et al., *Numerical Recipes*, 2007
- PSNR / AWGN: Gonzalez & Woods, 2018
- Foto: Wikimedia Commons, CC0 — ver `imagens/FONTE.txt`

Lista completa em `resumo_experimento.md`.
