#!/usr/bin/env python3
"""Montagens das capturas original x remake por palco (fase 6), a partir de stage_geo_pause.py.

Para cada posição com pausa_<k>_0.png (original) e pausa_<k>_1_0.png (remake) grava palco_<k>_ab.png (as duas lado a
lado, reduzidas, e embaixo um recorte do centro em tamanho real) e uma folha de contato palcos_ab.png com todas.
Uso: python tools/remake/stage_montage.py
"""
from __future__ import annotations

import glob
import os
import re

from PIL import Image, ImageDraw

HERE = os.path.dirname(os.path.abspath(__file__))
D = os.path.join(HERE, '..', 'recomp', 'logs', 'remake', 'palcos')
NAMES = {0: '234 ponte de aço', 1: '233 Sushi Planet', 2: '242 parque', 3: '243 fábrica', 4: '232 templo de pedra',
         5: '237 cenário de papel', 6: '244 templo oriental', 7: '235 discoteca', 8: '236 rua', 9: '231 navio pirata',
         10: '238 estúdio de TV', 11: '(posição 11)'}


def main():
    ks = sorted(int(m.group(1)) for f in glob.glob(os.path.join(D, 'pausa_*_0.png'))
                for m in [re.search(r'pausa_(\d+)_0\.png$', f)] if m)
    sheet = []
    for k in ks:
        a, b = os.path.join(D, f'pausa_{k:02d}_0.png'), os.path.join(D, f'pausa_{k:02d}_1_0.png')
        if not os.path.exists(b):
            continue
        A, B = Image.open(a).convert('RGB'), Image.open(b).convert('RGB')
        W, H = A.size
        w, h = 1152, 1152 * H // W
        m = Image.new('RGB', (2 * w, h + 600))
        m.paste(A.resize((w, h)), (0, 0))
        m.paste(B.resize((w, h)), (w, 0))
        box = (W // 2 - 576, H // 2 - 300, W // 2 + 576, H // 2 + 300)
        m.paste(A.crop(box), (0, h))
        m.paste(B.crop(box), (w, h))
        dr = ImageDraw.Draw(m)
        dr.text((10, 10), f'original - {NAMES.get(k, k)}', fill=(255, 255, 0))
        dr.text((w + 10, 10), 'remake (fase 6)', fill=(255, 255, 0))
        m.save(os.path.join(D, f'palco_{k:02d}_ab.png'))
        sheet.append(m.resize((m.width // 3, m.height // 3)))
    if sheet:
        cols = 2
        sw, sh = sheet[0].size
        S = Image.new('RGB', (cols * sw, ((len(sheet) + cols - 1) // cols) * sh))
        for i, im in enumerate(sheet):
            S.paste(im, ((i % cols) * sw, (i // cols) * sh))
        S.save(os.path.join(D, 'palcos_ab.png'))
    print(len(sheet), 'palcos')


if __name__ == '__main__':
    main()
