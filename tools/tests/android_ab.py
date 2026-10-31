#!/usr/bin/env python3
"""A/B no tablet com o mesmo trecho da partida para cada variante (docs/android-apk.md §7): janelas seguidas pegam
trechos diferentes da música e a variância engolia o efeito. Entra na partida (BAG2_MATCH, padrão o palco do ROBO-Z),
espera `espera` s, salva o savestate no slot 9 (só na memória: compara dentro da mesma sessão) e, duas vezes para cada
variante: manda os comandos, carrega o slot 9 e mede N s (quadros/s pelo contador do servidor, render do log, heat_hitch).
Uso: python tools/tests/android_ab.py rótulo '[["rótulo", [{"cmd": ...}, ...]], ...]' [segundos] [espera]
  ex.: '[["remake",[]],["original",[{"cmd":"mod_cmd","name":"heat_mode","args":"0"}]]]'
"""
import json
import os
import re
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from android_session import AndroidSession  # noqa: E402
import android_perf as ap  # noqa: E402

os.environ.setdefault('BAG2_UNLOCK', '1')
os.environ.setdefault('BAG2_STAGE_IDX', '11')
os.environ.setdefault('BAG2_MATCH', 'HEAT ROBO-Z_GOLD 0')


def main():
    tag = sys.argv[1]
    steps = json.loads(sys.argv[2])
    secs = float(sys.argv[3]) if len(sys.argv) > 3 else 6
    wait = float(sys.argv[4]) if len(sys.argv) > 4 else 30
    s = AndroidSession(label=tag)
    s.start()
    try:
        print('personagens', s.enter_match_checked(), flush=True)
        time.sleep(wait)
        print('save', s.cmd({'cmd': 'savestate', 'slot': 9, 'op': 'save'}, 30), flush=True)
        for rep in range(2):
            for label, cmds in steps:
                for c in cmds:
                    r = s.cmd(c)
                    if rep == 0:
                        print('  ', c, str(r)[:160], flush=True)
                s.cmd({'cmd': 'savestate', 'slot': 9, 'op': 'load'}, 30)
                time.sleep(0.7)
                n0 = len(ap.fps_lines(s))
                f0 = s.cmd({'cmd': 'frame'}).get('frame', 0)
                s.cmd({'cmd': 'mod_cmd', 'name': 'heat_hitch', 'args': 'z'})
                t0 = time.time()
                time.sleep(secs)
                f1 = s.cmd({'cmd': 'frame'}).get('frame', 0)
                dt = time.time() - t0
                h = s.cmd({'cmd': 'mod_cmd', 'name': 'heat_hitch', 'args': ''}).get('reply', '')
                vals = [ap.RE.search(l) for l in ap.fps_lines(s)[n0:]]
                vals = [float(m.group(3)) for m in vals if m]
                hn = re.search(r'hitch_n=(\d+)', h)
                print('%d %-30s quadros/s %5.1f render %5.1f%% hitch_n %s' % (
                    rep, label, (f1 - f0) / dt, sum(vals) / max(len(vals), 1), hn.group(1) if hn else '?'), flush=True)
    finally:
        s.stop()


if __name__ == '__main__':
    main()
