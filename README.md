# Filtro bilateral com OpenMP — desempenho e eficiência energética

Código, scripts, dados e registros do artigo:

> **Impacto do Número de Threads no Desempenho e na Eficiência Energética de um
> Filtro Bilateral com OpenMP**
> Bianca Valenciani, Felipe Lima Estevanatto, Felipe Soares Calderaro,
> Isabella Maria Ribeiro, Vitor Guilherme Alves — UNIFESP/ICT
> Programação Concorrente e Distribuída (Prof. Álvaro Fazenda), 2026.

**Pergunta.** Qual número de threads oferece a melhor relação entre tempo de
execução e eficiência energética ao aplicar um filtro bilateral em imagens de
alta resolução com OpenMP?

**Hipótese (H1).** O menor tempo ocorre com o número de threads lógicas, mas o
menor consumo de energia ocorre antes, com o número de núcleos físicos.
**Resultado:** H1 foi **refutada** — com 8 threads (todas as lógicas) o filtro
foi de 3,4 a 3,9 vezes mais rápido e gastou de 38% a 44% menos energia que a
versão sequencial.

## Estrutura

```text
src/
  bilateral_common.{c,h}   I/O PNM, ruido, PSNR, kernel, pixel do filtro
  bilateral_seq.c          filtro sequencial de referencia
  bilateral_omp.c          filtro paralelo; difere do sequencial SO na diretiva
                           OpenMP (marcada com "DIFERENCA" no arquivo)
  ruido.c                  gerador de ruido AWGN (programa separado)
scripts/
  build.ps1 / build.sh     compilacao
  run_experimento.ps1/.sh  protocolo do artigo (raios 3/5/7, threads 1..16, 10 reps)
  analisar.py              consolida os logs em CSV/tabelas (e figuras)
  montar_comparacao.py     figura original | ruidosa | filtrada
  baixar_imagem.py         rebaixa a foto CC0 de origem
dados/
  entrada/                 imagens do artigo (limpas e ruidosas) + FONTE.txt
  resultados/              logs brutos do artigo + CSVs consolidados
  legado/                  insumos antigos nao usados no artigo
tools/pcm.exe              Intel PCM (leitura de energia RAPL)
docs/piloto_amd.md         experimento-piloto anterior (AMD Ryzen, sem RAPL)
Makefile
```

## Compilar

```bash
make                 # ou:  scripts/build.sh   /   scripts\build.ps1
gcc -O3            -o bilateral_seq.exe src/bilateral_seq.c src/bilateral_common.c -lm
gcc -O3 -fopenmp   -o bilateral_omp.exe src/bilateral_omp.c src/bilateral_common.c -lm
gcc -O3            -o ruido.exe         src/ruido.c         src/bilateral_common.c -lm
```

Sequencial e paralelo são **dois fontes quase idênticos** (`src/bilateral_seq.c`
e `src/bilateral_omp.c`): a única diferença funcional é a diretiva
`#pragma omp parallel for`, marcada com `DIFERENCA` dentro de `bilateral_omp.c`.
Ambos compartilham `bilateral_common.c` (I/O, ruído, PSNR, pixel do filtro).

## Reproduzir o experimento

```powershell
# Windows (protocolo completo do artigo)
scripts\run_experimento.ps1
# teste rápido (1 imagem, 1 raio, 2 threads, sem PCM)
scripts\run_experimento.ps1 -Reps 2 -Radii 3 -Threads 4,8 -NoPcm

python scripts/analisar.py            # CSV + tabelas
python scripts/analisar.py --figuras  # tambem figuras (matplotlib)
```

```bash
# Linux / macOS / MSYS2
./scripts/run_experimento.sh
python scripts/analisar.py
```

O runner roda, para cada imagem, cada raio `{3,5,7}` e cada configuração
`{seq, 1, 2, 4, 8, 16}`, um aquecimento (descartado) e então 10 repetições,
cada uma sob o Intel PCM (`tools/pcm.exe -- <programa>`) quando disponível. Os
logs saem em `dados/resultados/<imagem>/`.

### Uso do filtro isolado

```bash
# <threads> <raio> <imagem> <sigma> [--save]
./bilateral_omp.exe 8 3 dados/entrada/montanha_4k.ppm 25
./bilateral_seq.exe 1 3 dados/entrada/montanha_4k.ppm 25 --save
```

| Argumento | Exemplo | Significado |
| --- | --- | --- |
| threads | `8` | nº de threads OpenMP (ignorado na versão sequencial) |
| raio | `3` | raio do kernel (janela `(2r+1)²`) |
| imagem | `dados/entrada/montanha_4k.ppm` | imagem limpa (PPM/PGM) |
| sigma | `25` | desvio do ruído AWGN; vira `σr = 2·sigma`, `σs = raio/2` |
| `--save` | — | grava a saída em `<imagem>_filtrada_<seq|omp>.ppm` |

A imagem **ruidosa** é procurada em `<imagem sem extensão>_ruido.ppm`
(gerada antes por `ruido.exe`, conforme a Seção 4.1 do artigo). O programa
imprime `tempo_s=...` no stdout e o PSNR no stderr.

## Dados e proveniência

- Imagem principal: `dados/entrada/montanha_4k.ppm` (4000×2250, RGB).
- Segunda imagem: `dados/entrada/montanha2_4k.ppm` (2048×1365, RGB) — o sufixo
  `4k` é do nome original; a resolução real é 2048×1365.
- Imagens ruidosas do artigo: `*_ruido.ppm` (AWGN σ=25, semente fixa).
  Conferência: PSNR contra a imagem limpa = **20,41 dB** (principal) e
  **20,63 dB** (segunda) — exatamente os valores do artigo.
- Ambiente dos números do artigo: Intel Core i5-1035G1 (4 núcleos / 8 threads),
  Windows 11, GCC 16.2.0, energia lida do RAPL pelo Intel PCM. Os registros
  brutos (10 execuções por configuração) estão em `dados/resultados/`.

## Notas

- A energia é medida pelo PCM envolvendo o processo inteiro (inclui leitura das
  imagens e cálculo do PSNR); o texto justifica a parte fixa no artigo.
- A implementação reproduz a **definição exata** do filtro (Tomasi & Manduchi,
  1998) e não foi otimizada para tempo absoluto — os tempos não são comparáveis
  a FFmpeg/OpenCV, que usam outros algoritmos. Ver `docs/piloto_amd.md`.

## Licença e créditos

- Código e dados: MIT (ver `LICENSE`).
- `tools/pcm.exe`: Intel Performance Counter Monitor (licença do projeto Intel).
- Foto: www.Pixel.la via Wikimedia Commons, CC0 — `dados/entrada/FONTE.txt`.

Se usar este material, cite o artigo e o repositório (ver `CITATION.cff`).
