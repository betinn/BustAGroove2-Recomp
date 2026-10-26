#!/usr/bin/env python3
"""Capturas ORIGINAL x REMAKE do HEAT dentro do jogo (build-debug, TCP 4370).

Entra numa partida e, em alguns momentos da dança, alterna o plugin entre as variantes pedidas
(argumentos do comando heat_mode; "0" = desenho original do jogo), tira screenshot_hires de cada uma e
monta recortes lado a lado em tools/recomp/logs/remake/jogo/<rótulo>_comparacao.png.
Os quadros de um mesmo momento ficam a ~0,3 s um do outro (a dança não para): a pose difere um pouco.
Uso: python tools/remake/game_compare.py [rótulo] [variantes separadas por |] [game.toml] [momentos]
     ex.: python tools/remake/game_compare.py cmp "0|1 4 0.6 0.12" game-nort.toml 4
"""
from __future__ import annotations

import os
import re
import sys
import time

from PIL import Image, ImageDraw, ImageFont

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from game_session import LOGS, Session  # noqa: E402


def label(img, text):
    d = ImageDraw.Draw(img)
    try:
        f = ImageFont.truetype('C:/Windows/Fonts/segoeuib.ttf', max(18, img.width // 22))
    except OSError:
        f = ImageFont.load_default()
    d.text((12, 8), text, fill=(240, 240, 240), font=f, stroke_width=2, stroke_fill=(0, 0, 0))
    return img


def name_of(v):
    return 'ORIGINAL (jogo)' if v.strip() == '0' else f'REMAKE ({v.strip()})'


def main():
    tag = sys.argv[1] if len(sys.argv) > 1 else 'cmp'
    variants = (sys.argv[2] if len(sys.argv) > 2 else '0|1 4 0.6 0.12').split('|')
    game = sys.argv[3] if len(sys.argv) > 3 else 'game.toml'
    moments = int(sys.argv[4]) if len(sys.argv) > 4 else 4
    s = Session(label=tag, game=game)
    s.start()
    shots = []
    try:
        s.to_match()
        for k in range(moments):
            time.sleep(4)
            remake = next((v for v in variants if v.strip() != '0'), '1')
            s.cmd({'cmd': 'mod_cmd', 'name': 'heat_mode', 'args': remake})
            time.sleep(0.15)
            r = s.cmd({'cmd': 'mod_cmd', 'name': 'heat_mode', 'args': remake})['reply']
            bb = list(map(float, re.search(r'bbox=([-\d.,]+)', r).group(1).split(',')))
            row = []
            for i, v in enumerate(variants):
                s.cmd({'cmd': 'mod_cmd', 'name': 'heat_mode', 'args': v})
                time.sleep(0.1)
                p = os.path.join(LOGS, f'{tag}_{k}_{i}.png')
                s.shot(p)
                row.append((p, name_of(v)))
            shots.append((row, bb))
        print(s.fps())
    finally:
        s.stop()
    rows = []
    for row, (x0, y0, x1, y1) in shots:
        crops = []
        for p, name in row:
            im = Image.open(p)
            sc = im.width / 512.0
            mx, my = (x1 - x0) * 0.25 + 6, (y1 - y0) * 0.08 + 6
            c = im.crop((int((x0 - mx) * sc), int((y0 - my) * sc), int((x1 + mx) * sc), int((y1 + my) * sc)))
            h = 900
            crops.append(label(c.resize((max(1, int(c.width * h / c.height)), h), Image.LANCZOS), name))
            im.resize((im.width // 4, im.height // 4), Image.LANCZOS).save(p[:-4] + '_s.png')
            os.remove(p)
        r = Image.new('RGB', (sum(c.width for c in crops) + 6 * (len(crops) - 1), 900))
        x = 0
        for c in crops:
            r.paste(c, (x, 0)); x += c.width + 6
        rows.append(r)
        r.save(os.path.join(LOGS, f'{tag}_momento{len(rows) - 1}.png'))
    print('\n'.join(os.path.join(LOGS, f'{tag}_momento{i}.png') for i in range(len(rows))))


if __name__ == '__main__':
    main()
