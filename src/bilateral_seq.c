/*
 * Filtro bilateral (Tomasi & Manduchi, ICCV 1998, p. 839-846,
 * DOI 10.1109/ICCV.1998.710815) -- VERSAO SEQUENCIAL de referencia.
 *
 * Este arquivo e QUASE IDENTICO a src/bilateral_omp.c. A unica diferenca
 * funcional esta no interior de bilateral_filter(), marcada com "DIFERENCA"
 * mais abaixo (a diretiva OpenMP que so existe no arquivo paralelo).
 * Todo o resto (CLI, leitura de imagens, calculo do pixel, PSNR) e igual.
 * O codigo comum aos dois fica em bilateral_common.{c,h}.
 *
 * Implementacao propria da definicao de Tomasi e Manduchi; nao e copia de
 * FFmpeg, OpenCV nem de codigo de artigo.
 *
 * Compilar:
 *   gcc -O3 -o bilateral_seq.exe src/bilateral_seq.c src/bilateral_common.c -lm
 *
 * Uso:
 *   bilateral_seq <threads> <raio> <imagem.ppm|pgm> <sigma> [--save]
 * (<threads> existe so para manter a CLI identica a do paralelo; e ignorado.)
 * A imagem ruidosa e procurada em <imagem sem extensao>_ruido.ppm.
 * Imprime uma linha parseavel: tempo_s=<segundos>
 */

#include "bilateral_common.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MODE "SEQ" /* troca para "OMP" em bilateral_omp.c */

static void bilateral_filter(const Image *src, Image *dst, int radius,
                             float sigma_s, float sigma_r, int threads) {
    const int w = src->width;
    const int h = src->height;
    const int ch = src->channels;
    const int n = w * h;
    const float inv_2r2 = 1.0f / (2.0f * sigma_r * sigma_r);
    int k = 0;
    float *spatial = make_spatial_kernel(radius, sigma_s, &k);

    (void)threads; /* a versao sequencial ignora o numero de threads */

    for (int c = 0; c < ch; c++) {
        const float *in = src->data + (size_t)c * (size_t)n;
        float *out = dst->data + (size_t)c * (size_t)n;

        /* -----------------------------------------------------------------
         * DIFERENCA: aqui o laco de linhas roda SERIAL. Em bilateral_omp.c,
         * imediatamente antes deste mesmo "for (int y = 0; ...)", existe a
         * diretiva:
         *     #pragma omp parallel for num_threads(threads) schedule(static)
         * que paraleliza o laco (Chapman, Jost & van der Pas, 2007).
         * ----------------------------------------------------------------- */
        for (int y = 0; y < h; y++) {
            for (int x = 0; x < w; x++) {
                out[y * w + x] = bilateral_pixel(in, w, h, x, y, radius, k, spatial, inv_2r2);
            }
        }
    }
    free(spatial);
}

static void usage(const char *argv0) {
    fprintf(stderr,
            "Uso: %s <threads> <raio> <imagem.ppm|pgm> <sigma_ruido> [--save]\n"
            "  <threads>  numero de threads OpenMP (ignorado na versao sequencial)\n"
            "  <raio>     raio do kernel (janela (2r+1)x(2r+1))\n"
            "  <sigma>    desvio do ruido AWGN (vira tambem sigma_r = 2*sigma)\n"
            "  --save     grava a saida em <imagem>_filtrada_<seq|omp>.ppm\n"
            "A imagem ruidosa e procurada em <imagem sem extensao>_ruido.ppm.\n",
            argv0);
}

int main(int argc, char **argv) {
    if (argc < 5) {
        usage(argv[0]);
        return 1;
    }
    if (!is_number(argv[1]) || !is_number(argv[2]) || !is_number(argv[4])) {
        usage(argv[0]);
        return 1;
    }

    const int threads = atoi(argv[1]);
    const int radius = atoi(argv[2]);
    const char *image_path = argv[3];
    const float noise_sigma = (float)atof(argv[4]);
    int save = 0;
    for (int i = 5; i < argc; i++) {
        if (strcmp(argv[i], "--save") == 0) {
            save = 1;
        } else {
            fprintf(stderr, "Argumento desconhecido: %s\n", argv[i]);
            return 1;
        }
    }
    if (threads < 1 || radius < 1) {
        usage(argv[0]);
        return 1;
    }

    const float sigma_s = (float)radius / 2.0f;
    const float sigma_r = (noise_sigma > 0.0f) ? (2.0f * noise_sigma) : 30.0f;

    Image src = load_pnm(image_path);

    char noisy_path[1024];
    derive_noisy_path(noisy_path, sizeof(noisy_path), image_path);
    Image ruido = load_pnm(noisy_path);

    const int width = ruido.width;
    const int height = ruido.height;
    const int ch = ruido.channels;

    fprintf(stderr, "bilateral %s  |  %dx%d x %d  |  raio %d  |  threads=%d  |  CPUs=%d\n",
            MODE, width, height, ch, radius, threads, cpu_count());
    fprintf(stderr, "entrada: %s  |  sigma_s=%.2f sigma_r=%.2f AWGN=%.1f\n",
            image_path, sigma_s, sigma_r, noise_sigma);

    Image out = image_alloc(width, height, ch);
    const double t0 = wall_time();
    bilateral_filter(&ruido, &out, radius, sigma_s, sigma_r, threads);
    const double elapsed = wall_time() - t0;

    printf("tempo_s=%.6f\n", elapsed);
    fflush(stdout);

    fprintf(stderr, "PSNR bilateral vs original = %.2f dB\n", psnr(&src, &out));
    fprintf(stderr, "PSNR gaussiano vs original = %.2f dB\n", psnr(&src, &ruido));

    if (save) {
        const char *dot = strrchr(image_path, '.');
        const size_t base_len = dot ? (size_t)(dot - image_path) : strlen(image_path);
        char out_path[1024];
        snprintf(out_path, sizeof(out_path), "%.*s_filtrada_%s.ppm",
                 (int)base_len, image_path, MODE);
        save_pnm(out_path, &out);
        fprintf(stderr, "gravado: %s\n", out_path);
    }

    image_free(&src);
    image_free(&ruido);
    image_free(&out);
    return 0;
}
