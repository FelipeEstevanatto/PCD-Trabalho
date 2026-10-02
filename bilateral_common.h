/*
 * Utilidades compartilhadas pelo filtro bilateral sequencial e OpenMP.
 * Definicao do filtro: Tomasi & Manduchi, ICCV 1998, DOI 10.1109/ICCV.1998.710815.
 */
#ifndef BILATERAL_COMMON_H
#define BILATERAL_COMMON_H

#include <math.h>
#include <stddef.h>
#include <stdio.h>

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

size_t image_n(const Image *img);
Image image_alloc(int width, int height, int channels);
void image_free(Image *img);
Image image_clone(const Image *src);

void die(const char *msg);
double wall_time(void);

Image load_pnm(const char *path);
void generate_synthetic_image(Image *img);
void add_gaussian_noise(Image *img, float sigma, unsigned seed);
const char *save_pnm(const char *path, const Image *img);

double max_abs_diff(const Image *a, const Image *b);
double psnr(const Image *ref, const Image *other);

float *make_spatial_kernel(int radius, float sigma_s, int *k_out);

double neighborhood_ops(int width, int height, int radius, int channels);
int is_number(const char *s);
void ensure_out_dir(void);
void path_join(char *dst, size_t n, const char *file);

/* Um pixel: Tomasi & Manduchi, ICCV 1998, DOI 10.1109/ICCV.1998.710815.
 * No header para o compilador inlinear nos dois binarios. */
static inline float bilateral_pixel(const float *in, int w, int h, int x, int y,
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

#endif /* BILATERAL_COMMON_H */
