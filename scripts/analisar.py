"""Consolida os logs de dados/resultados em CSV e tabelas (formato do artigo).

Cada arquivo dados/resultados/<imagem>/<imagem>_K<raio>_<cfg>.txt contem as
repeticoes de uma configuracao, cada uma produzida sob o Intel PCM quando
disponivel. Extraimos, por repeticao:

  - tempo do filtro (tempo_s=...)
  - energia do pacote (coluna "CPU energy" da tabela MEM do PCM, em J)
  - PSNR do filtro e da imagem ruidosa
  - duracao do processo pelo TSC (para potencia media = energia / duracao)

Metricas derivadas (relatorio, Sec. 4.3/4.4): speedup (S = T_seq / T_p),
eficiencia (S/p), potencia media, MFLOPS/W e EDP (energia x tempo).

Uso:
    python scripts/analisar.py             # todos os logs de dados/resultados
    python scripts/analisar.py --figuras   # tambem gera figuras (matplotlib)
"""

from __future__ import annotations

import argparse
import csv
import re
import statistics as st
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
RES = ROOT / "dados" / "resultados"

# 8 operacoes por vizinho (relatorio, Sec. 4.3).
FLOPS_PER_NEIGHBOR = 8

RE_TEMPO = re.compile(r"tempo_s=([0-9.]+)")
RE_PSNR_FILTRO = re.compile(r"PSNR bilateral vs original\s*=\s*([0-9.]+)")
RE_PSNR_RUIDO = re.compile(r"PSNR gaussiano vs original\s*=\s*([0-9.]+)")
# O PCM imprime o TSC em Mticks (execucoes curtas) ou Gticks (longas).
RE_TSC = re.compile(r"Time \(TSC\):\s*([\d.]+)\s*([KMG]?)ticks")
TSC_MULT = {"": 1.0, "K": 1e3, "M": 1e6, "G": 1e9}
RE_FREQ = re.compile(r"Nominal core frequency:\s*(\d+)\s*Hz")
RE_DIM = re.compile(r"bilateral (?:SEQ|OMP)\s+\|\s+(\d+)x(\d+) x (\d+)")
# linha final da tabela MEM: SKT 0 READ WRITE IO IA GT | CPU energy | PP0 energy | PP1 energy
RE_MEM = re.compile(
    r"^\s*SKT\s+0\s+([-\d.]+)\s+([-\d.]+)\s+([-\d.]+)\s+([-\d.]+)\s+([-\d.]+)"
    r"\s+([-\d.]+)\s+([-\d.]+)\s+([-\d.]+)",
    re.MULTILINE,
)
RE_NAME = re.compile(r"(?P<base>.+?)_K(?P<raio>\d+)_(?P<cfg>.+)\.txt$")


def split_runs(text: str) -> list[str]:
    """Separa as repeticoes de um log (blocos com tempo_s=)."""
    if "Performance Counter Monitor" in text:
        parts = re.split(r"Intel\(r\) Performance Counter Monitor", text)
    else:  # execucao sem PCM: cada bloco comeca no cabecalho do programa
        parts = re.split(r"bilateral (?:SEQ|OMP)\s+\|", text)
    return [p for p in parts if "tempo_s=" in p]


def parse_run(block: str) -> dict:
    def f1(rx):
        m = rx.search(block)
        return float(m.group(1)) if m else None

    dims = RE_DIM.search(block)
    width, height, channels = (int(dims.group(1)), int(dims.group(2)), int(dims.group(3))) if dims else (None, None, None)

    energy = pp0 = None
    matches = RE_MEM.findall(block)
    if matches:
        g = matches[-1]  # ultima tabela MEM = fim do processo
        energy, pp0 = float(g[5]), float(g[6])

    tsc = RE_TSC.search(block)
    freq = RE_FREQ.search(block)
    duration = None
    if tsc and freq and int(freq.group(1)) > 0:
        ticks = float(tsc.group(1)) * TSC_MULT[tsc.group(2)]
        duration = ticks / int(freq.group(1))

    return {
        "tempo": f1(RE_TEMPO),
        "psnr_filtro": f1(RE_PSNR_FILTRO),
        "psnr_ruido": f1(RE_PSNR_RUIDO),
        "energia": energy,
        "pp0": pp0,
        "duracao": duration,
        "w": width,
        "h": height,
        "c": channels,
    }


def mean_std(values: list[float]) -> tuple[float | None, float | None]:
    vals = [v for v in values if v is not None]
    if not vals:
        return None, None
    if len(vals) == 1:
        return vals[0], 0.0
    return st.mean(vals), st.stdev(vals)


def load_logs() -> list[dict]:
    rows = []
    for path in sorted(RES.glob("*/*.txt")):
        m = RE_NAME.match(path.name)
        if not m:
            continue
        base, raio, cfg = m.group("base"), int(m.group("raio")), m.group("cfg")
        if cfg.lower() == "seq":  # os logs originais usam "seq" e "Seq"
            cfg, threads = "seq", 1
        else:
            threads = int(cfg)
        runs = [parse_run(b) for b in split_runs(path.read_text(encoding="utf-8", errors="ignore"))]
        runs = [r for r in runs if r["tempo"] is not None]
        if not runs:
            continue

        dims = next((r for r in runs if r["w"]), runs[0])
        nflop = None
        if dims["w"]:
            nflop = FLOPS_PER_NEIGHBOR * dims["w"] * dims["h"] * dims["c"] * (2 * raio + 1) ** 2

        t_mean, t_sd = mean_std([r["tempo"] for r in runs])
        e_mean, e_sd = mean_std([r["energia"] for r in runs])
        p_mean, _ = mean_std([r["energia"] / r["duracao"] for r in runs if r["energia"] and r["duracao"]])
        mflops_w, _ = mean_std(
            [nflop / 1e6 / r["energia"] for r in runs if nflop and r["energia"]]
        )
        psnr_f, _ = mean_std([r["psnr_filtro"] for r in runs])
        edp = (e_mean * t_mean) if (e_mean is not None and t_mean is not None) else None

        rows.append({
            "imagem": base, "raio": raio, "config": cfg, "threads": threads,
            "reps": len(runs), "tempo_media_s": t_mean, "tempo_desvio_s": t_sd,
            "energia_media_J": e_mean, "energia_desvio_J": e_sd, "potencia_media_W": p_mean,
            "mflops_w": mflops_w, "edp_Js": edp, "psnr_filtro_dB": psnr_f,
        })
    return rows


