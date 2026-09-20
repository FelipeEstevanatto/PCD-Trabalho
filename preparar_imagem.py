"""
Baixa uma fotografia 4K em dominio publico (CC0) e gera:
  - JPEG original (cor)
  - PPM P6 em cor, lido pelo filtro em C
  - PNG em cor, para visualizar no Windows

Nao existe um 'dataset 4K oficial' de filtro bilateral. O padrao na literatura
e uma foto limpa com ruído Gaussiano adicionado (AWGN), que o programa em C
aplica de forma reproduzivel.
"""

from io import BytesIO
from pathlib import Path
from urllib.request import Request, urlopen

from PIL import Image

OUT_DIR = Path(__file__).resolve().parent / "imagens"
FILE_PAGE = "Landscape-mountains-nature-rock_(24030753220).jpg"
URL = f"https://commons.wikimedia.org/wiki/Special:FilePath/{FILE_PAGE}"
USER_AGENT = "PCD-bilateral-filter/1.0 (academic coursework; local experiment)"


def main() -> None:
    OUT_DIR.mkdir(exist_ok=True)
    jpg_path = OUT_DIR / "montanha_4k_original.jpg"
    ppm_path = OUT_DIR / "montanha_4k.ppm"
    png_path = OUT_DIR / "montanha_4k.png"
    fonte_path = OUT_DIR / "FONTE.txt"

    if jpg_path.exists() and jpg_path.stat().st_size > 1000:
        data = jpg_path.read_bytes()
        print(f"Usando JPEG ja baixado: {jpg_path}")
    else:
        print(f"Baixando {URL}")
        req = Request(URL, headers={"User-Agent": USER_AGENT})
        with urlopen(req, timeout=120) as resp:
            data = resp.read()
        jpg_path.write_bytes(data)
        print(f"JPEG original: {jpg_path} ({len(data)} bytes)")

    im = Image.open(BytesIO(data)).convert("RGB")
    print(f"Resolucao: {im.width} x {im.height}  |  modo RGB")

    im.save(ppm_path)
    im.save(png_path)

    fonte_path.write_text(
        "Titulo: Landscape-mountains-nature-rock (24030753220).jpg\n"
        "Autor: www.Pixel.la Free Stock Photos\n"
        "Fonte: Wikimedia Commons\n"
        "Licenca: CC0 1.0 Universal (dominio publico)\n"
        "Pagina: https://commons.wikimedia.org/wiki/File:Landscape-mountains-nature-rock_(24030753220).jpg\n"
        f"Resolucao original: {im.width} x {im.height}\n"
        "Uso: fotografia em cor (PPM P6). O filtro bilateral adiciona ruído\n"
        "     Gaussiano (AWGN) de forma controlada, como nos papers de denoising.\n",
        encoding="utf-8",
    )
    print(f"PPM (cor): {ppm_path}")
    print(f"PNG (cor): {png_path}")
    print(f"Creditos: {fonte_path}")


if __name__ == "__main__":
    main()
