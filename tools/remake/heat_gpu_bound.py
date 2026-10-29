#!/usr/bin/env python3
"""Pendência da fase 5: o segundo dançarino com remake sobe a render (+9/+10) por espera pela GPU? (fase 6b; build-debug,
TCP 4370, 2 jogadores, palco do HEAT com o palco original como na medição da fase 5).

O carimbo de GPU não separa trabalho de espera (o intervalo inclui o tempo até os comandos chegarem quando a GPU tem
folga). Aqui o teste é pela resolução: se a diferença entre "só o HEAT com remake" e "os dois com remake" vem de
preencher pixels na GPU, ela cai quando a resolução interna cai (9x -> 4x: 1/5 dos pixels); se vem da CPU
da thread de render, fica. Na mesma partida, para cada escala, janelas curtas alternadas A B A B (render_busy médio
das linhas por segundo do log). A escala muda ao vivo pelo `video_set` (linhas internas: 9x = 2160, 4x = 960), que grava
a escolha nas configurações: o script guarda e devolve settings.toml e game.toml da build-debug.
Saída em tools/recomp/logs/remake/palcos/heat_gpu_bound.txt.
Uso: python tools/remake/heat_gpu_bound.py [escalas, ex. 9,4] [segundos por janela] [repetições]
"""
from __future__ import annotations

import os
import re
import shutil
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from game_session import LOGS, Session, REPO  # noqa: E402

OUT = os.path.join(REPO, 'tools', 'recomp', 'logs', 'remake', 'palcos', 'heat_gpu_bound.txt')
VARS = {'só o HEAT': '1 4 0.6 0.25 2050', 'os dois': '1 4 0.6 0.25 2'}


def lines(s):
    with open(os.path.join(LOGS, f'{s.label}.log'), errors='replace') as f:
        return [l for l in f if 'render_busy=' in l]


def main():
    scales = [int(x) for x in (sys.argv[1] if len(sys.argv) > 1 else '9,4').split(',')]
    secs = float(sys.argv[2]) if len(sys.argv) > 2 else 8.0
    reps = int(sys.argv[3]) if len(sys.argv) > 3 else 4
    bd = os.path.join(REPO, 'tools', 'recomp', 'bag2', 'build-debug')
    saved = {f: open(os.path.join(bd, f), 'rb').read() for f in ('settings.toml', 'game.toml') if os.path.exists(os.path.join(bd, f))}
    s = Session(label='heat_gpu_bound')
    s.start()
    log = open(OUT, 'a', encoding='utf-8')
    mod = lambda name, args: s.cmd({'cmd': 'mod_cmd', 'name': name, 'args': args}).get('reply', '')
    try:
        s.enter_match()
        mod('stage_geo', '0')
        mod('stage_post', '1 0')
        time.sleep(4)
        for sc in scales:
            print('video_set', sc, s.cmd({'cmd': 'video_set', 'internal_resolution': 240 * sc}), flush=True)
            time.sleep(3)
            acc = {k: [] for k in VARS}
            for r in range(reps):
                for name, args in VARS.items():
                    mod('heat_mode', args)
                    time.sleep(1.5)
                    n0 = len(lines(s))
                    time.sleep(secs)
                    ls = lines(s)[n0:]
                    rb = [float(m.group(1)) for l in ls for m in [re.search(r'render_busy=([\d.]+)%', l)] if m]
                    eb = [float(m.group(1)) for l in ls for m in [re.search(r'emu_busy=([\d.]+)%', l)] if m]
                    if rb:
                        acc[name].append((sum(rb) / len(rb), sum(eb) / len(eb)))
            m = {k: (sum(x[0] for x in v) / len(v), sum(x[1] for x in v) / len(v)) for k, v in acc.items() if v}
            a, b = m['só o HEAT'], m['os dois']
            line = (f'escala {sc}x: render só o HEAT {a[0]:.1f} %, os dois {b[0]:.1f} % (Δ {b[0] - a[0]:+.1f}); '
                    f'jogo {a[1]:.1f} -> {b[1]:.1f} %; {reps} janelas de {secs:.0f} s por variante')
            print(line, flush=True)
            log.write(line + '\n')
        s.cmd({'cmd': 'video_set', 'internal_resolution': 2160})
    finally:
        log.close()
        s.stop()
        for f, b in saved.items():   # a escala escolhida volta às configurações
            with open(os.path.join(bd, f), 'wb') as fh:
                fh.write(b)


if __name__ == '__main__':
    main()