def add_speedup(rows: list[dict]) -> None:
    seq = {(r["imagem"], r["raio"]): r for r in rows if r["config"] == "seq"}
    for r in rows:
        ref = seq.get((r["imagem"], r["raio"]))
        if not ref or not ref["tempo_media_s"] or not r["tempo_media_s"]:
            r["speedup"] = r["eficiencia"] = None
            continue
        r["speedup"] = ref["tempo_media_s"] / r["tempo_media_s"]
        r["eficiencia"] = r["speedup"] / r["threads"] if r["threads"] else None


COLS = ["imagem", "raio", "config", "threads", "reps", "tempo_media_s", "tempo_desvio_s",
        "energia_media_J", "energia_desvio_J", "potencia_media_W", "speedup",
        "eficiencia", "mflops_w", "edp_Js", "psnr_filtro_dB"]


def fmt(v, nd=2):
    return "-" if v is None else f"{v:.{nd}f}"


def print_tables(rows: list[dict]) -> None:
    for imagem in sorted({r["imagem"] for r in rows}):
        print(f"\n### {imagem}")
        for raio in sorted({r["raio"] for r in rows if r["imagem"] == imagem}):
            print(f"\nRaio r={raio}")
            print(f"{'cfg':>5} {'T(s)':>12} {'E(J)':>12} {'P(W)':>8} {'S':>7} {'Ef':>6} {'MFLOPS/W':>9} {'EDP':>10}")
            for r in sorted((x for x in rows if x["imagem"] == imagem and x["raio"] == raio),
                            key=lambda x: x["threads"]):
                t = fmt(r["tempo_media_s"]) + "±" + fmt(r["tempo_desvio_s"]) if r["tempo_desvio_s"] is not None else fmt(r["tempo_media_s"])
                print(f"{r['config']:>5} {t:>12} {fmt(r['energia_media_J']):>12} "
                      f"{fmt(r['potencia_media_W']):>8} {fmt(r['speedup'],3):>7} {fmt(r['eficiencia'],3):>6} "
                      f"{fmt(r['mflops_w'],1):>9} {fmt(r['edp_Js'],0):>10}")


def write_csv(rows: list[dict]) -> None:
    for imagem in sorted({r["imagem"] for r in rows}):
        dest = RES / f"{imagem}_resumo.csv"
        with dest.open("w", newline="", encoding="utf-8") as f:
            w = csv.DictWriter(f, fieldnames=COLS)
            w.writeheader()
            for r in sorted((x for x in rows if x["imagem"] == imagem), key=lambda x: (x["raio"], x["threads"])):
                w.writerow({k: r.get(k) for k in COLS})
        print(f"CSV: {dest}")


def make_figures(rows: list[dict]) -> None:
    try:
        import matplotlib
        matplotlib.use("Agg")
        import matplotlib.pyplot as plt
    except ImportError:
        print("matplotlib ausente; pulei as figuras.")
        return

    figdir = ROOT / "figuras"
    figdir.mkdir(exist_ok=True)
    for imagem in sorted({r["imagem"] for r in rows}):
        data = [r for r in rows if r["imagem"] == imagem and r["config"] != "seq"]
        if not data:
            continue
        fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(10, 4))
        for raio in sorted({r["raio"] for r in data}):
            pts = sorted((x for x in data if x["raio"] == raio), key=lambda x: x["threads"])
            xs = [x["threads"] for x in pts]
            ax1.plot(xs, [x["speedup"] for x in pts], marker="o", label=f"r={raio}")
            if pts[0]["energia_media_J"]:
                seq = next((r for r in rows if r["imagem"] == imagem and r["raio"] == raio and r["config"] == "seq"), None)
                if seq and seq["energia_media_J"]:
                    ax2.plot(xs, [x["energia_media_J"] / seq["energia_media_J"] for x in pts], marker="o", label=f"r={raio}")
        ax1.set_xlabel("threads"); ax1.set_ylabel("speedup"); ax1.set_title("Speedup"); ax1.legend(); ax1.grid(True)
        ax2.set_xlabel("threads"); ax2.set_ylabel("E / E_seq"); ax2.set_title("Energia normalizada"); ax2.legend(); ax2.grid(True)
        fig.tight_layout()
        dest = figdir / f"{imagem}_desempenho.png"
        fig.savefig(dest, dpi=150)
        print(f"Figura: {dest}")


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--figuras", action="store_true", help="gera figuras (requer matplotlib)")
    args = ap.parse_args()

    if not RES.exists():
        print(f"Sem logs em {RES}. Rode antes o scripts/run_experimento.")
        return
    rows = load_logs()
    if not rows:
        print(f"Nenhum log parseavel em {RES}.")
        return
    add_speedup(rows)
    print_tables(rows)
    write_csv(rows)
    if args.figuras:
        make_figures(rows)


if __name__ == "__main__":
    main()
