#!/usr/bin/env python3
"""Expressão do rosto dos outros personagens (fase 4; build-debug, TCP 4370).

Lê `heat_chars` a cada ~0,3 s; quando um personagem tem casas do rosto remapeadas (o jogo copiou olhos/boca na VRAM
com MoveImage e o plugin converteu para as páginas dele), tira uma captura no modo 3 (original à esquerda, remake à
direita) e recorta a parte de cima do retângulo dele (cabeça), dos dois. Um recorte por número de casas visto.
Uso: python tools/tests/chars_face.py [segundos]   (BAG2_MATCH; GAME_TCP=1 usa o jogo já aberto)
Saída: logs/remake/jogo/face_<personagem>_<k>.png
"""
from __future__ import annotations

import os
import re
import sys
import time

from PIL import Image

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'remake'))
from game_session import LOGS, Session  # noqa: E402


def insts(s):
    txt = s.cmd({'cmd': 'mod_cmd', 'name': 'heat_chars', 'args': ''}).get('reply', '')
    out = []
    for part in txt.split('||'):
        m = re.search(r'jogo: (.+?) raiz=\w+ ha=(\d+) .*paginas=(\w+).*bbox=([-\d.]+),([-\d.]+),([-\d.]+),([-\d.]+) casas=(\d+)', part)
        if m and int(m.group(2)) < 8:
            out.append((m.group(1), m.group(3), [float(m.group(k)) for k in range(4, 8)], int(m.group(8))))
    return out


def main():
    secs = float(sys.argv[1]) if len(sys.argv) > 1 else 60.0
    live = os.environ.get('GAME_TCP') == '1'
    s = Session(label='chars_face')
    if not live:
        s.start()
    seen = {}
    try:
        if not live:
            s.enter_match()
        t0 = time.time()
        while time.time() - t0 < secs:
            for name, pages, (x0, y0, x1, y1), nc in insts(s):
                key = (name, nc)
                if nc and key not in seen:
                    s.cmd({'cmd': 'mod_cmd', 'name': 'heat_mode', 'args': '3'})
                    time.sleep(0.25)
                    p = os.path.join(LOGS, 'face_tmp.png')
                    s.shot(p)
                    after = {n: (b, c) for n, _, b, c in insts(s)}
                    s.cmd({'cmd': 'mod_cmd', 'name': 'heat_mode', 'args': '1'})
                    if name not in after:
                        continue
                    (x0, y0, x1, y1), c2 = after[name]
                    w = (x1 - x0) * 1.05
                    hgt = (y1 - y0) * 0.33
                    im = Image.open(p).convert('RGB')
                    crop = im.crop((int(max(0, x0 - w) * 9), int(max(0, y0 - 4) * 9), int(min(512, x1) * 9),
                                    int(min(240, y0 + hgt) * 9)))
                    f = os.path.join(LOGS, f'face_{name.replace("&", "").replace(" ", "_").replace("*", "_alt")}_{len([k for k in seen if k[0] == name])}.png')
                    crop.save(f)
                    seen[key] = f
                    print(f'{name}: páginas {pages}, casas {nc} -> {f}', flush=True)
            time.sleep(0.3)
        print('vistos:', {k: v for k, v in seen.items()})
    finally:
        if not live:
            s.stop()


if __name__ == '__main__':
    main()
