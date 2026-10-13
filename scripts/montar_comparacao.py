"""Monta uma figura original | ruidosa | filtrada, com recortes ampliados.

Le as imagens PPM de dados/entrada e a saida filtrada (gerada rodando o
filtro com --save, ex.: dados/entrada/montanha_4k_filtrada_omp.ppm).
Escreve figuras/comparacao_bilateral.png.

Uso:
    python scripts/montar_comparacao.py [nome_base]
    # nome_base padrao: montanha_4k
"""

import sys
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parent.parent
ENTRADA = ROOT / "dados" / "entrada"
FIGURAS = ROOT / "figuras"

# Recortes (x0, y0, x1, y1): ceu liso (o ruido aparece) e face da rocha (aresta).
CROPS = {
    "ceu": (2520, 40, 2520 + 720, 40 + 400),
    "rocha": (1480, 720, 1480 + 720, 720 + 400),
}


def load(path: Path) -> Image.Image:
    im = Image.open(path).convert("RGB")
    return im


def zoom_crop(im: Image.Image, box: tuple[int, int, int, int], scale: int = 2) -> Image.Image:
    crop = im.crop(box)
    return crop.resize((crop.width * scale, crop.height * scale), Image.Resampling.NEAREST)


def label(im: Image.Image, text: str) -> Image.Image:
    out = im.copy()
    draw = ImageDraw.Draw(out)
    try:
        font = ImageFont.truetype("arial.ttf", 42)
    except OSError:
        font = ImageFont.load_default()
    pad = 12
    bbox = draw.textbbox((0, 0), text, font=font)
    tw, th = bbox[2] - bbox[0], bbox[3] - bbox[1]
    draw.rectangle([0, 0, tw + 2 * pad, th + 2 * pad], fill=(0, 0, 0))
    draw.text((pad, pad), text, fill=(255, 255, 255), font=font)
    return out


def row(images: list[Image.Image]) -> Image.Image:
    gap = 16
    w = sum(im.width for im in images) + gap * (len(images) - 1)
    h = max(im.height for im in images)
    canvas = Image.new("RGB", (w, h), (24, 24, 24))
    x = 0
    for im in images:
        canvas.paste(im, (x, 0))
        x += im.width + gap
    return canvas


def main() -> None:
    base = sys.argv[1] if len(sys.argv) > 1 else "montanha_4k"
    limpa = ENTRADA / f"{base}.ppm"
    ruidosa = ENTRADA / f"{base}_ruido.ppm"
    filtradas = sorted(ENTRADA.glob(f"{base}_filtrada_*.ppm"))
    if not filtradas:
        raise SystemExit(
            f"Nao encontrei a saida filtrada. Rode primeiro, por exemplo:\n"
            f"  ./bilateral_omp.exe 8 3 dados/entrada/{base}.ppm 25 --save"
        )
    filtrada = filtradas[0]

    original = load(limpa)
    noisy = load(ruidosa)
    filt = load(filtrada)

    preview_w = 1280
    scale = preview_w / original.width
    preview_h = int(original.height * scale)

    top = row([
        label(original.resize((preview_w, preview_h), Image.Resampling.BOX), "Original (limpa)"),
        label(noisy.resize((preview_w, preview_h), Image.Resampling.BOX), "Com ruido Gaussiano"),
        label(filt.resize((preview_w, preview_h), Image.Resampling.BOX), "Bilateral"),
    ])

    blocks = [top]
    for name, box in CROPS.items():
        blocks.append(row([
            label(zoom_crop(original, box), f"Zoom {name} — original"),
            label(zoom_crop(noisy, box), f"Zoom {name} — ruido"),
            label(zoom_crop(filt, box), f"Zoom {name} — bilateral"),
        ]))

    gap = 24
    width = max(b.width for b in blocks)
    height = sum(b.height for b in blocks) + gap * (len(blocks) - 1)
    out = Image.new("RGB", (width, height), (24, 24, 24))
    y = 0
    for b in blocks:
        out.paste(b, (0, y))
        y += b.height + gap

    FIGURAS.mkdir(exist_ok=True)
    dest = FIGURAS / "comparacao_bilateral.png"
    out.save(dest, quality=95)
    print(f"Salvo: {dest}")


if __name__ == "__main__":
    main()
