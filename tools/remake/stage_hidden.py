#!/usr/bin/env python3
"""Palcos fora da tela de palco do 2 jogadores (containers 241 e 245; fase 6b; build-debug, TCP 4370).

O índice do palco escolhido fica em 0x8005FB56 (byte; cópias em 0x8008A308 e 0x8008A350 na partida): 0 = 234, 1 = 243,
2 = 233, 3 = 244, 4 = 236, 5 = 242, 6 = 238, 7 = 237, 8 = 235, 9 = 231, 10 = 232, 11 = 241, 12 = 245 (achado comparando a
RAM das partidas do inventário, stage_inventory.py; 13 não tem palco). A tela de palco só oferece 0..10; com
BAG2_STAGE_IDX o game_session escreve outro índice em laço logo depois de confirmar o palco; o script vê qual overlay o
jogo carrega e qual asset do remake o plugin reconhece; depois faz o A/B no mesmo
quadro (savestate, como stage_geo_pause.py) e grava pausa_x<índice>_<variante>.png.
Uso: python tools/remake/stage_hidden.py [índices separados por vírgula] [segundos até a pausa]
"""
from __future__ import annotations

import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from game_session import Session, REPO  # noqa: E402
from stage_geo_pause import montage  # noqa: E402

OUT = os.path.join(REPO, 'tools', 'recomp', 'logs', 'remake', 'palcos')
from game_session import STAGE_IDX as SEL  # noqa: E402


def main():
    ids = [int(x) for x in (sys.argv[1] if len(sys.argv) > 1 else '11,12').split(',')]
    wait = float(sys.argv[2]) if len(sys.argv) > 2 else 30.0
    s = Session(label='palcos_ocultos')
    s.start()
    g = lambda a: s.cmd({'cmd': 'mod_cmd', 'name': 'stage_geo', 'args': a}).get('reply', '')
    try:
        for i, k in enumerate(ids):
            os.environ['BAG2_STAGE_IDX'] = str(k)   # game_session escreve o índice depois da confirmação
            if i == 0:
                s.to_match_2p('HEAT', 'COMET', 0)
            else:
                s.menu_to_match_2p('HEAT', 'COMET', 0)
            rd = lambda a: s.cmd({'cmd': 'read_ram', 'addr': f'{a:08X}', 'len': 1})['hex']
            print(k, 'índice na RAM:', [rd(a) for a in SEL], 'partida:', s.who(), flush=True)
            time.sleep(wait)
            st = g('s')
            print(k, 'palco:', st[st.find('palco2'):][:400], flush=True)
            s.cmd({'cmd': 'savestate', 'slot': 9, 'op': 'save'})
            time.sleep(0.5)
            s.cmd({'cmd': 'savestate', 'slot': 9, 'op': 'load'})
            time.sleep(1.0)
            paths = []
            for v in ('0', '1 0', '1 256'):
                g(v)
                s.cmd({'cmd': 'savestate', 'slot': 9, 'op': 'load'})
                f0 = s.cmd({'cmd': 'frame'}).get('frame', 0)
                while s.cmd({'cmd': 'frame'}).get('frame', 0) < f0 + 60:
                    time.sleep(0.005)
                p = os.path.join(OUT, f'pausa_x{k:02d}_{v.replace(" ", "_")}.png')
                s.shot(p)
                paths.append(p)
                st = g('sz')
                print(k, v, st[:200], flush=True)
            g('1 0')
            montage(paths, os.path.join(OUT, f'pausa_x{k:02d}.png'))
            s.quit_match()
    finally:
        s.stop()


if __name__ == '__main__':
    main()
