#!/usr/bin/env python3
"""Tempo de GPU do remake por variante, na mesma partida (fase 6b; build-debug, TCP 4370, 2 jogadores).

O render_busy do game_perf.py varia uns 5 pontos entre janelas de 60 s da mesma variante (o fim da música pesa mais),
o que esconde diferenças pequenas. Aqui as variantes alternam em janelas curtas (A B A B ...) na mesma partida e os
plugins medem o tempo de GPU dos desenhos de host com carimbos GL_TIMESTAMP (gpu_timers 1): o do palco separado entre
a superfície canônica e o espelho da tela larga (stage_geo s), e o dos dançarinos, corpo e sombra no chão (heat_stats);
mais as faces livres e os pacotes sem par do palco por OT.
Variante: como no game_perf.py, "args do heat_mode;args do stage_post;args do stage_geo" (parte vazia = não muda).
Saída: uma linha por janela e a média por variante, em tools/recomp/logs/remake/palcos/gpu_ab.txt.
Uso: python tools/remake/stage_gpu_ab.py [posições] ["varA|varB"] [segundos por janela] [janelas por variante]
     ex.: python tools/remake/stage_gpu_ab.py 8 ";;1 0|;;1 2048" 10 4     (recorte do centro no espelho)
          python tools/remake/stage_gpu_ab.py 0 "1 4 0.6 0.25 2050;;0|1 4 0.6 0.25 2;;0" 10 4   (só o HEAT x os dois)
"""
from __future__ import annotations

import os
import re
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from game_session import Session, REPO  # noqa: E402

OUT = os.path.join(REPO, 'tools', 'recomp', 'logs', 'remake', 'palcos', 'gpu_ab.txt')
COLS = ('palco canônica', 'palco espelho', 'corpos', 'chão', 'faces livres', 'sem par')


def num(rx, s):
    m = re.search(rx, s)
    return float(m.group(1)) if m else 0.0


def main():
    stages = [int(x) for x in (sys.argv[1] if len(sys.argv) > 1 else '8').split(',')]
    variants = (sys.argv[2] if len(sys.argv) > 2 else ';;1 0|;;1 2048').split('|')
    secs = float(sys.argv[3]) if len(sys.argv) > 3 else 10.0
    reps = int(sys.argv[4]) if len(sys.argv) > 4 else 4
    if 10 in stages:
        os.environ['BAG2_UNLOCK'] = '1'
    s = Session(label='palcos_gpu_ab')
    s.start()
    log = open(OUT, 'a', encoding='utf-8')
    mod = lambda name, args: s.cmd({'cmd': 'mod_cmd', 'name': name, 'args': args}).get('reply', '')
    try:
        for i, k in enumerate(stages):
            if i == 0:
                s.to_match_2p('HEAT', 'COMET', k)
            else:
                s.menu_to_match_2p('HEAT', 'COMET', k)
            mod('gpu_timers', '1')
            time.sleep(6)
            acc = {v: [] for v in variants}
            for r in range(reps):
                for v in variants:
                    hv, sv, gv = (v.split(';') + ['', ''])[:3]
                    if hv:
                        mod('heat_mode', hv)
                    if sv:
                        mod('stage_post', sv)
                    if gv:
                        mod('stage_geo', gv)
                    time.sleep(1.0)
                    mod('stage_geo', 'sz')
                    mod('heat_stats', '')
                    f0 = s.cmd({'cmd': 'frame'}).get('frame', 0)
                    time.sleep(secs)
                    st = mod('stage_geo', 'sz')
                    hs = mod('heat_stats', '')
                    frames = max(s.cmd({'cmd': 'frame'}).get('frame', 0) - f0, 1)
                    ots = max(num(r'ots=(\d+)', st), 1.0)
                    pct = 100.0 / (frames / 60.0 * 1000.0)   # ms de GPU na janela -> % do tempo
                    row = (num(r'gpu0_ms_total=([\d.]+)', st) * pct, num(r'gpu1_ms_total=([\d.]+)', st) * pct,
                           num(r'gpu_corpo_us=([\d.]+)', hs) * num(r'gpu_corpo_us=[\d.]+ \((\d+)\)', hs) / 1000 * pct,
                           num(r'gpu_chao_us=([\d.]+)', hs) * num(r'gpu_chao_us=[\d.]+ \((\d+)\)', hs) / 1000 * pct,
                           num(r'faces=(\d+)', st) / ots, num(r'sem_par=(\d+)', st) / ots)
                    acc[v].append(row)
                    line = f'pos {k} [{v}] janela {r}: ' + ', '.join(f'{c} {x:.2f}' for c, x in zip(COLS, row))
                    print(line, flush=True)
                    log.write(line + '\n')
            for v in variants:
                rows = acc[v]
                m = [sum(x[j] for x in rows) / len(rows) for j in range(len(COLS))]
                line = (f'pos {k} [{v}] média (GPU em % do tempo; faces por OT): ' +
                        ', '.join(f'{c} {x:.2f}' for c, x in zip(COLS, m)))
                print(line, flush=True)
                log.write(line + '\n')
            log.flush()
            s.quit_match()
    finally:
        log.close()
        s.stop()


if __name__ == '__main__':
    main()
