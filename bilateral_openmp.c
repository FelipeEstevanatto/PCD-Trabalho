/*
 * Filtro bilateral em cor, sequencial e OpenMP.
 *
 * Implementacao propria da definicao de Tomasi & Manduchi (1998), para a
 * disciplina de PCD — NAO e copia de FFmpeg, OpenCV nem de codigo de artigo.
 * O FFmpeg usa outro algoritmo (bilateral recursivo, Yang 2012).
 *
 * Fontes da DEFINICAO (nao do texto-fonte deste arquivo):
 *   Tomasi & Manduchi, ICCV 1998          — pesos espacial * faixa, exp()
 *   Chapman, Jost & van der Pas, 2007     — OpenMP parallel for
 *   Gonzalez & Woods, 2018                — PSNR, AWGN como teste de denoising
 *   Box & Muller, 1958                    — geracao de ruido Gaussiano
 *   Press et al., Numerical Recipes, 2007 — LCG (constantes 1664525, 1013904223)
 *
 * Compilar:
 *   gcc -O3 -fopenmp -o bilateral_openmp.exe bilateral_openmp.c
 *
 * Uso:
 *   bilateral_openmp.exe
 *       protocolo: 1 aquecimento + 10 repeticoes (seq, 1, 2, 4, 8, 16, 32)
 *   bilateral_openmp.exe <nthreads> [raio] [imagem.ppm|pgm] [sigma_ruido]
 *   bilateral_openmp.exe <nthreads> [raio] --sintetica <W> <H> [sigma_ruido]
 *
 * sigma_ruido = 0 desliga o ruido. Padrao: 25.
 * Saidas em imagens/.
 */

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <direct.h>
#else
#include <sys/stat.h>
#endif
#include <omp.h>

#define N_REPS 10
#define OUT_DIR "imagens"

#ifndef DEFAULT_IMAGE
#define DEFAULT_IMAGE "imagens/montanha_4k.ppm"
#endif

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

typedef struct {
    int width;
    int height;
    int channels; /* 1 = cinza, 3 = RGB planar */
    float *data;  /* canal c em data + c * width * height */
} Image;

static size_t image_n(const Image *img) {
    return (size_t)img->width * (size_t)img->height;
}

static Image image_alloc(int width, int height, int channels) {
    Image img;
    img.width = width;
    img.height = height;
    img.channels = channels;
    img.data = (float *)malloc(image_n(&img) * (size_t)channels * sizeof(float));
    if (!img.data) {
        fprintf(stderr, "Falha ao alocar imagem %dx%d x %d\n", width, height, channels);
        exit(1);
    }
    return img;
}

static void image_free(Image *img) {
    free(img->data);
    img->data = NULL;
}

static Image image_clone(const Image *src) {
    Image dst = image_alloc(src->width, src->height, src->channels);
    memcpy(dst.data, src->data, image_n(src) * (size_t)src->channels * sizeof(float));
    return dst;
}

static inline float clampf(float x, float lo, float hi) {
    if (x < lo) return lo;
    if (x > hi) return hi;
    return x;
}

static inline unsigned char to_u8(float x) {
    return (unsigned char)(clampf(x, 0.0f, 255.0f) + 0.5f);
}

static void die(const char *msg) {
    fprintf(stderr, "%s\n", msg);
    exit(1);
}

static unsigned lcg_next(unsigned *state) {
    /* Numerical Recipes (Press et al.): X_{n+1} = 1664525 X_n + 1013904223 */
    *state = (*state * 1664525u + 1013904223u);
    return *state;
}

static float rand_u01(unsigned *state) {
    return ((lcg_next(state) >> 8) + 1) / 16777216.0f;
}

static float randn(unsigned *state) {
    /* Box-Muller (1958): dois uniformes -> um normal N(0,1). */
    float u1 = rand_u01(state);
    const float u2 = rand_u01(state);
    return sqrtf(-2.0f * logf(u1)) * cosf((float)(2.0 * M_PI) * u2);
}

static int skip_ws_comments(FILE *f) {
    int c;
    while ((c = fgetc(f)) != EOF) {
        if (c == '#') {
            while ((c = fgetc(f)) != EOF && c != '\n') {
            }
            continue;
        }
        if (!isspace(c)) {
            ungetc(c, f);
            return 0;
        }
    }
    return -1;
}

