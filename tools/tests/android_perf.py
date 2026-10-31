#!/usr/bin/env python3
"""Mede uma partida no tablet (APK Android, docs/android-apk.md), como tools/tests/game_perf.py no PC.

Entra numa partida de 2 jogadores (BAG2_MATCH, padrão "HEAT COMET 0") e, para cada variante, roda N segundos e
relata, das linhas por segundo do log do aparelho (PSX_FRAME_REPORT_MS=1000 pelo manifesto): fps médio e mínimo,
segundos abaixo de 59,5, emu_busy e render_busy médios e máximos; `audio_stats` antes/depois (underruns = estalos);
temperatura da bateria (dumpsys battery) no fim de cada janela.

Variante = "heat_mode;stage_post;stage_geo" (args de cada comando; vazio = não mexe), como no game_perf.py:
    "1;1;1" remake inteiro (padrão), "0;0;0" original, "1;0;1" sem pós-efeito.
Uso: python tools/tests/android_perf.py [rótulo] [variantes separadas por |] [segundos]
     ex.: python tools/tests/android_perf.py tablet "1;1;1|0;0;0" 60
"""
from __future__ import annotations

import os
import re
import subprocess
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from android_session import ADB, FILES, AndroidSession, adb  # noqa: E402
import game_session as gs  # noqa: E402

RE = re.compile(r'fps=([\d.]+) avg_fps=[\d.]+ emu_busy=([\d.]+)% render_busy=([\d.]+)% dynres=(\d+)')


def fps_lines(s):
    path = s.pull_log()
    with open(path, errors='replace') as f:
        return [l for l in f if 'fps=' in l and 'emu_busy=' in l]


def battery_temp():
    out = adb('shell', 'dumpsys', 'battery', capture=True, check=False) or ''
    m = re.search(r'temperature: (\d+)', out)
    return int(m.group(1)) / 10.0 if m else float('nan')


def underruns(s):
    try:
        a = s.cmd({'cmd': 'audio_stats'})
    except OSError:
        return 0
    return int(a.get('underruns', 0) or 0)


def main():
    tag = sys.argv[1] if len(sys.argv) > 1 else 'android_perf'
    variants = (sys.argv[2] if len(sys.argv) > 2 else '1;1;1').split('|')
    secs = int(sys.argv[3]) if len(sys.argv) > 3 else 60
    s = AndroidSession(label=tag)
    s.start()
    rows = []
    try:
        s.enter_match()
        print('personagens', s.who(), flush=True)
        time.sleep(3)
        for v in variants:
            parts = (v.split(';') + ['', '', ''])[:3]
            for name, args in zip(('heat_mode', 'stage_post', 'stage_geo'), parts):
                if args != '':
                    s.cmd({'cmd': 'mod_cmd', 'name': name, 'args': args})
            time.sleep(2)
            n0 = len(fps_lines(s))
            u0 = underruns(s)
            t0 = time.time()
            time.sleep(secs)
            lines = fps_lines(s)[n0:]
            u1 = underruns(s)
            temp = battery_temp()
            vals = [RE.search(l) for l in lines]
            vals = [(float(m.group(1)), float(m.group(2)), float(m.group(3)), int(m.group(4))) for m in vals if m]
            if not vals:
                print(f'{v}: sem linhas de fps', flush=True)
                continue
            fps = [a for a, _, _, _ in vals]
            emu = [b for _, b, _, _ in vals]
            ren = [c for _, _, c, _ in vals]
            row = (v, len(vals), sum(fps) / len(fps), min(fps), sum(1 for f in fps if f < 59.5),
                   sum(emu) / len(emu), max(emu), sum(ren) / len(ren), max(ren), u1 - u0, temp, vals[-1][3])
            rows.append(row)
            print('%-12s s=%3d fps méd %5.1f mín %5.1f <59,5: %2d | jogo %4.1f/%4.1f %% render %4.1f/%4.1f %% | '
                  'underruns +%d | bateria %.1f °C | escala %dx' % row, flush=True)
    finally:
        s.stop()
    if rows:
        print('\nvariante      | s | fps méd | fps mín | <59,5 | jogo méd/máx | render méd/máx | underruns | °C')
        for r in rows:
            print('%-13s|%3d| %7.1f | %7.1f | %5d | %5.1f / %5.1f | %6.1f / %6.1f | %9d | %.1f' %
                  (r[0], r[1], r[2], r[3], r[4], r[5], r[6], r[7], r[8], r[9], r[10]))


if __name__ == '__main__':
    main()
