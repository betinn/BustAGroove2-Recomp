#!/usr/bin/env python3
"""Capturas do HEAT ORIGINAL e REMAKE no MESMO quadro do jogo: mesma pose, mesma luz do palco, mesma câmera.
heat_mode 3: o remake ao lado do original, deslocado na horizontal. heat_mode 7: três painéis, original,
remake por pacotes (fase 0) e remake pelo desenho de host (fase 1).

Uso: python tools/remake/game_sidebyside.py [rótulo] [args do heat_mode sem o modo] [game.toml] [momentos] [modo] [gesto]
     ex.: python tools/remake/game_sidebyside.py lado "4 0.6 0.25 2" game.toml 6 7
     modo 4: o remake e uma cópia ao lado com flags = flags ^ <7º número dos args> (A/B no mesmo quadro),
     ex.: "4 0.6 0.25 2 3300 128" compara com e sem sombra própria.
     gesto = 1: cada captura espera um gesto de mão alternativo (punho, apontar) no HEAT (máscara do
     heat_mode diferente de 0x13, mãos abertas), até 20 s; gesto = "f": força os gestos em rodízio
     (heat_force: aberta, punho, apontar), original e remake com a mesma mão.
Saída: tools/recomp/logs/remake/jogo/<rótulo>_<k>.png (recorte) e <rótulo>_<k>_tela.png (tela inteira reduzida).
"""
from __future__ import annotations

import os
import re
import sys
import time

from PIL import Image, ImageDraw, ImageFont

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from game_session import LOGS, Session  # noqa: E402

LABELS = {3: ['ORIGINAL', 'REMAKE'], 4: ['REMAKE', 'VARIANTE'],
          7: ['ORIGINAL', 'FASE 0 (PACOTES)', 'FASE 1 (DESENHO DE HOST)']}


def main():
    tag = sys.argv[1] if len(sys.argv) > 1 else 'lado'
    params = sys.argv[2] if len(sys.argv) > 2 else '4 0.6 0.25 2'
    game = sys.argv[3] if len(sys.argv) > 3 else 'game.toml'
    moments = int(sys.argv[4]) if len(sys.argv) > 4 else 4
    mode = int(sys.argv[5]) if len(sys.argv) > 5 else 3
    gesture = len(sys.argv) > 6 and sys.argv[6] == '1'
    force = len(sys.argv) > 6 and sys.argv[6] == 'f'
    labels = LABELS[mode]
    s = Session(label=tag, game=game)
    s.start()
    out = []
    try:
        s.enter_match()
        s.cmd({'cmd': 'mod_cmd', 'name': 'heat_mode', 'args': f'{mode} ' + params})
        if os.environ.get('HEAT_LIGHT'):   # ajuste de luz para o teste (args do heat_light)
            print(s.cmd({'cmd': 'mod_cmd', 'name': 'heat_light', 'args': os.environ['HEAT_LIGHT']}).get('reply'))
        for k in range(moments):
            if force:
                s.cmd({'cmd': 'mod_cmd', 'name': 'heat_force', 'args': f'{k % 3} {k % 3}'})
            time.sleep(3)
            r = s.cmd({'cmd': 'mod_cmd', 'name': 'heat_mode', 'args': f'{mode} ' + params})['reply']
            t0 = time.time()
            while gesture and re.search(r'mask=(\w+)', r).group(1) == '13' and time.time() - t0 < 20:
                time.sleep(0.03)
                r = s.cmd({'cmd': 'mod_cmd', 'name': 'heat_mode', 'args': f'{mode} ' + params})['reply']
            if gesture:
                print('máscara', re.search(r'mask=(\w+)', r).group(1), flush=True)
            x0, y0, x1, y1 = map(float, re.search(r'bbox=([-\d.,]+)', r).group(1).split(','))
            p = os.path.join(LOGS, f'{tag}_{k}_full.png')
            s.shot(p)
            out.append((p, (x0, y0, x1, y1)))
        s.cmd({'cmd': 'mod_cmd', 'name': 'heat_mode', 'args': '1 ' + params})
        s.cmd({'cmd': 'mod_cmd', 'name': 'heat_force', 'args': '-1 -1'})
        print(s.fps())
    finally:
        s.stop()
    try:
        font = ImageFont.truetype('C:/Windows/Fonts/segoeuib.ttf', 34)
    except OSError:
        font = ImageFont.load_default()
    n = len(labels)
    for k, (p, (x0, y0, x1, y1)) in enumerate(out):
        im = Image.open(p)
        sc = im.width / 512.0
        w = (x1 - x0)
        left = x0 - w * 1.05 * (n - 1) - w * 0.15   # o original fica (n - 1) larguras à esquerda do último
        if mode == 4:   # o retângulo é o do remake; a cópia fica uma largura à direita
            left, x1 = x0 - w * 0.15, x1 + w * 1.05
        box = (int(left * sc), int((y0 - (y1 - y0) * 0.06) * sc), int((x1 + w * 0.15) * sc), int((y1 + (y1 - y0) * 0.04) * sc))
        c = im.crop(box)
        c = c.resize((int(c.width * 1100 / c.height), 1100), Image.LANCZOS)
        d = ImageDraw.Draw(c)
        for i, lab in enumerate(labels):
            d.text((16 + i * c.width // n, 10), lab, fill=(255, 255, 255), font=font, stroke_width=3, stroke_fill=(0, 0, 0))
        fn = os.path.join(LOGS, f'{tag}_{k}.png')
        c.save(fn)
        im.resize((im.width // 3, im.height // 3), Image.LANCZOS).save(os.path.join(LOGS, f'{tag}_{k}_tela.png'))
        os.remove(p)
        print(fn)


if __name__ == '__main__':
    main()