static Image load_pnm(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "Nao foi possivel abrir %s\n", path);
        exit(1);
    }

    char magic[3] = {0};
    if (fread(magic, 1, 2, f) != 2) {
        fprintf(stderr, "Arquivo PNM invalido: %s\n", path);
        exit(1);
    }

    int width = 0, height = 0, maxval = 0;
    if (skip_ws_comments(f) != 0 || fscanf(f, "%d", &width) != 1 ||
        skip_ws_comments(f) != 0 || fscanf(f, "%d", &height) != 1 ||
        skip_ws_comments(f) != 0 || fscanf(f, "%d", &maxval) != 1) {
        fprintf(stderr, "Cabecalho PNM invalido: %s\n", path);
        exit(1);
    }
    if (width < 16 || height < 16 || maxval != 255) {
        fprintf(stderr, "PNM nao suportado (%dx%d, maxval=%d). Use maxval 255.\n",
                width, height, maxval);
        exit(1);
    }
    fgetc(f);

    int channels = 1;
    if (memcmp(magic, "P6", 2) == 0) {
        channels = 3;
    } else if (memcmp(magic, "P5", 2) != 0 && memcmp(magic, "P2", 2) != 0) {
        fprintf(stderr, "Formato %s nao suportado (use PGM P2/P5 ou PPM P6): %s\n",
                magic, path);
        exit(1);
    }

    Image img = image_alloc(width, height, channels);
    const int n = width * height;

    if (memcmp(magic, "P6", 2) == 0) {
        for (int i = 0; i < n; i++) {
            int r = fgetc(f), g = fgetc(f), b = fgetc(f);
            if (b == EOF) {
                fprintf(stderr, "PPM truncado: %s\n", path);
                exit(1);
            }
            img.data[i] = (float)r;
            img.data[n + i] = (float)g;
            img.data[2 * n + i] = (float)b;
        }
    } else if (memcmp(magic, "P5", 2) == 0) {
        for (int i = 0; i < n; i++) {
            int c = fgetc(f);
            if (c == EOF) {
                fprintf(stderr, "PGM truncado: %s\n", path);
                exit(1);
            }
            img.data[i] = (float)c;
        }
    } else {
        for (int i = 0; i < n; i++) {
            int v;
            if (fscanf(f, "%d", &v) != 1) {
                fprintf(stderr, "PGM ASCII truncado: %s\n", path);
                exit(1);
            }
            img.data[i] = (float)v;
        }
    }

    fclose(f);
    return img;
}

static void generate_synthetic_image(Image *img) {
    const int w = img->width;
    const int h = img->height;

    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            float v = 40.0f + 80.0f * ((float)x / (float)(w - 1));
            if (x > w / 6 && x < w / 3 && y > h / 5 && y < 3 * h / 5) v = 210.0f;
            if (x > w / 2 && x < 4 * w / 5 && y > 2 * h / 5 && y < 4 * h / 5) v = 30.0f;
            {
                const int cx = (3 * w) / 4, cy = h / 4, r = w / 10;
                const int dx = x - cx, dy = y - cy;
                if (dx * dx + dy * dy <= r * r) v = 180.0f;
            }
            img->data[y * w + x] = clampf(v, 0.0f, 255.0f);
        }
    }
}

static void add_gaussian_noise(Image *img, float sigma, unsigned seed) {
    if (sigma <= 0.0f) return;
    const int n = (int)image_n(img) * img->channels;
    unsigned rng = seed;
    for (int i = 0; i < n; i++) {
        img->data[i] = clampf(img->data[i] + sigma * randn(&rng), 0.0f, 255.0f);
    }
}

static const char *save_pnm(const char *path, const Image *img) {
    FILE *f = fopen(path, "wb");
    if (!f) {
        fprintf(stderr, "Nao foi possivel gravar %s\n", path);
        return path;
    }
    const int n = (int)image_n(img);
    if (img->channels == 3) {
        fprintf(f, "P6\n%d %d\n255\n", img->width, img->height);
        for (int i = 0; i < n; i++) {
            fputc(to_u8(img->data[i]), f);
            fputc(to_u8(img->data[n + i]), f);
            fputc(to_u8(img->data[2 * n + i]), f);
        }
    } else {
        fprintf(f, "P5\n%d %d\n255\n", img->width, img->height);
        for (int i = 0; i < n; i++) {
            fputc(to_u8(img->data[i]), f);
        }
    }
    fclose(f);
    return path;
}

