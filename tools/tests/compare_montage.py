#!/usr/bin/env python3
"""Imagens de comparação original × melhorado do README, a partir das capturas de tools/tests/compare_shots.py.

Para cada cena de logs/comparacao/:
- <cena>_lado.jpg: o original como o PS1 mostra numa TV (4:3, pixels ampliados sem filtro) ao lado do melhorado (16:9),
  na mesma altura;
- <cena>_corte.jpg (cena da capa): metade esquerda original, metade direita melhorada no mesmo quadro 16:9, com o
  original no centro em 4:3 (as faixas pretas são o que a tela larga revela);
- <cena>_zoom.jpg: o mesmo recorte de um dançarino nas duas versões, ampliado.
O original ocupa as 1440 colunas do centro do quadro 16:9 de 1920 (a tela larga só acrescenta as laterais; a altura
é a mesma). A captura melhorada vem com a largura do modo de vídeo do jogo (512) esticada para 16:9 e é comprimida
para 3840 × 2160 antes.
Saída em docs/img/comparacao/.
Uso: python tools/tests/compare_montage.py [cena_da_capa] [cena:x0,y0,x1,y1 ...]
     (recortes do zoom em coordenadas do original 4:3 normalizadas de 0 a 1)
"""
from __future__ import annotations

import glob
import os
import sys

from PIL import Image, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, '..', '..'))
SRC = os.path.join(REPO, 'logs', 'comparacao')
OUT = os.path.join(REPO, 'docs', 'img', 'comparacao')
H = 1080                       # altura comum
W43, W169 = 1440, 1920
L_ORIG = 'PS1 original'
L_NEW = 'Recompilado + remake'
SUB_ORIG = '512×240, 4:3, texturas e modelos do jogo'
SUB_NEW = '4K interno, 16:9, modelos, texturas e luz novos'


def font(size, bold=False):
    for name in (('segoeuib.ttf', 'arialbd.ttf') if bold else ('segoeui.ttf', 'arial.ttf')):
        try:
            return ImageFont.truetype(os.path.join(os.environ.get('WINDIR', 'C:/Windows'), 'Fonts', name), size)
        except OSError:
            pass
    return ImageFont.load_default()


def load(name):
    orig = Image.open(os.path.join(SRC, f'{name}_original.png')).convert('RGB')
    new = Image.open(os.path.join(SRC, f'{name}_melhorado.png')).convert('RGB')
    orig43 = orig.resize((W43, H), Image.NEAREST)              # como a TV mostra: 4:3, pixels grandes
    new169 = new.resize((3840, 2160), Image.LANCZOS).resize((W169, H), Image.LANCZOS)
    return orig, new, orig43, new169


def label(im, xy, title, sub, anchor='la'):
    d = ImageDraw.Draw(im, 'RGBA')
    f1, f2 = font(44, True), font(26)
    x, y = xy
    tw = max(d.textlength(title, font=f1), d.textlength(sub, font=f2))
    if anchor == 'ra':
        x -= tw
    d.rounded_rectangle((x - 18, y - 12, x + tw + 18, y + 92), 14, fill=(0, 0, 0, 170))
    d.text((x, y), title, font=f1, fill=(255, 255, 255))
    d.text((x, y + 56), sub, font=f2, fill=(205, 205, 205))


def side_by_side(name, orig43, new169):
    gap = 16
    im = Image.new('RGB', (W43 + gap + W169, H), (16, 16, 16))
    im.paste(orig43, (0, 0))
    im.paste(new169, (W43 + gap, 0))
    label(im, (36, 30), L_ORIG, SUB_ORIG)
    label(im, (W43 + gap + 36, 30), L_NEW, SUB_NEW)
    im = im.resize((im.width * 2 // 3, H * 2 // 3), Image.LANCZOS)
    im.save(os.path.join(OUT, f'{name}_lado.jpg'), quality=90, optimize=True)


def split(name, orig43, new169):
    im = new169.copy()
    left = Image.new('RGB', (W169, H), (0, 0, 0))
    left.paste(orig43, ((W169 - W43) // 2, 0))
    im.paste(left.crop((0, 0, W169 // 2, H)), (0, 0))
    d = ImageDraw.Draw(im)
    d.rectangle((W169 // 2 - 2, 0, W169 // 2 + 2, H), fill=(255, 255, 255))
    label(im, (W169 // 2 - 36, 30), L_ORIG, SUB_ORIG, anchor='ra')
    label(im, (W169 // 2 + 36, 30), L_NEW, SUB_NEW)
    im.save(os.path.join(OUT, f'{name}_corte.jpg'), quality=90, optimize=True)


def zoom(name, orig, new, box):
    """box em coordenadas normalizadas do quadro 4:3 do original."""
    x0, y0, x1, y1 = box
    ow, oh = orig.size
    a = orig.crop((round(x0 * ow), round(y0 * oh), round(x1 * ow), round(y1 * oh)))
    # mesmo pedaço no 16:9 de 3840 × 2160: o 4:3 é a faixa central de 2880 colunas
    nw = new.resize((3840, 2160), Image.LANCZOS)
    bx = lambda u: 480 + u * 2880
    b = nw.crop((round(bx(x0)), round(y0 * 2160), round(bx(x1)), round(y1 * 2160)))
    hh = 900
    ww = round(hh * (x1 - x0) * 4 / 3 / (y1 - y0))
    a = a.resize((ww, hh), Image.NEAREST)
    b = b.resize((ww, hh), Image.LANCZOS)
    gap = 16
    im = Image.new('RGB', (ww * 2 + gap, hh), (16, 16, 16))
    im.paste(a, (0, 0))
    im.paste(b, (ww + gap, 0))
    label(im, (28, 24), L_ORIG, 'detalhe ampliado')
    label(im, (ww + gap + 28, 24), L_NEW, 'mesmo recorte')
    im = im.resize((im.width * 2 // 3, hh * 2 // 3), Image.LANCZOS)
    im.save(os.path.join(OUT, f'{name}_zoom.jpg'), quality=90, optimize=True)


def main():
    os.makedirs(OUT, exist_ok=True)
    names = sorted(os.path.basename(p)[:-len('_original.png')] for p in glob.glob(os.path.join(SRC, '*_original.png')))
    cover = sys.argv[1] if len(sys.argv) > 1 else (names[0] if names else '')
    boxes = {}
    for a in sys.argv[2:]:
        n, b = a.split(':')
        boxes[n] = tuple(float(v) for v in b.split(','))
    for name in names:
        if not os.path.exists(os.path.join(SRC, f'{name}_melhorado.png')):
            continue
        orig, new, orig43, new169 = load(name)
        side_by_side(name, orig43, new169)
        if name == cover:
            split(name, orig43, new169)
        if name in boxes:
            zoom(name, orig, new, boxes[name])
        print(name, 'ok', flush=True)


if __name__ == '__main__':
    main()
