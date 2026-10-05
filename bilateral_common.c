/*
 * Implementacao das utilidades compartilhadas (I/O, ruido, PSNR, pixel).
 *
 * Fontes da DEFINICAO (nao do texto-fonte deste arquivo):
 *   Tomasi & Manduchi, ICCV 1998, p. 839-846
 *     DOI 10.1109/ICCV.1998.710815 — pesos espacial * faixa, exp()
 *   Gonzalez & Woods, Digital Image Processing, 4. ed., Pearson, 2018
 *     — PSNR e protocolo foto limpa + AWGN
 *   Box & Muller, Ann. Math. Statist., v. 29, n. 2, p. 610-611, 1958
 *     DOI 10.1214/aoms/1177706645 — ruido Gaussiano
 *   Press, Teukolsky, Vetterling & Flannery, Numerical Recipes, 3. ed.,
 *     Cambridge Univ. Press, 2007 — LCG 1664525 / 1013904223
 */

#include "bilateral_common.h"

#include <ctype.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <direct.h>
#include <windows.h>
#else
#include <sys/stat.h>
#include <time.h>
#endif

size_t image_n(const Image *img) {
    return (size_t)img->width * (size_t)img->height;
}

Image image_alloc(int width, int height, int channels) {
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

void image_free(Image *img) {
    free(img->data);
    img->data = NULL;
}

Image image_clone(const Image *src) {
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

void die(const char *msg) {
    fprintf(stderr, "%s\n", msg);
    exit(1);
}

double wall_time(void) {
#ifdef _WIN32
    static LARGE_INTEGER freq;
    static int init = 0;
    LARGE_INTEGER now;
    if (!init) {
        QueryPerformanceFrequency(&freq);
        init = 1;
    }
    QueryPerformanceCounter(&now);
    return (double)now.QuadPart / (double)freq.QuadPart;
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
#endif
}

static unsigned lcg_next(unsigned *state) {
    /* Press, Teukolsky, Vetterling & Flannery, Numerical Recipes, 3. ed., 2007:
     * X_{n+1} = 1664525 X_n + 1013904223 */
    *state = (*state * 1664525u + 1013904223u);
    return *state;
}

static float rand_u01(unsigned *state) {
    return ((lcg_next(state) >> 8) + 1) / 16777216.0f;
}

static float randn(unsigned *state) {
    /* Box & Muller, Ann. Math. Statist. 29(2):610-611, 1958,
     * DOI 10.1214/aoms/1177706645: dois uniformes -> um normal N(0,1). */
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

Image load_pnm(const char *path) {
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

void generate_synthetic_image(Image *img) {
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

const char *save_pnm(const char *path, const Image *img) {
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

double max_abs_diff(const Image *a, const Image *b) {
    const int n = (int)image_n(a) * a->channels;
    double worst = 0.0;
    for (int i = 0; i < n; i++) {
        double d = fabs((double)a->data[i] - (double)b->data[i]);
        if (d > worst) worst = d;
    }
    return worst;
}

/* PSNR: Gonzalez & Woods, Digital Image Processing, 4. ed., Pearson, 2018. */
double psnr(const Image *ref, const Image *other) {
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

float *make_spatial_kernel(int radius, float sigma_s, int *k_out) {
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

double neighborhood_ops(int width, int height, int radius, int channels) {
    const double k = (double)(2 * radius + 1);
    return (double)width * (double)height * (double)channels * k * k;
}

int is_number(const char *s) {
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

void ensure_out_dir(void) {
#ifdef _WIN32
    _mkdir(OUT_DIR);
#else
    mkdir(OUT_DIR, 0755);
#endif
}

void path_join(char *dst, size_t n, const char *file) {
    snprintf(dst, n, "%s/%s", OUT_DIR, file);
}
