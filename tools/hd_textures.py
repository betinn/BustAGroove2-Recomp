#!/usr/bin/env python3
"""Pacote de texturas HD para o Bust A Groove 2 recompilado (PSXRecomp, formato DuckStation).

Amplia os PNG gravados pelo mod `psx.enhancement.hd-textures` ("Dump textures") por um multiplo
inteiro, mantendo nome, proporcao e as classes de alfa do PS1, e grava em `replacements/`.

Uso (padroes apontam para tools/recomp/bag2/build/mods/texture-packs/SLUS-01159):
    python tools/hd_textures.py                      # dumps/ -> replacements/, 4x, xBR
    python tools/hd_textures.py --scale 2            # 2x
    python tools/hd_textures.py --filter nearest     # so replica pixels (teste/controle)
    python tools/hd_textures.py --dumps D --out R    # outras pastas
    python tools/hd_textures.py --force              # regrava arquivos ja existentes

Algoritmo: xBR nivel 1 (Hyllian) em numpy, 2x por passada, aplicado log2(scale) vezes. Cada pixel
de saida copia a cor de um pixel de entrada (sem mistura), entao os valores de alfa continuam
exatamente os do dump (0 = recorte, 143 = semitransparente STP, 255 = opaco): o runtime classifica
transparencia por limiares de alfa (docs/DUCKSTATION_TEXTURE_FORMAT.md) e mistura criaria franjas.
Dependencias: pip install pillow numpy.
"""
from __future__ import annotations

import argparse
import os
import sys
from concurrent.futures import ProcessPoolExecutor

import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
PACK = os.path.join(HERE, "recomp", "bag2", "build", "mods", "texture-packs", "SLUS-01159")

# Pesos YUV do xBR e limiar de "cores iguais" (escala 0..255).
_Y = np.array([0.299, 0.587, 0.114], dtype=np.float32)
_U = np.array([-0.169, -0.331, 0.5], dtype=np.float32)
_V = np.array([0.5, -0.419, -0.081], dtype=np.float32)
EQ_THRESHOLD = 15.0


def _yuva(img: np.ndarray) -> np.ndarray:
    """RGBA uint8 (H,W,4) -> (H,W,4) float32 com Y, U, V ponderados e alfa com peso alto."""
    rgb = img[..., :3].astype(np.float32)
    y = rgb @ _Y
    u = rgb @ _U
    v = rgb @ _V
    a = img[..., 3].astype(np.float32)
    # 48/7/6 sao os pesos classicos do xBR; a diferenca de alfa domina para que um pixel
    # recortado nunca "conte" como vizinho da mesma cor de um pixel opaco.
    return np.stack([y * (48.0 / 61.0), u * (7.0 / 61.0), v * (6.0 / 61.0), a * 4.0], axis=-1)


def _df(a: np.ndarray, b: np.ndarray) -> np.ndarray:
    return np.abs(a - b).sum(axis=-1)


def _eq(a: np.ndarray, b: np.ndarray) -> np.ndarray:
    return _df(a, b) < EQ_THRESHOLD


def xbr2x(img: np.ndarray) -> np.ndarray:
    """xBR nivel 1, 2x. img: RGBA uint8 (H,W,4). Retorna (2H,2W,4) uint8."""
    h, w, _ = img.shape
    pad = np.pad(img, ((2, 2), (2, 2), (0, 0)), mode="edge")
    f = _yuva(pad)

    def at(dy: int, dx: int) -> np.ndarray:  # vizinho deslocado, janela da imagem original
        return f[2 + dy:2 + dy + h, 2 + dx:2 + dx + w]

    def at_rgba(dy: int, dx: int) -> np.ndarray:
        return pad[2 + dy:2 + dy + h, 2 + dx:2 + dx + w]

    out = np.empty((2 * h, 2 * w, 4), dtype=np.uint8)
    E = at(0, 0)
    E_rgba = at_rgba(0, 0)
    # Quatro cantos por rotacao: (sy, sx) = sinal da diagonal do canto (1,1) = inferior-direito.
    for sy, sx, oy, ox in ((1, 1, 1, 1), (1, -1, 1, 0), (-1, 1, 0, 1), (-1, -1, 0, 0)):
        # Nomenclatura do xBR para o canto inferior-direito (rotacionada por sy/sx):
        #  A B C      com anel externo B1 (acima de B), D0 (esq. de D), F4 (dir. de F),
        #  D E F      H5 (abaixo de H), I4 (dir. de I), I5 (abaixo de I), C1, G0.
        #  G H I
        B, D = at(-sy, 0), at(0, -sx)
        F, H = at(0, sx), at(sy, 0)
        C, G, I = at(-sy, sx), at(sy, -sx), at(sy, sx)
        F4, I4 = at(0, 2 * sx), at(sy, 2 * sx)
        H5, I5 = at(2 * sy, 0), at(2 * sy, sx)
        e = _df(E, C) + _df(E, G) + _df(I, H5) + _df(I, F4) + 4.0 * _df(H, F)
        i = _df(H, D) + _df(H, I5) + _df(F, I4) + _df(F, B) + 4.0 * _df(E, I)
        edr = (e < i) & ~(_eq(E, F) & _eq(E, H)) & (
            (~_eq(F, B) & ~_eq(H, D))
            | (_eq(E, I) & ~_eq(F, I4) & ~_eq(H, I5))
            | _eq(E, G)
            | _eq(E, C)
        )
        use_f = _df(E, F) <= _df(E, H)
        px = np.where(use_f[..., None], at_rgba(0, sx), at_rgba(sy, 0))
        sub = np.where(edr[..., None], px, E_rgba)
        out[oy::2, ox::2] = sub
    return out


