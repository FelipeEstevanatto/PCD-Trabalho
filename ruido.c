#include "bilateral_common.h"

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <time.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static unsigned lcg_next(unsigned *state) {
    *state = (*state * 1664525u + 1013904223u);
    return *state;
}

static float rand_u01(unsigned *state) {
    return ((lcg_next(state) >> 8) + 1) / 16777216.0f;
}

static float randn(unsigned *state) {
    float u1 = rand_u01(state);
    float u2 = rand_u01(state);

    return sqrtf(-2.0f * logf(u1)) *
           cosf((float)(2.0 * M_PI) * u2);
}

static float clampf(float x, float min, float max) {
    if (x < min) return min;
    if (x > max) return max;
    return x;
}

void add_gaussian_noise(Image *img, float sigma, unsigned seed) {

    if (sigma <= 0.0f)
        return;

    const int n = (int)image_n(img) * img->channels;

    unsigned rng = seed;

    for (int i = 0; i < n; i++) {

        float ruido = sigma * randn(&rng);

        img->data[i] = clampf(
            img->data[i] + ruido,
            0.0f,
            255.0f
        );
    }
}


int main(int argc, char *argv[]) {

    if (argc != 2) {
        fprintf(stderr, "Uso: %s <caminho_da_imagem>\n", argv[0]);
        return 1;
    }

    const char *entrada = argv[1];

    Image img = load_pnm(entrada);

    add_gaussian_noise(&img, 25.0f, 20260920u);

    save_pnm("imagens/ruido.ppm", &img);
    image_free(&img);

    printf("Imagem com ruido gerada com sucesso: ruido.ppm\n");

    return 0;
}