#!/usr/bin/env python3
"""Texturas que a geometria do código do overlay usa e o asset do palco ainda não tem (fase 6; build-debug, TCP 4370).

Uma sessão, HEAT x COMET, um palco por partida (posições da tela de palco, padrão 0..11): espera a câmera rodar o
palco e lê do plugin as faces livres que ficaram com o jogo por falta de camada HD (stage_geo "k": página e CLUT de
cada textura, vezes). Grava tools/remake/stage_extra_keys.json ({container: [[tpage, clut, vezes], ...]}, só
coordenadas da VRAM), que tools/remake/export_stage.py usa para gerar as camadas que faltam.
Uso: python tools/remake/stage_keys.py [palcos separados por vírgula] [segundos por palco]
"""
from __future__ import annotations

import json
import os
import re
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from game_session import Session  # noqa: E402

OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'stage_extra_keys.json')


def main():
    stages = [int(x) for x in (sys.argv[1] if len(sys.argv) > 1 else ','.join(map(str, range(12)))).split(',')]
    secs = float(sys.argv[2]) if len(sys.argv) > 2 else 45.0
    keys = json.load(open(OUT, encoding='utf-8')) if os.path.exists(OUT) else {}
    s = Session(label='palcos_chaves')
    s.start()
    g = lambda a: s.cmd({'cmd': 'mod_cmd', 'name': 'stage_geo', 'args': a}).get('reply', '')
    try:
        for i, k in enumerate(stages):
            if i == 0:
                s.to_match_2p('HEAT', 'COMET', k)
            else:
                s.menu_to_match_2p('HEAT', 'COMET', k)
            g('kz')
            time.sleep(secs)
            m = re.search(r'palco(\d+)\.bin', g(''))
            r = g('kz')
            if not m:
                print(k, 'sem palco', flush=True)
                s.quit_match()
                continue
            c = m.group(1)
            cur = {(a, b): n for a, b, n in keys.get(c, [])}
            for t, cl, n in re.findall(r'([0-9A-F]{3})/([0-9A-F]{4})/(\d+)', r):
                key = (int(t, 16), int(cl, 16))
                cur[key] = cur.get(key, 0) + int(n)
            keys[c] = sorted([a, b, n] for (a, b), n in cur.items())
            print(k, c, r[:200], flush=True)
            with open(OUT, 'w', encoding='utf-8') as f:
                json.dump(keys, f, indent=1)
            s.quit_match()
    finally:
        s.stop()


if __name__ == '__main__':
    main()