def upscale(img: Image.Image, scale: int, filt: str) -> Image.Image:
    rgba = np.asarray(img.convert("RGBA"))
    if filt == "nearest":
        return Image.fromarray(np.repeat(np.repeat(rgba, scale, axis=0), scale, axis=1))
    cur = rgba
    s = 1
    while s * 2 <= scale:
        cur = xbr2x(cur)
        s *= 2
    if s != scale:  # resto nao-potencia-de-2: replica pixels (mantem alfa discreto)
        k = scale // s
        cur = np.repeat(np.repeat(cur, k, axis=0), k, axis=1)
    return Image.fromarray(cur)


def process(args: tuple[str, str, int, str]) -> tuple[str, str]:
    src, dst, scale, filt = args
    try:
        img = Image.open(src)
        out = upscale(img, scale, filt)
        tmp = dst + ".tmp"
        out.save(tmp, format="PNG", optimize=False)
        os.replace(tmp, dst)
        return src, f"{img.size[0]}x{img.size[1]} -> {out.size[0]}x{out.size[1]}"
    except Exception as exc:  # noqa: BLE001 - reportar e seguir
        return src, f"ERRO {exc}"


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--dumps", default=os.path.join(PACK, "dumps"))
    ap.add_argument("--out", default=os.path.join(PACK, "replacements"))
    ap.add_argument("--scale", type=int, default=4, help="multiplo inteiro (padrao 4)")
    ap.add_argument("--filter", choices=("xbr", "nearest"), default="xbr")
    ap.add_argument("--force", action="store_true", help="regrava arquivos existentes em --out")
    ap.add_argument("--jobs", type=int, default=max(1, (os.cpu_count() or 2) - 1))
    a = ap.parse_args()
    if a.scale < 1:
        ap.error("--scale deve ser >= 1")
    if not os.path.isdir(a.dumps):
        print(f"pasta de dumps nao existe: {a.dumps}", file=sys.stderr)
        return 1
    os.makedirs(a.out, exist_ok=True)
    jobs = []
    skipped = 0
    for root, _dirs, files in os.walk(a.dumps):
        rel = os.path.relpath(root, a.dumps)
        for name in sorted(files):
            if not name.lower().endswith(".png"):
                continue
            dst_dir = a.out if rel == "." else os.path.join(a.out, rel)
            os.makedirs(dst_dir, exist_ok=True)
            dst = os.path.join(dst_dir, name)
            if os.path.exists(dst) and not a.force:
                skipped += 1
                continue
            jobs.append((os.path.join(root, name), dst, a.scale, a.filter))
    print(f"{len(jobs)} texturas para ampliar ({a.filter} {a.scale}x), {skipped} ja existentes puladas")
    errors = 0
    if a.jobs > 1 and len(jobs) > 4:
        it = ProcessPoolExecutor(a.jobs).map(process, jobs, chunksize=4)
    else:
        it = map(process, jobs)
    for n, (src, msg) in enumerate(it, 1):
        if msg.startswith("ERRO"):
            errors += 1
            print(f"[{n}/{len(jobs)}] {os.path.basename(src)}: {msg}", file=sys.stderr)
        elif n % 50 == 0 or n == len(jobs):
            print(f"[{n}/{len(jobs)}] {os.path.basename(src)}: {msg}")
    print(f"pronto: {len(jobs) - errors} gravadas em {a.out}, {errors} erros")
    return 1 if errors else 0


if __name__ == "__main__":
    sys.exit(main())
