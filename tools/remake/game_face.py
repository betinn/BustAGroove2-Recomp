#!/usr/bin/env python3
"""Expressão do rosto do HEAT no jogo: ORIGINAL e REMAKE lado a lado (heat_mode 3), no MESMO quadro, um recorte
do rosto para cada estado das casas (olhos, boca) que o jogo usar durante a partida (tools/remake/faces.py).

Uso: python tools/remake/game_face.py [rótulo] [segundos] [capturas por estado]
Lê `heat_face` a cada ~15 ms; quando a tabela de casas muda para um estado ainda sem captura, tira a captura de
alta resolução na hora e confere depois se o estado não mudou no meio (senão descarta). Saída:
tools/recomp/logs/remake/jogo/<rótulo>_<k>.png e o resumo dos contadores bag2_heat.face_*.
"""
from __future__ import annotations

import os
import re
import sys
import time

from PIL import Image, ImageDraw, ImageFont

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from faces import SLOTS  # noqa: E402
from game_session import LOGS, Session  # noqa: E402


def state_name(rm: str) -> str:
    """'1:0,32-48,56>96,-32 ...' -> 'olho_a=96,0 ...' (origem de cada casa; casa fora da lista = disco)."""
    names = []
    for m in re.finditer(r'(\d+):(\d+),(\d+)-(\d+),(\d+)>(-?\d+),(-?\d+)', rm):
        x0, y0, du, dv = int(m.group(2)), int(m.group(3)), int(m.group(6)), int(m.group(7))
        slot = next((k for k, ((x, y, _, _), _) in SLOTS.items() if (x, y) == (x0, y0)), f'{x0},{y0}')
        names.append(f'{slot}={x0 + du},{y0 + dv}')
    return ' '.join(sorted(names)) or 'disco'


def face(s):
    r = s.cmd({'cmd': 'mod_cmd', 'name': 'heat_face', 'args': ''}).get('reply', '')
    m = re.search(r'cabeca=([-\d.]+),([-\d.]+),([-\d.]+),([-\d.]+) shift=([-\d.]+) rm=\d+(.*)', r)
    if not m:
        return None
    hx, hy, z, h, shift = (float(m.group(i)) for i in range(1, 6))
    return dict(hx=hx, hy=hy, z=z, h=h, shift=shift, state=state_name(m.group(6)), raw=r)


def main():
    tag = sys.argv[1] if len(sys.argv) > 1 else 'rosto'
    secs = float(sys.argv[2]) if len(sys.argv) > 2 else 60
    per_state = int(sys.argv[3]) if len(sys.argv) > 3 else 1
    s = Session(label=tag)
    s.start()
    shots, seen = [], {}
    try:
        s.to_match()
        s.cmd({'cmd': 'mod_cmd', 'name': 'heat_mode', 'args': '3 4 0.6 0.25 2'})
        c0 = {c['name']: c['count'] for c in s.cmd({'cmd': 'mod_counters'})['counters']}
        t0 = time.time()
        while time.time() - t0 < secs:
            f = face(s)
            if not f or f['z'] <= 0 or seen.get(f['state'], 0) >= per_state:
                time.sleep(0.015)
                continue
            p = os.path.join(LOGS, f'{tag}_{len(shots)}_full.png')
            s.shot(p)
            g = face(s)
            if g['state'] != f['state']:   # trocou durante a captura: não dá para saber o quadro
                os.remove(p)
                continue
            seen[f['state']] = seen.get(f['state'], 0) + 1
            shots.append((p, f))
            print(f'{time.time() - t0:5.1f} s: {f["state"]}', flush=True)
        c1 = {c['name']: c['count'] for c in s.cmd({'cmd': 'mod_counters'})['counters']}
        for k in sorted(c1):
            if 'face' in k:
                print(f'{k}: {c1[k] - c0.get(k, 0)}')
        print(s.fps())
    finally:
        s.stop()
    try:
        font = ImageFont.truetype('C:/Windows/Fonts/segoeuib.ttf', 26)
    except OSError:
        font = ImageFont.load_default()
    for k, (p, f) in enumerate(shots):
        im = Image.open(p)
        sc = im.width / 512.0
        r = f['h'] * 120.0 / f['z']   # meia altura da cabeça em pixels do PS1 (~120 unidades da câmera do GTE)
        crops = []
        for x in (f['hx'], f['hx'] + f['shift']):
            box = (int((x - r) * sc), int((f['hy'] - r * 1.1) * sc), int((x + r) * sc), int((f['hy'] + r * 0.9) * sc))
            crops.append(im.crop(box).resize((520, 520), Image.LANCZOS))
        out = Image.new('RGB', (1046, 520))
        out.paste(crops[0], (0, 0)); out.paste(crops[1], (526, 0))
        d = ImageDraw.Draw(out)
        for i, lab in enumerate(('ORIGINAL', 'REMAKE')):
            d.text((12 + 526 * i, 8), lab, fill=(255, 255, 255), font=font, stroke_width=3, stroke_fill=(0, 0, 0))
        d.text((12, 486), f['state'], fill=(255, 255, 255), font=font, stroke_width=3, stroke_fill=(0, 0, 0))
        fn = os.path.join(LOGS, f'{tag}_{k}.png')
        out.save(fn)
        im.resize((im.width // 3, im.height // 3), Image.LANCZOS).save(os.path.join(LOGS, f'{tag}_{k}_tela.png'))
        os.remove(p)
        print(fn)


if __name__ == '__main__':
    main()
