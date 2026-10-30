#!/usr/bin/env python3
"""Texturas das faces livres que não estão nos TIMs do palco, lidas da VRAM na partida (fase 6b; build-debug, TCP 4370).

stage_keys.py lista as texturas (página, CLUT) que a geometria do código do overlay usa e o asset ainda não tem; o
exportador só monta camada quando a página e a CLUT estão nos TIMs do palco. As outras (CLUT gerada pelo overlay, página
montada em tempo de execução) são lidas aqui da VRAM do jogo (`vram_peek`): para cada palco, HEAT x COMET, depois de
`segundos` na partida, a CLUT de cada chave em três leituras 1,5 s uma da outra (mudou = textura animada, fica com o
jogo) e a página inteira. Grava em logs/remake/palcos/vram/<container>/<tpage>_<clut>.npz (dados do jogo,
fora do git): `page` (halfwords da VRAM da página), `clut`, `animated`. tools/remake/export_stage.py usa os que não
são animados. Chaves que aparecem em 5 palcos ou mais são texturas do jogo (efeitos), não do palco: ficam de fora.
Uso: python tools/remake/stage_vram_keys.py [posições] [segundos]
"""
from __future__ import annotations

import json
import os
import re
import sys
import time

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from game_session import Session, REPO  # noqa: E402

OUT = os.path.join(REPO, 'logs', 'remake', 'palcos', 'vram')


def peek(s, x, y, w, h):
    out = np.zeros((h, w), np.uint16)
    for y0 in range(0, h, 128):
        for x0 in range(0, w, 128):
            ww, hh = min(128, w - x0), min(128, h - y0)
            r = s.cmd({'cmd': 'vram_peek', 'x': x + x0, 'y': y + y0, 'w': ww, 'h': hh}, 20)
            v = np.frombuffer(bytes.fromhex(r['hex']), dtype='>u2').reshape(hh, ww)
            out[y0:y0 + hh, x0:x0 + ww] = v
    return out


def main():
    stages = [int(x) for x in (sys.argv[1] if len(sys.argv) > 1 else ','.join(map(str, range(11)))).split(',')]
    secs = float(sys.argv[2]) if len(sys.argv) > 2 else 25.0
    if 10 in stages:
        os.environ['BAG2_UNLOCK'] = '1'
    s = Session(label='palcos_vram')
    s.start()
    g = lambda a: s.cmd({'cmd': 'mod_cmd', 'name': 'stage_geo', 'args': a}).get('reply', '')
    seen = {}
    try:
        for i, k in enumerate(stages):
            if i == 0:
                s.to_match_2p('HEAT', 'COMET', k)
            else:
                s.menu_to_match_2p('HEAT', 'COMET', k)
            g('kz')   # a lista de faltas é deste palco
            time.sleep(secs)
            m = re.search(r'atual=\d+ \([^)]*palco(\d+)\.bin', g(''))   # o 's' corta a lista quando muitos palcos já passaram
            if not m:
                print(k, 'sem palco pronto', flush=True)
                s.quit_match()
                continue
            c = int(m.group(1))
            keys = [(int(a, 16), int(b, 16), int(n)) for a, b, n in re.findall(r'([0-9A-F]{3})/([0-9A-F]{4})/(\d+)', g('kz'))]
            os.makedirs(os.path.join(OUT, str(c)), exist_ok=True)
            for tp, cw, n in keys:
                bpp = {0: 4, 1: 8, 2: 16}[(tp >> 7) & 3]
                cx, cy = (cw & 0x3F) * 16, (cw >> 6) & 0x1FF
                nc = 16 if bpp == 4 else 256
                cl = [peek(s, cx, cy, nc, 1)[0] for _ in range(1)]
                for _ in range(2):
                    time.sleep(1.5)
                    cl.append(peek(s, cx, cy, nc, 1)[0])
                animated = any((c_ != cl[0]).any() for c_ in cl[1:]) if bpp != 16 else False
                tx, ty = (tp & 0xF) * 64, ((tp >> 4) & 1) * 256
                page = peek(s, tx, ty, 256 * bpp // 16, 256)
                np.savez_compressed(os.path.join(OUT, str(c), f'{tp:03x}_{cw:04x}.npz'), page=page, clut=cl[0],
                                    animated=animated, bpp=bpp, n=n)
                seen.setdefault((tp, cw), set()).add(c)
                print(k, c, f'{tp:03X}/{cw:04X}', 'animada' if animated else 'estática', n, flush=True)
            s.quit_match()
    finally:
        s.stop()
    common = sorted(f'{tp:03X}/{cw:04X}' for (tp, cw), cs in seen.items() if len(cs) >= 5)
    with open(os.path.join(OUT, 'resumo.json'), 'w', encoding='utf-8') as f:
        json.dump({'comuns': common, 'por_chave': {f'{tp:03X}/{cw:04X}': sorted(cs) for (tp, cw), cs in seen.items()}},
                  f, indent=1)
    print('comuns (efeitos do jogo):', common, flush=True)


if __name__ == '__main__':
    main()
