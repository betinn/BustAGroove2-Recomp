#!/usr/bin/env python3
"""A/B do pós-efeito do palco no jogo (fase 3; build-debug, TCP 4370).

Entra numa partida e, em alguns momentos, tira para cada variante de `stage_post` (args do comando separados
por |) uma present_shot (a janela, com a tela larga) e uma screenshot_hires (superfície canônica 4:3 em 9x).
Com a flag 8 (SPLIT) o mesmo quadro mostra o original à esquerda e o efeito à direita.
Também confere que nenhum marcador sai antes da partida (menus/FMV intactos).
Saída: logs/remake/palco/<rótulo>_<momento>_<variante>.png
Uso: python tools/tests/stage_ab.py <rótulo> "<var1>|<var2>" [momentos] [intervalo_s]
     ex.: python tools/tests/stage_ab.py ab1 "1 8|1 0|0" 4 6
"""
from __future__ import annotations

import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'remake'))
from game_session import Session, REPO  # noqa: E402
from stage_beat import present_shot  # noqa: E402

OUT = os.path.join(REPO, 'logs', 'remake', 'palco')


def counter(s, name):
    for c in s.cmd({'cmd': 'mod_counters'})['counters']:
        if c['name'] == name:
            return c['count']
    return 0


def main():
    tag = sys.argv[1] if len(sys.argv) > 1 else 'ab'
    variants = (sys.argv[2] if len(sys.argv) > 2 else '1 8').split('|')
    moments = int(sys.argv[3]) if len(sys.argv) > 3 else 3
    gap = float(sys.argv[4]) if len(sys.argv) > 4 else 6.0
    os.makedirs(OUT, exist_ok=True)
    s = Session(label=f'palco_{tag}')
    s.start()
    try:
        s.at(74)
        print('marcadores antes da partida:', counter(s, 'bag2_stage.markers'))
        s.enter_match()
        print('marcadores na partida:', counter(s, 'bag2_stage.markers'))
        for k in range(moments):
            time.sleep(gap)
            for i, v in enumerate(variants):
                r = s.cmd({'cmd': 'mod_cmd', 'name': 'stage_post', 'args': v})
                time.sleep(0.12)
                present_shot(s, os.path.join(OUT, f'{tag}_{k}_{i}_janela.png'))
                s.shot(os.path.join(OUT, f'{tag}_{k}_{i}_hr.png'))
                if k == 0:
                    print(v, '->', r.get('reply', r))
            print('stats', s.cmd({'cmd': 'mod_cmd', 'name': 'stage_stats', 'args': ''}).get('reply'))
        s.cmd({'cmd': 'mod_cmd', 'name': 'stage_post', 'args': variants[0]})
        print(s.cmd({'cmd': 'mod_cmd', 'name': 'stage_post', 'args': ''}).get('reply'))
        print(s.fps())
    finally:
        s.stop()


if __name__ == '__main__':
    main()
