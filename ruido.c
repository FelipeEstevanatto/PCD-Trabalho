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


void add_gaussian_noise(Image *img, float sigma, unsigned seed) {
    if (sigma <= 0.0f) return;
    const int n = (int)image_n(img) * img->channels;
    unsigned rng = seed;
    for (int i = 0; i < n; i++) {
        img->data[i] = clampf(img->data[i] + sigma * randn(&rng), 0.0f, 255.0f);
    }
}