#!/usr/bin/env python3
"""Mede o desempenho do remake do HEAT numa partida (build-debug, TCP 4370).

Para cada variante (args do comando heat_mode; "0" = HEAT original) roda N segundos na mesma partida e
relata, a partir das linhas por segundo do log (PSX_FRAME_REPORT_MS=1000): fps médio, mínimo, segundos
abaixo de 59,5, emu_busy médio e máximo; e dos contadores do plugin: pacotes por quadro e tempo da CPU.
`audio_stats` antes/depois de cada janela mostra se o áudio passou fome (underruns), que é o que se
ouviria como engasgo da música.
Uso: python tools/remake/game_perf.py [rótulo] [variantes separadas por |] [segundos] [game.toml]
     ex.: python tools/remake/game_perf.py base "0|1" 40 game.toml
"""
from __future__ import annotations

import os
import re
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from game_session import LOGS, Session  # noqa: E402


def log_lines(s):
    with open(os.path.join(LOGS, f'{s.label}.log'), errors='replace') as f:
        return [l for l in f if 'fps=' in l and 'emu_busy=' in l]


def main():
    tag = sys.argv[1] if len(sys.argv) > 1 else 'perf'
    variants = (sys.argv[2] if len(sys.argv) > 2 else '0|1').split('|')
    secs = int(sys.argv[3]) if len(sys.argv) > 3 else 40
    game = sys.argv[4] if len(sys.argv) > 4 else 'game.toml'
    s = Session(label=tag, game=game)
    s.start()
    rows = []
    try:
        s.to_match()
        for v in variants:
            s.cmd({'cmd': 'mod_cmd', 'name': 'heat_mode', 'args': v})
            time.sleep(2)
            n0 = len(log_lines(s))
            c0 = {c['name']: c['count'] for c in s.cmd({'cmd': 'mod_counters'})['counters']}
            a0 = s.cmd({'cmd': 'audio_stats'})
            time.sleep(secs)
            c1 = {c['name']: c['count'] for c in s.cmd({'cmd': 'mod_counters'})['counters']}
            a1 = s.cmd({'cmd': 'audio_stats'})
            lines = log_lines(s)[n0:]
            fps = [float(re.search(r' fps=([\d.]+)', l).group(1)) for l in lines]
            busy = [float(re.search(r'emu_busy=([\d.]+)', l).group(1)) for l in lines]
            d = {k: c1.get(k, 0) - c0.get(k, 0) for k in c1}
            fr = max(d.get('bag2_heat.frames', 0), 1)
            us = sum(d.get(f'bag2_heat.us_{k}', 0) for k in ('geometry', 'prepare', 'emit')) / fr
            audio = {k: (a1.get(k, 0) - a0.get(k, 0)) if isinstance(a1.get(k), (int, float)) else a1.get(k)
                     for k in a1 if k not in ('id', 'ok')}
            rows.append((v, fps, busy, d.get('bag2_heat.packets', 0) / fr, us, audio))
            print(f'[{v}] fps med {sum(fps) / max(len(fps), 1):.1f} min {min(fps, default=0):.1f} '
                  f'<59.5: {sum(f < 59.5 for f in fps)}/{len(fps)} s | emu_busy med {sum(busy) / max(len(busy), 1):.0f}% '
                  f'max {max(busy, default=0):.0f}% | pacotes/quadro {rows[-1][3]:.0f} plugin {us:.0f} us', flush=True)
            print('   por quadro: ' + ' '.join(f'{k[10:]}={d.get(k, 0) / fr:.0f}' for k in (
                'bag2_heat.faces_visible', 'bag2_heat.faces_uvforced', 'bag2_heat.children', 'bag2_heat.pk_textured',
                'bag2_heat.pk_base')), flush=True)
            print(f'   áudio: underruns {audio.get("underruns")} pump_skips {audio.get("pump_skips")} '
                  f'pump_calls {audio.get("pump_calls")}', flush=True)
    finally:
        s.stop()


if __name__ == '__main__':
    main()
