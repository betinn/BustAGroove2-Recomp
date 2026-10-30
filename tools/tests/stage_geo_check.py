#!/usr/bin/env python3
"""Conferência da geometria do palco pelo caminho novo (fase 6, peça 4; build-debug, TCP 4370, 2 jogadores).

Uma sessão, HEAT x COMET, um palco por partida (posições da tela de palco, padrão 0..11): espera o asset do palco
carregar e os recursos GL ficarem prontos, confere a projeção dos vértices do asset contra os pacotes do jogo (flag
64 do stage_geo: erro em pixels do PS1), e tira em três momentos uma captura dividida no mesmo quadro (flag 128: metade
esquerda original, direita remake) e uma com o remake na tela toda. Grava em logs/remake/palcos/
geo_<k>_<n>_{ab,remake}.png, a montagem geo_<k>.png e o relatório geo_check.txt (estado do asset, sequências e pacotes
por OT, erro da projeção, fps).
Uso: python tools/tests/stage_geo_check.py [palcos separados por vírgula] [segundos de espera depois da carga]
"""
from __future__ import annotations

import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'remake'))
from game_session import Session, REPO  # noqa: E402

OUT = os.path.join(REPO, 'logs', 'remake', 'palcos')


def geo(s, args):
    return s.cmd({'cmd': 'mod_cmd', 'name': 'stage_geo', 'args': args}).get('reply', '')


def main():
    stages = [int(x) for x in (sys.argv[1] if len(sys.argv) > 1 else ','.join(map(str, range(12)))).split(',')]
    wait = float(sys.argv[2]) if len(sys.argv) > 2 else 20.0
    os.makedirs(OUT, exist_ok=True)
    rep = open(os.path.join(OUT, 'geo_check.txt'), 'a', encoding='utf-8')
    s = Session(label='palcos_geo')
    s.start()
    try:
        for i, k in enumerate(stages):
            if i == 0:
                s.to_match_2p('HEAT', 'COMET', k)
            else:
                s.menu_to_match_2p('HEAT', 'COMET', k)
            time.sleep(wait)
            geo(s, '1 64')
            geo(s, 'sz')
            time.sleep(3)
            st = geo(s, 's')
            geo(s, '1 0')
            print(k, s.who(), st, flush=True)
            rep.write(f'palco {k}: {geo(s, "")}\n  {st}\n  {s.fps()}\n')
            shots = []
            for n in range(3):
                geo(s, '1 128')
                time.sleep(0.3)
                a = os.path.join(OUT, f'geo_{k:02d}_{n}_ab.png')
                s.shot(a)
                geo(s, '1 0')
                time.sleep(0.3)
                b = os.path.join(OUT, f'geo_{k:02d}_{n}_remake.png')
                s.shot(b)
                shots.append((a, b))
                time.sleep(6)
            rep.write(f'  depois das capturas: {geo(s, "s")}\n')
            rep.flush()
            montage(shots, os.path.join(OUT, f'geo_{k:02d}.png'))
            s.quit_match()
    finally:
        s.stop()
        rep.close()


def montage(shots, path):
    from PIL import Image
    W, H = 1152, 540
    im = Image.new('RGB', (2 * W, H * len(shots)))
    for r, (a, b) in enumerate(shots):
        for c, f in enumerate((a, b)):
            if os.path.exists(f):
                im.paste(Image.open(f).convert('RGB').resize((W, H)), (c * W, r * H))
    im.save(path)


if __name__ == '__main__':
    main()
