#!/usr/bin/env python3
"""Capturas dos personagens com remake, original x remake ou A/B no mesmo quadro (fase 4; build-debug, TCP 4370).

Para cada momento e cada variante (args do heat_mode), tira uma screenshot_hires (4608x2160) e recorta cada
personagem da partida pelo retângulo que o plugin relata (`heat_chars`): no modo 3 o remake fica à direita do
original; nos modos 4..6 as cópias (com flags ^ o 7º número) ficam à direita do remake. Monta uma grade por
personagem em tools/recomp/logs/remake/jogo/<rótulo>_<personagem>.png.
Uso: python tools/remake/game_chars.py <rótulo> ["variante|variante"] [momentos] [intervalo_s]
     ex.: game_chars.py lado "3"            (original x remake)
          game_chars.py semi "4 4 0.6 0.25 2 3300 1024"   (remake x remake sem semitransparência)
Rota: BAG2_MATCH (2 jogadores, padrão HEAT x COMET no palco do HEAT). Com GAME_TCP=1 usa o jogo já aberto
(game_tcp.py start) e não fecha no fim.
"""
from __future__ import annotations

import os
import re
import sys
import time

from PIL import Image

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from game_session import LOGS, Session  # noqa: E402


def instances(s):
    txt = s.cmd({'cmd': 'mod_cmd', 'name': 'heat_chars', 'args': ''}).get('reply', '')
    out = []
    for part in txt.split('||'):
        m = re.search(r'jogo: (\S+) raiz=\w+ ha=(\d+) .*bbox=([-\d.]+),([-\d.]+),([-\d.]+),([-\d.]+)', part)
        if m and int(m.group(2)) < 8:
            out.append((m.group(1), [float(m.group(k)) for k in range(3, 7)]))
    return out


def main():
    tag = sys.argv[1] if len(sys.argv) > 1 else 'chars'
    variants = (sys.argv[2] if len(sys.argv) > 2 else '3').split('|')
    moments = int(sys.argv[3]) if len(sys.argv) > 3 else 4
    gap = float(sys.argv[4]) if len(sys.argv) > 4 else 12.0
    live = os.environ.get('GAME_TCP') == '1'
    s = Session(label=f'chars_{tag}')
    if not live:
        s.start()
    crops = {}
    try:
        if not live:
            s.enter_match()
            time.sleep(4)
        for k in range(moments):
            for vi, v in enumerate(variants):
                s.cmd({'cmd': 'mod_cmd', 'name': 'heat_mode', 'args': v})
                time.sleep(0.3)
                p = os.path.join(LOGS, f'{tag}_{k}_{vi}.png')
                s.shot(p)
                insts = instances(s)
                im = Image.open(p).convert('RGB')
                mode = int(v.split()[0])
                copies = mode - 3 if mode >= 4 else 0
                for name, (x0, y0, x1, y1) in insts:
                    w = (x1 - x0) * 1.05
                    if mode == 3:
                        x0 -= w
                    else:
                        x1 = x0 + w * (copies + 1)
                    box = [int(max(0, (x0 - 6) * 9)), int(max(0, (y0 - 6) * 9)), int(min(512, x1 + 6) * 9),
                           int(min(240, y1 + 6) * 9)]
                    if box[2] - box[0] < 50 or box[3] - box[1] < 50:
                        continue
                    crops.setdefault(name, []).append(im.crop(box))
            print(f'momento {k}: ' + ', '.join(n for n, _ in instances(s)), flush=True)
            time.sleep(gap)
        s.cmd({'cmd': 'mod_cmd', 'name': 'heat_mode', 'args': '1 4 0.6 0.25 2 3300 0'})
        for name, cs in crops.items():
            hgt = 900
            cs = [c.resize((max(1, c.size[0] * hgt // c.size[1]), hgt)) for c in cs]
            cols = len(variants)
            rows = (len(cs) + cols - 1) // cols
            W = max(sum(c.size[0] for c in cs[r * cols:(r + 1) * cols]) for r in range(rows))
            grid = Image.new('RGB', (W, hgt * rows))
            for r in range(rows):
                x = 0
                for c in cs[r * cols:(r + 1) * cols]:
                    grid.paste(c, (x, r * hgt)); x += c.size[0]
            out = os.path.join(LOGS, f'{tag}_{name.replace("&", "").replace("*", "_alt")}.png')
            grid.save(out)
            print(out, grid.size, flush=True)
        print(s.fps())
    finally:
        if not live:
            s.stop()


if __name__ == '__main__':
    main()
