#!/usr/bin/env python3
"""Capturas do HEAT ORIGINAL e REMAKE no MESMO quadro do jogo (heat_mode 3: o plugin desenha o remake ao
lado do original, deslocado na horizontal). Mesma pose, mesma luz do palco, mesma câmera.

Uso: python tools/remake/game_sidebyside.py [rótulo] [args do heat_mode sem o modo] [game.toml] [momentos]
     ex.: python tools/remake/game_sidebyside.py lado "4 0.6 0.12 0" game-nort.toml 6
Saída: tools/recomp/logs/remake/jogo/<rótulo>_<k>.png (recorte) e <rótulo>_<k>_tela.png (tela inteira reduzida).
"""
from __future__ import annotations

import os
import re
import sys
import time

from PIL import Image, ImageDraw, ImageFont

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from game_session import LOGS, Session  # noqa: E402


def main():
    tag = sys.argv[1] if len(sys.argv) > 1 else 'lado'
    params = sys.argv[2] if len(sys.argv) > 2 else '4 0.6 0.12 0'
    game = sys.argv[3] if len(sys.argv) > 3 else 'game.toml'
    moments = int(sys.argv[4]) if len(sys.argv) > 4 else 4
    s = Session(label=tag, game=game)
    s.start()
    out = []
    try:
        s.to_match()
        s.cmd({'cmd': 'mod_cmd', 'name': 'heat_mode', 'args': '3 ' + params})
        for k in range(moments):
            time.sleep(3)
            r = s.cmd({'cmd': 'mod_cmd', 'name': 'heat_mode', 'args': '3 ' + params})['reply']
            x0, y0, x1, y1 = map(float, re.search(r'bbox=([-\d.,]+)', r).group(1).split(','))
            p = os.path.join(LOGS, f'{tag}_{k}_full.png')
            s.shot(p)
            out.append((p, (x0, y0, x1, y1)))
        s.cmd({'cmd': 'mod_cmd', 'name': 'heat_mode', 'args': '1 ' + params})
        print(s.fps())
    finally:
        s.stop()
    try:
        font = ImageFont.truetype('C:/Windows/Fonts/segoeuib.ttf', 34)
    except OSError:
        font = ImageFont.load_default()
    for k, (p, (x0, y0, x1, y1)) in enumerate(out):
        im = Image.open(p)
        sc = im.width / 512.0
        w = (x1 - x0)
        left = x0 - w * 1.05 - w * 0.15          # original fica uma largura à esquerda do remake
        box = (int(left * sc), int((y0 - (y1 - y0) * 0.06) * sc), int((x1 + w * 0.15) * sc), int((y1 + (y1 - y0) * 0.04) * sc))
        c = im.crop(box)
        c = c.resize((int(c.width * 1100 / c.height), 1100), Image.LANCZOS)
        d = ImageDraw.Draw(c)
        d.text((16, 10), 'ORIGINAL', fill=(255, 255, 255), font=font, stroke_width=3, stroke_fill=(0, 0, 0))
        d.text((c.width // 2 + 16, 10), 'REMAKE', fill=(255, 255, 255), font=font, stroke_width=3, stroke_fill=(0, 0, 0))
        fn = os.path.join(LOGS, f'{tag}_{k}.png')
        c.save(fn)
        im.resize((im.width // 3, im.height // 3), Image.LANCZOS).save(os.path.join(LOGS, f'{tag}_{k}_tela.png'))
        os.remove(p)
        print(fn)


if __name__ == '__main__':
    main()
