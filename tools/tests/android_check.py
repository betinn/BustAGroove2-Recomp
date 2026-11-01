#!/usr/bin/env python3
"""Conferência no tablet depois de uma mudança (docs/android-apk.md): capturas de tela do Android (o que o jogador
vê) no menu, na seleção de personagens, na tela de palco e na partida, controles abertos por porta (log) e, na
partida, `heat_hitch` a cada 10 s (maior intervalo entre vblanks, engasgos > 25 ms com instante, tempo dos passos de preparo GL
dos personagens e do palco) para pegar engasgos que a média por segundo esconde.

Uso: BAG2_MATCH="STRIKE HEAT 0" [BAG2_IR=960] python tools/tests/android_check.py [segundos na partida] [rótulo]
Saída: logs/remake/jogo/<rótulo>_*.png e o resumo no stdout.
"""
from __future__ import annotations

import os
import re
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from android_session import AndroidSession  # noqa: E402
import game_session as gs  # noqa: E402


def main():
    secs = int(sys.argv[1]) if len(sys.argv) > 1 else 150
    tag = sys.argv[2] if len(sys.argv) > 2 else 'check'
    s = AndroidSession(label=tag)
    s.start()
    try:
        spec = os.environ.get('BAG2_MATCH', 'HEAT COMET 0').split()
        p1, p2, st = (spec + ['HEAT', 'COMET', '0'][len(spec):])[:3]

        def step(name):
            time.sleep(1.0)
            s.screencap(os.path.join(gs.LOGS, f'{tag}_{name}.png'))
            print('captura', name, flush=True)

        ir = os.environ.get('BAG2_IR')   # linhas da resolução interna ao vivo (video_set): 960 = 4x, 1200 = 5x
        if ir:
            print('video_set', s.cmd({'cmd': 'video_set', 'internal_resolution': int(ir)}), flush=True)
        s.to_match_2p(p1, p2, int(st), on_step=step)
        time.sleep(6)
        step('carregando')
        print('personagens', s.who(), flush=True)
        time.sleep(20)
        step('partida')
        log = s.pull_log()
        with open(log, errors='replace') as f:
            for l in f:
                if 'controller' in l.lower() or 'slot' in l.lower():
                    print(l.strip()[:160], flush=True)
        t_end = time.time() + secs
        s.cmd({'cmd': 'mod_cmd', 'name': 'heat_hitch', 'args': 'z'})   # zera a janela
        while time.time() < t_end:
            time.sleep(10)
            h = s.cmd({'cmd': 'mod_cmd', 'name': 'heat_hitch', 'args': ''}).get('reply', '')
            st = s.cmd({'cmd': 'mod_cmd', 'name': 'stage_stats', 'args': ''}).get('reply', '')
            prep = re.search(r'prep_max_ms=\S+ prep_mip_ms=\S+', st)
            print('t=%3.0fs %s | palco %s' % (time.time() - s.t0, h, prep.group(0) if prep else '?'), flush=True)
        step('fim')
        log = s.pull_log()
        with open(log, errors='replace') as f:
            lines = [l for l in f if 'fps=' in l]
        low = [l for l in lines if float(re.search(r' fps=([\d.]+)', l).group(1)) < 59.5]
        print(f'segundos < 59,5 fps: {len(low)} de {len(lines)}', flush=True)
        for l in low[-10:]:
            print('  ' + l.strip()[11:120], flush=True)
    finally:
        s.stop()


if __name__ == '__main__':
    main()
