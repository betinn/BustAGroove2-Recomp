#!/usr/bin/env python3
"""Tempo de GPU do remake do palco por peça (fase 3; build-debug, TCP 4370).

`frame_perf` do framework põe GL_TIME_ELAPSED em volta das draws da cena e da apresentação (lidas com 4 quadros de
atraso); os desenhos de host caem dentro da consulta da cena. É o intervalo na GPU, com as esperas dentro (cena +
apresentação dão sempre ~16,7 ms): serve de tendência, não de custo. O custo de cada peça vem dos carimbos. Numa
partida só, troca as variantes (args do heat_mode ; args do stage_post), espera o anel de 256 quadros renovar e lê
a média. Ordem ida e volta (A B C D D C B A) para separar deriva do palco da diferença entre variantes.
Também lê o tempo de GPU de cada peça medido pelos próprios plugins (carimbos GL_TIMESTAMP, src/gpu_timer.h):
pós-efeito (`stage_stats` gpu_us), corpo do HEAT e sombra no chão (`heat_stats` gpu_corpo_us / gpu_chao_us).
Uso: python tools/remake/stage_gpu.py ["variante|variante|..."] [segundos por leitura]
"""
from __future__ import annotations

import os
import re
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from game_session import Session  # noqa: E402

DEFAULT = ('base=1 4 0.6 0.25 258;0|pos=1 4 0.6 0.25 258;1 0|pos_emenda=1 4 0.6 0.25 258;1 32|chao=1 4 0.6 0.25 2;0|'
           'tudo=1 4 0.6 0.25 2;1 0')
KEYS = ('scene_gpu_ms_avg', 'mirror_gpu_ms_avg', 'canon_gpu_ms_avg', 'present_gpu_ms_avg', 'total_ms_avg',
        'cpu_flush_ms_avg', 'cpu_wide_ms_avg', 'batches_avg', 'mirror_passes_avg')


def main():
    variants = [v.split('=', 1) for v in (sys.argv[1] if len(sys.argv) > 1 else DEFAULT).split('|')]
    secs = float(sys.argv[2]) if len(sys.argv) > 2 else 8.0
    s = Session(label='palco_gpu')
    s.start()
    rows = {}
    try:
        s.to_match()
        print(s.cmd({'cmd': 'mod_cmd', 'name': 'gpu_timers', 'args': '1'}).get('reply'))
        s.at(110)
        for name, v in variants + variants[::-1]:
            heat, post = v.split(';')
            s.cmd({'cmd': 'mod_cmd', 'name': 'heat_mode', 'args': heat})
            s.cmd({'cmd': 'mod_cmd', 'name': 'stage_post', 'args': post})
            time.sleep(1.0)
            for c in ('stage_stats', 'heat_stats'):
                s.cmd({'cmd': 'mod_cmd', 'name': c, 'args': ''})
            time.sleep(secs - 1.0)
            w = dict(s.cmd({'cmd': 'frame_perf'}).get('wide_16_9', {}))
            st = s.cmd({'cmd': 'mod_cmd', 'name': 'stage_stats', 'args': ''}).get('reply', '')
            ht = s.cmd({'cmd': 'mod_cmd', 'name': 'heat_stats', 'args': ''}).get('reply', '')
            for key, txt in (('gpu_us', st), ('gpu_corpo_us', ht), ('gpu_chao_us', ht)):
                m = re.search(key + r'=([0-9.]+)(?: \((\d+)\))?', txt)
                w[key] = float(m.group(1)) if m else 0.0
                if m and m.group(2):
                    w[key + '_n'] = int(m.group(2))
            m = re.search(r'gpu_n=(\d+)', st)
            w['gpu_us_n'] = int(m.group(1)) if m else 0
            rows.setdefault(name, []).append(w)
            print(f'{name:10s} ' + ' '.join(f'{k.replace("_ms_avg", "").replace("_avg", "")}={w.get(k, 0):.3f}'
                                            for k in KEYS) +
                  f' | GPU por chamada: pos {w["gpu_us"]:.0f} us ({w.get("gpu_us_n", 0)}) corpo {w["gpu_corpo_us"]:.0f} us '
                  f'({w.get("gpu_corpo_us_n", 0)}) chao {w["gpu_chao_us"]:.0f} us ({w.get("gpu_chao_us_n", 0)})', flush=True)
        print('média das duas leituras (ms por quadro, GPU):')
        for name, _ in variants:
            r = rows[name]
            print(f'{name:10s} ' + ' '.join(f'{k.replace("_ms_avg", "").replace("_avg", "")}='
                                            f'{sum(x.get(k, 0) for x in r) / len(r):.3f}' for k in KEYS[:5] +
                                            ('gpu_us', 'gpu_corpo_us', 'gpu_chao_us')))
        print(s.fps())
    finally:
        s.stop()


if __name__ == '__main__':
    main()
