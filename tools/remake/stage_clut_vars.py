#!/usr/bin/env python3
"""Variantes de CLUT animada por MoveImage (fase 6b, segunda rodada; build-debug, TCP 4370, 2 jogadores).

Parte das texturas animadas dos palcos troca só a CLUT: o jogo copia (MoveImage) uma linha de um banco de paletas na
VRAM para a CLUT da chave, no tempo da música (as setas do 232, os letreiros do 233, as luzes da discoteca). Com uma
camada HD por linha do banco, o caminho novo desenha essas faces com a paleta certa (o plugin troca a camada a cada
MoveImage de uma origem conhecida: `.stg` v2, src/bag2_stage.c, dyn_rect). Aqui, para cada palco, HEAT x COMET:
`stage_geo v` lista os MoveImage da partida (`segundos` depois da carga); as cópias de uma linha (16 ou 256 cores) para
uma CLUT dão as origens, e cada linha de origem é lida da VRAM (`vram_peek`). Grava
logs/remake/palcos/vram/<container>/clut_var.json (dados do jogo, fora do git):
  {"<clut hex>": {"w": 16|256, "src": [[x, y, [cores...]], ...]}}
tools/remake/export_stage.py monta uma camada por origem, se couber no limite de 64.
Uso: python tools/remake/stage_clut_vars.py [posições] [segundos]
"""
from __future__ import annotations

import json
import os
import re
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from game_session import Session  # noqa: E402
from stage_vram_keys import OUT, peek  # noqa: E402


def main():
    stages = [int(x) for x in (sys.argv[1] if len(sys.argv) > 1 else ','.join(map(str, range(11)))).split(',')]
    secs = float(sys.argv[2]) if len(sys.argv) > 2 else 115.0   # a música inteira (até ~2 min depois da carga)
    if 10 in stages:
        os.environ['BAG2_UNLOCK'] = '1'
    s = Session(label='palcos_clut_var')
    s.start()
    g = lambda a: s.cmd({'cmd': 'mod_cmd', 'name': 'stage_geo', 'args': a}).get('reply', '')
    try:
        for i, k in enumerate(stages):
            if i == 0:
                s.to_match_2p('HEAT', 'COMET', k)
            else:
                s.menu_to_match_2p('HEAT', 'COMET', k)
            time.sleep(8)
            g('vz')
            m = re.search(r'atual=\d+ \([^)]*palco(\d+)\.bin', g(''))   # agora: no fim da música o palco descarrega
            time.sleep(secs)
            if not m:
                print(k, 'sem palco pronto', flush=True)
                s.quit_match()
                continue
            c = int(m.group(1))
            moves = [tuple(int(v) for v in t) for t in
                     re.findall(r'(-?\d+),(-?\d+)<-(-?\d+),(-?\d+) (\d+)x(\d+)/(\d+)', g('v'))]
            out = {}
            for dx, dy, sx, sy, w, h, n in moves:
                if h != 1 or w not in (16, 256) or dx % 16:
                    continue
                cw = (dy << 6) | (dx >> 4)
                e = out.setdefault(f'{cw:04X}', {'w': w, 'src': []})
                if any(a == sx and b == sy for a, b, _ in e['src']):
                    continue
                e['src'].append([sx, sy, [int(v) for v in peek(s, sx, sy, w, 1)[0]]])
            os.makedirs(os.path.join(OUT, str(c)), exist_ok=True)
            with open(os.path.join(OUT, str(c), 'clut_var.json'), 'w', encoding='utf-8') as f:
                json.dump(out, f)
            print(k, c, {cw: len(e['src']) for cw, e in out.items()}, flush=True)
            s.quit_match()
    finally:
        s.stop()


if __name__ == '__main__':
    main()