static double max_abs_diff(const Image *a, const Image *b) {
    const int n = (int)image_n(a) * a->channels;
    double worst = 0.0;
    for (int i = 0; i < n; i++) {
        double d = fabs((double)a->data[i] - (double)b->data[i]);
        if (d > worst) worst = d;
    }
    return worst;
}

static double psnr(const Image *ref, const Image *other) {
    const int n = (int)image_n(ref) * ref->channels;
    double mse = 0.0;
    for (int i = 0; i < n; i++) {
        double d = (double)ref->data[i] - (double)other->data[i];
        mse += d * d;
    }
    mse /= (double)n;
    if (mse <= 1e-12) return 99.0;
    return 10.0 * log10(255.0 * 255.0 / mse);
}

/* Um pixel: I'(p) = sum_q G_s(||p-q||) G_r(|I(p)-I(q)|) I(q) / sum_q G_s G_r
 * (Tomasi & Manduchi, 1998). always_inline: gcc incorpora o corpo nos laços. */
static inline __attribute__((always_inline))
float bilateral_pixel(const float *in, int w, int h, int x, int y,
                                    int radius, int k, const float *spatial, float inv_2r2) {
    const float center = in[y * w + x];
    float sum = 0.0f, wsum = 0.0f;

    for (int dy = -radius; dy <= radius; dy++) {
        const int ny = y + dy;
        if (ny < 0 || ny >= h) continue;
        for (int dx = -radius; dx <= radius; dx++) {
            const int nx = x + dx;
            if (nx < 0 || nx >= w) continue;
            const float val = in[ny * w + nx];
            const float dI = val - center;
            const float ww = spatial[(dy + radius) * k + (dx + radius)] * expf(-dI * dI * inv_2r2);
            sum += ww * val;
            wsum += ww;
        }
    }
    return (wsum > 0.0f) ? (sum / wsum) : center;
}

static float *make_spatial_kernel(int radius, float sigma_s, int *k_out) {
    const int k = 2 * radius + 1;
    const float inv_2s2 = 1.0f / (2.0f * sigma_s * sigma_s);
    float *spatial = (float *)malloc((size_t)k * (size_t)k * sizeof(float));
    if (!spatial) die("Falha ao alocar kernel espacial");
    for (int dy = -radius; dy <= radius; dy++) {
        for (int dx = -radius; dx <= radius; dx++) {
            const float dist2 = (float)(dx * dx + dy * dy);
            spatial[(dy + radius) * k + (dx + radius)] = expf(-dist2 * inv_2s2);
        }
    }
    *k_out = k;
    return spatial;
}

/*
 * threads <= 0: laço sequencial puro (sem runtime OpenMP).
 * threads >  0: o mesmo laço com #pragma omp parallel for.
 * Os dois corpos ficam separados de propósito: OpenMP com 1 thread tem overhead.
 */
static void bilateral_filter(const Image *src, Image *dst, int radius,
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

        if (threads <= 0) {
            for (int y = 0; y < h; y++) {
                for (int x = 0; x < w; x++) {
                    out[y * w + x] = bilateral_pixel(in, w, h, x, y, radius, k, spatial, inv_2r2);
                }
            }
        } else {
            #pragma omp parallel for num_threads(threads) schedule(static)
            for (int y = 0; y < h; y++) {
                for (int x = 0; x < w; x++) {
                    out[y * w + x] = bilateral_pixel(in, w, h, x, y, radius, k, spatial, inv_2r2);
                }
            }
        }
    }
    free(spatial);
}

static double time_filter(const Image *src, Image *dst, int radius,
                          float sigma_s, float sigma_r, int threads) {
    const double t0 = omp_get_wtime();
    bilateral_filter(src, dst, radius, sigma_s, sigma_r, threads);
    return omp_get_wtime() - t0;
}

static void print_validation(double err, int todas_configs) {
    printf("\nErro maximo |paralelo - sequencial| = %.6e\n", err);
    if (err < 1e-3) {
        printf("Validacao: OK (saidas equivalentes%s).\n",
               todas_configs ? " em todas as configs" : "");
    } else {
        printf("Validacao: FALHOU.\n");
    }
}

static double neighborhood_ops(int width, int height, int radius, int channels) {
    const double k = (double)(2 * radius + 1);
    return (double)width * (double)height * (double)channels * k * k;
}

static void print_header(void) {
    printf("%-10s %12s %10s %12s %12s %14s\n",
           "threads", "tempo(s)", "speedup", "eficiencia", "Mpixel/s", "Gnhood/s");
}

