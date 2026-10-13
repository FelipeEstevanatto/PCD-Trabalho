/*
 * Gera a imagem com ruido Gaussiano aditivo (AWGN).
 *
 * Programa SEPARADO, como descrito no relatorio (Sec. 4.1): o ruido e gerado
 * antes e gravado em disco; o filtro le a imagem original e a ruidosa.
 * Gera amostras por Box-Muller (Box & Muller, 1958) sobre um LCG com semente
 * fixa (Press et al., 2007), garantindo a mesma imagem ruidosa a cada execucao.
 *
 * Uso:
 *   ruido <entrada.ppm|pgm> <saida.ppm> [sigma] [semente]
 * Padroes: sigma = 25, semente = 20260920 (os do experimento do artigo).
 */

#include "bilateral_common.h"

#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr, "Uso: %s <entrada.ppm|pgm> <saida.ppm> [sigma=25] [semente=20260920]\n",
                argv[0]);
        return 1;
    }

    const float sigma = (argc > 3) ? (float)atof(argv[3]) : 25.0f;
    const unsigned seed = (argc > 4) ? (unsigned)strtoul(argv[4], NULL, 10) : 20260920u;

    Image img = load_pnm(argv[1]);
    add_gaussian_noise(&img, sigma, seed);
    save_pnm(argv[2], &img);
    fprintf(stderr, "ruido: sigma=%.1f semente=%u -> %s\n", sigma, seed, argv[2]);
    image_free(&img);
    return 0;
}
