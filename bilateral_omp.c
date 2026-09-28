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
        /* Chapman, Jost & van der Pas, Using OpenMP, MIT Press, 2007 */
        #pragma omp parallel for num_threads(threads) schedule(static)
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
    int do_save = 0;
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
    if (argc > argi && strcmp(argv[argi], "--save") == 0) {
        do_save = 1;
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

    Image clean = image_clone(&src);
    add_gaussian_noise(&src, noise_sigma, 20260920u);

    const int width = src.width;
    const int height = src.height;
    const int ch = src.channels;
    const int k = 2 * radius + 1;

    fprintf(stderr, "bilateral OMP  |  %dx%d x %d  |  raio %d  |  threads=%d  |  CPUs=%d\n",
            width, height, ch, radius, nthreads, omp_get_num_procs());
    fprintf(stderr, "entrada: %s  |  sigma_s=%.2f sigma_r=%.2f AWGN=%.1f\n",
            image_path ? image_path : "(sintetica)", sigma_s, sigma_r, noise_sigma);

    Image out = image_alloc(width, height, ch);
    const double t0 = wall_time();
    bilateral_filter_omp(&src, &out, radius, sigma_s, sigma_r, nthreads);
    const double elapsed = wall_time() - t0;

    printf("tempo_s=%.6f\n", elapsed);
    fflush(stdout);

    if (do_save) {
        ensure_out_dir();
        char p_par[256], p_seq[256];
        path_join(p_par, sizeof(p_par), "saida_paralela.ppm");
        path_join(p_seq, sizeof(p_seq), "saida_sequencial.ppm");
        save_pnm(p_par, &out);
        fprintf(stderr, "gravado: %s\n", p_par);

        FILE *fseq = fopen(p_seq, "rb");
        if (fseq) {
            fclose(fseq);
            /* Compara apos round-trip PPM (u8), nao float vs arquivo. */
            Image seq_disk = load_pnm(p_seq);
            Image par_disk = load_pnm(p_par);
            if (seq_disk.width == par_disk.width &&
                seq_disk.height == par_disk.height &&
                seq_disk.channels == par_disk.channels) {
                const double err = max_abs_diff(&seq_disk, &par_disk);
                fprintf(stderr, "erro maximo |par - seq| = %.6e\n", err);
                fprintf(stderr, "Validacao: %s\n", err < 1e-3 ? "OK" : "FALHOU");
            }
            image_free(&seq_disk);
            image_free(&par_disk);
        }
        if (noise_sigma > 0.0f) {
            fprintf(stderr, "PSNR bilateral vs original = %.2f dB\n", psnr(&clean, &out));
        }
    }

    image_free(&src);
    image_free(&clean);
    image_free(&out);
    return 0;
}