static void print_row(const char *label, double t, double t_seq,
                      int width, int height, int radius, int channels) {
    const double speedup = t_seq / t;
    const double pix = (double)width * (double)height;
    const double nhood = neighborhood_ops(width, height, radius, channels);

    if (strcmp(label, "seq") == 0) {
        printf("%-10s %12.4f %10s %12s %12.2f %14.3f\n",
               label, t, "-", "-",
               pix / t / 1e6, nhood / t / 1e9);
        return;
    }
    const int threads = atoi(label);
    const double eff = speedup / (double)threads;
    printf("%-10s %12.4f %10.3f %12.3f %12.2f %14.3f\n",
           label, t, speedup, eff,
           pix / t / 1e6, nhood / t / 1e9);
}

static int is_number(const char *s) {
    if (!s || !*s) return 0;
    if (*s == '-' || *s == '+') s++;
    int dots = 0;
    if (!*s) return 0;
    for (; *s; s++) {
        if (*s == '.') {
            if (++dots > 1) return 0;
        } else if (*s < '0' || *s > '9') {
            return 0;
        }
    }
    return 1;
}

static void usage(const char *argv0) {
    fprintf(stderr, "Uso:\n");
    fprintf(stderr, "  %s\n", argv0);
    fprintf(stderr, "  %s <nthreads> [raio] [imagem.ppm|pgm] [sigma_ruido]\n", argv0);
    fprintf(stderr, "  %s <nthreads> [raio] --sintetica <W> <H> [sigma_ruido]\n", argv0);
}

static double vec_mean(const double *v, int n) {
    double s = 0.0;
    for (int i = 0; i < n; i++) s += v[i];
    return s / (double)n;
}

static double vec_stdev(const double *v, int n, double mean) {
    if (n < 2) return 0.0;
    double s = 0.0;
    for (int i = 0; i < n; i++) {
        const double d = v[i] - mean;
        s += d * d;
    }
    return sqrt(s / (double)(n - 1));
}

static void ensure_out_dir(void) {
#ifdef _WIN32
    _mkdir(OUT_DIR);
#else
    mkdir(OUT_DIR, 0755);
#endif
}

static void path_join(char *dst, size_t n, const char *file) {
    snprintf(dst, n, "%s/%s", OUT_DIR, file);
}

