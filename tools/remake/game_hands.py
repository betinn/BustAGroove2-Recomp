#!/usr/bin/env python3
"""Close das mãos do HEAT no jogo, ORIGINAL e REMAKE no mesmo quadro (heat_mode 3), em cada gesto.

O jogo só troca de mão em passos acertados da dança; aqui o gesto é forçado (comando heat_force, que faz o
mesmo que o script da dança: liga o nó da mão do gesto como irmão do antebraço). O original e o remake usam
a mesma árvore, então os dois mostram o mesmo gesto. O recorte segue a posição na tela do osso de cada mão
(heat_mode devolve "maos=" e "shift=").
Uso: python tools/remake/game_hands.py [rótulo] [repetições] [game.toml]
Saída: tools/recomp/logs/remake/jogo/<rótulo>_<rep>_<gesto>.png (esquerda original | remake, direita idem).
"""
from __future__ import annotations

import os
import re
import sys
import time

from PIL import Image, ImageDraw, ImageFont

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from game_session import LOGS, Session  # noqa: E402

GESTOS = ['aberta', 'punho', 'aponta']


def main():
    tag = sys.argv[1] if len(sys.argv) > 1 else 'maos'
    reps = int(sys.argv[2]) if len(sys.argv) > 2 else 2
    game = sys.argv[3] if len(sys.argv) > 3 else 'game.toml'
    s = Session(label=tag, game=game)
    s.start()
    shots = []
    try:
        s.to_match()
        s.cmd({'cmd': 'mod_cmd', 'name': 'heat_mode', 'args': '3'})
        for rep in range(reps):
            for g in (1, 2, 0):
                s.cmd({'cmd': 'mod_cmd', 'name': 'heat_force', 'args': f'{g} {g}'})
                time.sleep(2.5)
                r = s.cmd({'cmd': 'mod_cmd', 'name': 'heat_mode', 'args': '3'})['reply']
                p = os.path.join(LOGS, f'{tag}_{rep}_{g}_full.png')
                s.shot(p)
                shots.append((p, r, g, rep))
        s.cmd({'cmd': 'mod_cmd', 'name': 'heat_force', 'args': '-1 -1'})
        s.cmd({'cmd': 'mod_cmd', 'name': 'heat_mode', 'args': '1'})
    finally:
        s.stop()
    try:
        font = ImageFont.truetype('C:/Windows/Fonts/segoeuib.ttf', 30)
    except OSError:
        font = ImageFont.load_default()
    for p, r, g, rep in shots:
        im = Image.open(p)
        sc = im.width / 512.0
        h = list(map(float, re.search(r'maos=([-\d.,]+)', r).group(1).split(',')))
        shift = float(re.search(r'shift=([-\d.]+)', r).group(1))
        mask = re.search(r'mask=(\w+)', r).group(1)
        bb = list(map(float, re.search(r'bbox=([-\d.,]+)', r).group(1).split(',')))
        tiles = []
        for k in range(2):
            x, y = h[3 * k:3 * k + 2]
            half = max((bb[3] - bb[1]) * 0.11, 8.0)   # uma mão tem ~1/9 da altura do HEAT
            for dx, lab in ((0.0, 'ORIGINAL'), (shift, 'REMAKE')):
                c = im.crop((int((x + dx - half) * sc), int((y - half * 0.6) * sc),
                             int((x + dx + half) * sc), int((y + half * 1.4) * sc))).resize((480, 480), Image.LANCZOS)
                ImageDraw.Draw(c).text((10, 6), f'{lab} {"esq." if k == 0 else "dir."}', fill=(255, 255, 255),
                                       font=font, stroke_width=3, stroke_fill=(0, 0, 0))
                tiles.append(c)
        o = Image.new('RGB', (4 * 480 + 3 * 6, 480))
        for i, t in enumerate(tiles):
            o.paste(t, (i * 486, 0))
        fn = os.path.join(LOGS, f'{tag}_{rep}_{GESTOS[g]}.png')
        o.save(fn)
        os.remove(p)
        print(fn, 'máscara', mask)


if __name__ == '__main__':
    main()
