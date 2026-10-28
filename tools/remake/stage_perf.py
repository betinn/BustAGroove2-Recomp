#!/usr/bin/env python3
"""Desempenho da geometria do palco em todos os palcos (fase 6, peça 4; build-debug, TCP 4370, 2 jogadores).

Uma sessão de tools/remake/game_perf.py por palco (HEAT x COMET, posição da tela de palco), duas janelas de 60 s:
base (stage_geo 0: o estado da fase 5, palco original com o pós-efeito) e tudo (stage_geo 1), com a ordem trocada de
um palco para o outro. Os logs ficam em tools/recomp/logs/remake/jogo/perf6_<posição>.log e o resumo das linhas de
cada janela em tools/recomp/logs/remake/palcos/perf6.txt.
Uso: python tools/remake/stage_perf.py [posições separadas por vírgula] [segundos]
     (a posição 10, o palco 238, só existe com os 18 personagens: o script liga BAG2_UNLOCK nela)
"""
from __future__ import annotations

import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, '..', '..'))
OUT = os.path.join(REPO, 'tools', 'recomp', 'logs', 'remake', 'palcos', 'perf6.txt')
BASE = '1 4 0.6 0.25 2;1 0;0'
ALL = '1 4 0.6 0.25 2;1 0;1'


def main():
    stages = [int(x) for x in (sys.argv[1] if len(sys.argv) > 1 else ','.join(map(str, range(11)))).split(',')]
    secs = sys.argv[2] if len(sys.argv) > 2 else '60'
    for i, k in enumerate(stages):
        order = f'{BASE}|{ALL}' if i % 2 == 0 else f'{ALL}|{BASE}'
        env = dict(os.environ, BAG2_MATCH=f'HEAT COMET {k}')
        if k >= 10:
            env['BAG2_UNLOCK'] = '1'
        env['PYTHONIOENCODING'] = 'utf-8'
        r = subprocess.run([sys.executable, os.path.join(HERE, 'game_perf.py'), f'perf6_{k}', order, secs],
                           env=env, capture_output=True, text=True, encoding='utf-8', errors='replace')
        lines = [l for l in r.stdout.splitlines() if l.startswith('[') or 'underruns' in l or 'geometria do palco' in l
                 or 'partida:' in l]
        with open(OUT, 'a', encoding='utf-8') as f:
            f.write(f'== posição {k}\n' + '\n'.join(lines) + '\n')
        print(k, '\n'.join(l[:160] for l in lines), flush=True)


if __name__ == '__main__':
    main()
