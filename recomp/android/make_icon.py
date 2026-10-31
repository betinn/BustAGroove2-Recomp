"""Ícone do app Android (mipmaps legados): disco de vinil laranja/preto, gerado com Pillow.
Uso: python recomp/android/make_icon.py  (escreve android/app/src/main/res/mipmap-*/ic_launcher.png)"""
import math
import os

from PIL import Image, ImageDraw

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "android", "app", "src", "main", "res")
SIZES = {"mdpi": 48, "hdpi": 72, "xhdpi": 96, "xxhdpi": 144, "xxxhdpi": 192}


def render(size):
    s = size * 8  # supersample
    im = Image.new("RGBA", (s, s), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    c = s / 2
    # fundo: quadrado arredondado roxo escuro
    r = s * 0.22
    d.rounded_rectangle([0, 0, s - 1, s - 1], radius=r, fill=(38, 16, 58, 255))
    # disco
    R = s * 0.40
    d.ellipse([c - R, c - R, c + R, c + R], fill=(14, 14, 18, 255))
    for i in range(1, 7):
        rr = R * (0.45 + 0.09 * i)
        d.ellipse([c - rr, c - rr, c + rr, c + rr], outline=(40, 40, 46, 255), width=max(1, s // 160))
    # reflexo
    for a0 in (200, 20):
        d.pieslice([c - R, c - R, c + R, c + R], a0, a0 + 28, fill=(60, 60, 70, 255))
    # rótulo laranja com nota
    lr = R * 0.42
    d.ellipse([c - lr, c - lr, c + lr, c + lr], fill=(255, 106, 0, 255))
    hr = R * 0.06
    d.ellipse([c - hr, c - hr, c + hr, c + hr], fill=(14, 14, 18, 255))
    # "2"
    w = max(2, s // 40)
    pts = [(c - lr * 0.35, c - lr * 0.35), (c + lr * 0.35, c - lr * 0.35), (c + lr * 0.35, c + 0),
           (c - lr * 0.35, c + lr * 0.5), (c + lr * 0.35, c + lr * 0.5)]
    d.line(pts, fill=(255, 240, 220, 255), width=w, joint="curve")
    return im.resize((size, size), Image.LANCZOS)


def main():
    for name, size in SIZES.items():
        out_dir = os.path.join(ROOT, f"mipmap-{name}")
        os.makedirs(out_dir, exist_ok=True)
        render(size).save(os.path.join(out_dir, "ic_launcher.png"))
        print("ok", out_dir)


if __name__ == "__main__":
    main()
