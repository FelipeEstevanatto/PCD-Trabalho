/*
 * Filtro bilateral PARALELO com OpenMP.
 *
 * Implementacao propria da definicao de Tomasi e Manduchi (ICCV 1998,
 * p. 839-846, DOI 10.1109/ICCV.1998.710815).
 * NAO e copia de FFmpeg, OpenCV nem de codigo de artigo.
 * O filtro bilateral do FFmpeg e o recursivo de Yang, ECCV 2012,
 * p. 399-413, DOI 10.1007/978-3-642-33718-5_29 (aproximacao O(n)).
 *
 * OpenMP: Chapman, Jost & van der Pas, Using OpenMP, MIT Press, 2007.
 *
 * Compilar:
 *   gcc -O3 -fopenmp -o bilateral_omp.exe bilateral_omp.c bilateral_common.c -lm
 *
 * Uso:
 *   bilateral_omp.exe <nthreads> [raio] [imagem.ppm|pgm] [sigma_ruido] [--save]
 *   bilateral_omp.exe <nthreads> [raio] --sintetica <W> <H> [sigma_ruido] [--save]
 *
 * Imprime uma linha parseavel: tempo_s=<segundos>
 * O protocolo completo (seq + omp intercalados) fica em run_experimento.ps1/.sh
 */

#include "bilateral_common.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <omp.h>

static void bilateral_filter_omp(const Image *src, Image *dst, int radius,
                                float sigma_s, float sigma_r, int threads) {
    const int w = src->width;
    const int h = src->height;
    const int ch = src->channels;
    const int n = w * h;
    const float inv_2r2 = 1.0f / (2.0f * sigma_r * sigma_r);
    int k = 0;
    float *spatial = make_spatial_kernel(radius, sigma_s, &k);
    
    for (int c = 0; c < ch; c++) {
        const float *in = src->data + (size_t)c * (size_t)n;
        float *out = dst->data + (size_t)c * (size_t)n;
        for (int y = 0; y < h; y++) {
            for (int x = 0; x < w; x++) {
                out[y * w + x] = bilateral_pixel(in, w, h, x, y, radius, k, spatial, inv_2r2);
            }
        }
    }
    free(spatial);
}

static void usage(const char *argv0) {
    fprintf(stderr, "Uso:\n");
    fprintf(stderr, "  %s <nthreads> [raio] [imagem.ppm|pgm] [sigma_ruido] [--save]\n", argv0);
    fprintf(stderr, "  %s <nthreads> [raio] --sintetica <W> <H> [sigma_ruido] [--save]\n", argv0);
}

int main(int argc, char **argv) {
    int nthreads = 0;
    int radius = 9;
    int synth_w = 0, synth_h = 0;
    const char *image_path = DEFAULT_IMAGE;
    float noise_sigma = 25.0f;
    int argi = 1;

    if (argc < 2 || !is_number(argv[1])) {
        usage(argv[0]);
        return 1;
    }
    nthreads = atoi(argv[1]);
    argi = 2;
    if (nthreads < 1) {
        fprintf(stderr, "nthreads deve ser >= 1\n");
        return 1;
    }

    if (argc > argi && is_number(argv[argi])) {
        radius = atoi(argv[argi]);
        argi++;
    }
    if (argc > argi) {
        if (strcmp(argv[argi], "--sintetica") == 0) {
            if (argc < argi + 3) {
                usage(argv[0]);
                return 1;
            }
            synth_w = atoi(argv[argi + 1]);
            synth_h = atoi(argv[argi + 2]);
            image_path = NULL;
            argi += 3;
        } else if (strcmp(argv[argi], "--save") != 0) {
            image_path = argv[argi];
            argi++;
        }
    }
    if (argc > argi && is_number(argv[argi])) {
        noise_sigma = (float)atof(argv[argi]);
        argi++;
    }

    if (radius < 1) {
        usage(argv[0]);
        return 1;
    }

    const float sigma_s = (float)radius / 2.0f;
    const float sigma_r = (noise_sigma > 0.0f) ? (2.0f * noise_sigma) : 30.0f;

    Image src;
    if (image_path) {
        src = load_pnm(image_path);
    } else {
        if (synth_w < 16 || synth_h < 16) {
            fprintf(stderr, "Tamanho sintetico invalido.\n");
            return 1;
        }
        src = image_alloc(synth_w, synth_h, 1);
        generate_synthetic_image(&src);
    }

    Image ruido = load_pnm("imagens/ruido.ppm");

    const int width = ruido.width;
    const int height = ruido.height;
    const int ch = ruido.channels;
    const int k = 2 * radius + 1;

    fprintf(stderr, "bilateral SEQ  |  %dx%d x %d  |  raio %d  |  threads=%d  |  CPUs=%d\n",
            width, height, ch, radius, 0, 8);
    fprintf(stderr, "entrada: %s  |  sigma_s=%.2f sigma_r=%.2f AWGN=%.1f\n",
            image_path ? image_path : "(sintetica)", sigma_s, sigma_r, noise_sigma);

    Image out = image_alloc(width, height, ch);
    const double t0 = wall_time();
    bilateral_filter_omp(&ruido, &out, radius, sigma_s, sigma_r, nthreads);
    const double elapsed = wall_time() - t0;

    printf("tempo_s=%.6f\n", elapsed);
    fflush(stdout);

    fprintf(stderr, "PSNR bilateral vs original = %.2f dB\n", psnr(&src, &out));
    fprintf(stderr, "PSNR gaussiano vs original = %.2f dB\n", psnr(&src, &ruido));

    image_free(&src);
    image_free(&ruido);
    image_free(&out);
    return 0;
}