int main(int argc, char **argv) {
    int nthreads_only = 0;
    int radius = 9;
    int synth_w = 0, synth_h = 0;
    const char *image_path = DEFAULT_IMAGE;
    float noise_sigma = 25.0f;
    int argi = 1;

    if (argc >= 2 && is_number(argv[1])) {
        nthreads_only = atoi(argv[1]);
        argi = 2;
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
        } else {
            image_path = argv[argi];
            argi++;
        }
    }
    if (argc > argi && is_number(argv[argi])) {
        noise_sigma = (float)atof(argv[argi]);
    }

    if (radius < 1) {
        usage(argv[0]);
        return 1;
    }

    const float sigma_s = (float)radius / 2.0f;
    const float sigma_r = (noise_sigma > 0.0f) ? (2.0f * noise_sigma) : 30.0f;
    const int nprocs = omp_get_num_procs();

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
    const double nhood = neighborhood_ops(width, height, radius, ch);
    const double bytes = (double)image_n(&src) * (double)ch * sizeof(float);

    printf("Filtro bilateral  |  imagem %dx%d x %d canais  |  raio %d (kernel %dx%d)\n",
           width, height, ch, radius, k, k);
    printf("entrada: %s\n", image_path ? image_path : "(sintetica)");
    printf("sigma_s = %.2f  |  sigma_r = %.2f  |  ruido AWGN sigma = %.1f  |  CPUs OpenMP = %d\n",
           sigma_s, sigma_r, noise_sigma, nprocs);
    printf("vizinhos/pixel/canal = %d  |  trabalho ~= %.2f Gvizinhancas  |  imagem = %.1f MB\n\n",
           k * k, nhood / 1e9, bytes / (1024.0 * 1024.0));

    ensure_out_dir();
    Image seq = image_alloc(width, height, ch);
    Image par = image_alloc(width, height, ch);

    char p_orig[256], p_in[256], p_seq[256], p_par[256], p_csv[256];
    path_join(p_orig, sizeof(p_orig), "original.ppm");
    path_join(p_in, sizeof(p_in), "entrada.ppm");
    path_join(p_seq, sizeof(p_seq), "saida_sequencial.ppm");
    path_join(p_par, sizeof(p_par), "saida_paralela.ppm");
    path_join(p_csv, sizeof(p_csv), "tempos_10reps.csv");

    save_pnm(p_orig, &clean);
    save_pnm(p_in, &src);

    if (nthreads_only > 0) {
        const double t_seq = time_filter(&src, &seq, radius, sigma_s, sigma_r, 0);
        save_pnm(p_seq, &seq);

        char label[16];
        const double t_par = time_filter(&src, &par, radius, sigma_s, sigma_r, nthreads_only);
        snprintf(label, sizeof(label), "%d", nthreads_only);

        print_header();
        print_row("seq", t_seq, t_seq, width, height, radius, ch);
        print_row(label, t_par, t_seq, width, height, radius, ch);
        print_validation(max_abs_diff(&seq, &par), 0);
        save_pnm(p_par, &par);
    } else {
        const int counts[] = {0, 1, 2, 4, 8, 16, 32};
        const int ncfg = (int)(sizeof(counts) / sizeof(counts[0]));
        double times[7][N_REPS];
        double worst_err = 0.0;

        printf("Aquecimento (descartado)...\n");
        fflush(stdout);
        bilateral_filter(&src, &seq, radius, sigma_s, sigma_r, 0);
        bilateral_filter(&src, &par, radius, sigma_s, sigma_r, 8);
        save_pnm(p_seq, &seq);
        save_pnm(p_par, &par);
        worst_err = max_abs_diff(&seq, &par);

        FILE *csv = fopen(p_csv, "w");
        if (!csv) die("Nao foi possivel gravar o CSV de tempos");
        fprintf(csv, "rep,config,threads,tempo_s\n");

        for (int r = 0; r < N_REPS; r++) {
            for (int i = 0; i < ncfg; i++) {
                const int t = counts[i];
                Image *dst = (t == 0) ? &seq : &par;
                if (t == 0) {
                    printf("  rep %2d/%d  seq ...\n", r + 1, N_REPS);
                } else {
                    printf("  rep %2d/%d  %d threads ...\n", r + 1, N_REPS, t);
                }
                fflush(stdout);
                times[i][r] = time_filter(&src, dst, radius, sigma_s, sigma_r, t);
                fprintf(csv, "%d,%s,%d,%.6f\n", r + 1, t == 0 ? "seq" : "omp", t, times[i][r]);
                if (t != 0) {
                    const double err = max_abs_diff(&seq, &par);
                    if (err > worst_err) worst_err = err;
                }
            }
        }
        fclose(csv);
        save_pnm(p_par, &par);

        const double t_seq_mean = vec_mean(times[0], N_REPS);
        printf("\nProtocolo: 1 aquecimento + %d repeticoes (ordem seq,1,2,4,8,16,32 em cada volta)\n",
               N_REPS);
        printf("Compilacao: gcc -O3 -fopenmp\n\n");
        printf("%-10s %12s %12s %10s %12s %14s\n",
               "threads", "media(s)", "desvio(s)", "speedup", "eficiencia", "proxy E (s*th)");
        for (int i = 0; i < ncfg; i++) {
            const int t = counts[i];
            const double m = vec_mean(times[i], N_REPS);
            const double sd = vec_stdev(times[i], N_REPS, m);
            if (t == 0) {
                printf("%-10s %12.4f %12.4f %10s %12s %14s\n",
                       "seq", m, sd, "-", "-", "-");
            } else {
                char label[16];
                const double sp = t_seq_mean / m;
                snprintf(label, sizeof(label), "%d", t);
                printf("%-10s %12.4f %12.4f %10.3f %12.3f %14.3f\n",
                       label, m, sd, sp, sp / (double)t, m * (double)t);
            }
        }

        print_validation(worst_err, 1);
        printf("CSV: %s\n", p_csv);
    }

    if (noise_sigma > 0.0f) {
        printf("PSNR ruidosa vs original  = %.2f dB\n", psnr(&clean, &src));
        printf("PSNR bilateral vs original = %.2f dB  (maior = mais perto da foto limpa)\n",
               psnr(&clean, &seq));
    }

    printf("\nArquivos em %s/: original.ppm, entrada.ppm, saida_sequencial.ppm, saida_paralela.ppm\n",
           OUT_DIR);

    image_free(&src);
    image_free(&clean);
    image_free(&seq);
    image_free(&par);
    return 0;
}
