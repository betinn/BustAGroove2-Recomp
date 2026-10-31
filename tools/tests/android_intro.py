#!/usr/bin/env python3
"""Introdução de cada palco no tablet (docs/android-apk.md §7): do X na tela de palco até a dança começar.

Uma sessão (2 jogadores, HEAT × COMET ou BAG2_INTRO_P1/P2, os 18 liberados) entra numa partida por palco e, do menu até `janela` segundos
depois do X na tela de palco, lê `heat_hitch` a cada 0,5 s (intervalos entre vblanks > 25 ms na thread do jogo, com o
instante, e os passos de preparo GL), guarda os segundos do log com fps < 59,5 e os underruns do áudio; depois sai pela
pausa (QUIT) e vai ao próximo. Cada partida carrega os dois personagens e o palco de novo (a descarga é 3 s depois do
último desenho).
Palcos: posições da tela de palco com os 18 liberados (0 = 234, 1 = 233, 2 = 242, 3 = 243, 4 = 232, 5 = 237, 6 = 244,
7 = 235, 8 = 236, 9 = 231, 10 = 238) e os do modo história pelo índice (x11 = 241, x12 = 245; o 245 não tem partida:
só o palco, então fica por último).
Uso: python tools/tests/android_intro.py [rótulo] [palcos separados por vírgula, ex. 0,3,x11] [janela em s]
Saída: tabela no stdout e logs/remake/android/<rótulo>.json.
"""
from __future__ import annotations

import json
import os
import re
import sys
import threading
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from android_session import AndroidSession  # noqa: E402
import android_perf as ap  # noqa: E402
import game_session as gs  # noqa: E402

# container pelo índice do palco na RAM (byte 0x8005FB56; tabela em game_session.STAGE_IDX)
BY_IDX = (234, 243, 233, 244, 236, 242, 238, 237, 235, 231, 232, 241, 245)
POS = {0: 234, 1: 233, 2: 242, 3: 243, 4: 232, 5: 237, 6: 244, 7: 235, 8: 236, 9: 231, 10: 238}
IDX = {11: 241, 12: 245}
OUT = os.path.join(gs.REPO, 'logs', 'remake', 'android')


def main():
    tag = sys.argv[1] if len(sys.argv) > 1 else 'intro'
    spec = (sys.argv[2] if len(sys.argv) > 2 else '0,1,2,3,4,5,6,7,8,9,10,x11,x12').split(',')
    win = float(sys.argv[3]) if len(sys.argv) > 3 else 28.0
    os.environ['BAG2_UNLOCK'] = '1'
    p1, p2 = os.environ.get('BAG2_INTRO_P1', 'HEAT'), os.environ.get('BAG2_INTRO_P2', 'COMET')   # a tela de palco começa no do 1P
    s = AndroidSession(label=tag)
    s.start()
    mc = lambda n, a='': s.cmd({'cmd': 'mod_cmd', 'name': n, 'args': a}).get('reply', '')
    rows = []
    try:
        s.cmd({'cmd': 'set_input', 'buttons': 'FFFF', 'port': 2, 'connected': 1})
        s.at(74)
        s.unlock_all()
        s.at(75); s.press(gs.START)
        s.at(78)
        for i, p in enumerate(spec):
            hidden = p.startswith('x')
            k = int(p[1:]) if hidden else int(p)
            cont = IDX[k] if hidden else POS[k]
            if hidden:
                os.environ['BAG2_STAGE_IDX'] = str(k)
            else:
                os.environ.pop('BAG2_STAGE_IDX', None)
            n0 = len(ap.fps_lines(s))
            u0 = ap.underruns(s)
            hits = {}
            mc('heat_hitch', 'z')   # janela desde a saída da tela de palco anterior (menu, seleção, palco, carga)
            mc('stage_stats', 'z')
            stop = threading.Event()

            def sample():   # 'ultimos' guarda só os 8 últimos engasgos: lê a cada 0,5 s durante a rota e a janela
                while not stop.is_set():
                    try:
                        for t_s, ms in re.findall(r' ([\d.]+)s:(\d+)ms', mc('heat_hitch')):
                            hits[t_s] = int(ms)
                    except (OSError, ValueError):
                        pass
                    time.sleep(0.5)
            th = threading.Thread(target=sample, daemon=True)
            th.start()
            s.menu_to_match_2p(p1, p2, 0 if hidden else k)   # volta ~16 s (12 com o índice) depois do X
            t_conf = time.time() - (12.0 if hidden else 16.0)
            while time.time() < t_conf + win:
                time.sleep(0.5)
            stop.set()
            th.join()
            h = mc('heat_hitch')
            for t_s, ms in re.findall(r' ([\d.]+)s:(\d+)ms', h):
                hits[t_s] = int(ms)
            st = mc('stage_stats')
            g = lambda pat, txt: (re.search(pat, txt).group(1) if re.search(pat, txt) else '?')
            lines = ap.fps_lines(s)[n0:]
            low = [float(m.group(1)) for m in (ap.RE.search(l) for l in lines) if m and float(m.group(1)) < 59.5]
            idx = int(s.cmd({'cmd': 'read_ram', 'addr': '8005FB56', 'len': 1}).get('hex', 'ff'), 16)
            if idx < len(BY_IDX):
                cont = BY_IDX[idx]
            row = {
                'pos': p, 'palco': cont, 'quem': s.who(),
                'hitch_n': int(g(r'hitch_n=(\d+)', h)), 'hitch_max_ms': float(g(r'hitch_max_ms=([\d.]+)', h)),
                'acima_50ms': sorted(v for v in hits.values() if v > 50),
                'rt_gap_max_ms': float(g(r'rt_gap_max_ms=([\d.]+)', h)), 'rt_gap_n': int(g(r'rt_gap_n=(\d+)', h)),
                'prep_n': int(g(r'prep_n=(\d+)', h)), 'prep_max_ms': float(g(r'prep_max_ms=([\d.]+)', h)),
                'prep_mip_max_ms': float(g(r'prep_mip_max_ms=([\d.]+)', h)),
                'palco_prep_max_ms': float(g(r'prep_max_ms=([\d.]+)', st)),
                's_baixos': len(low), 'fps_min': min(low) if low else 59.9, 'underruns': ap.underruns(s) - u0,
            }
            rows.append(row)
            print(json.dumps(row, ensure_ascii=False), flush=True)
            if cont == 245:
                break
            s.quit_match()
    finally:
        s.stop()
        with open(os.path.join(OUT, f'{tag}.json'), 'w', encoding='utf-8') as f:
            json.dump(rows, f, ensure_ascii=False, indent=1)
    print('\npalco | quem | jogo máx (ms) | > 50 ms | render gap máx | preparo máx (pers. / palco) | s < 59,5 | underruns')
    for r in rows:
        print('%s (%s) | %s | %.0f (%d > 25) | %s | %.0f | %.0f / %.0f | %d | %d' % (
            r['palco'], r['pos'], '×'.join(str(x) for x in r['quem']), r['hitch_max_ms'], r['hitch_n'],
            r['acima_50ms'] or '-', r['rt_gap_max_ms'], r['prep_max_ms'], r['palco_prep_max_ms'], r['s_baixos'],
            r['underruns']))


if __name__ == '__main__':
    main()
