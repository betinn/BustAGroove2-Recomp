#!/usr/bin/env python3
"""Volta pelo elenco no modo 2 jogadores (fase 4, peça 3; build-debug, TCP 4370).

Para cada par "P1,P2,palco" abre o jogo, entra na partida e:
  1. lê as árvores de ossos dos dois por `segundos` (char_tree: qual nó o jogo liga em cada posição; mãos e peças
     que o asset não tem aparecem como `bag2_heat.match_fail`, quadros em que o original volta);
  2. tira 3 capturas no modo 3 (original à esquerda, remake à direita) e recorta cada personagem (game_chars);
  3. relata `heat_chars` (estado dos assets, páginas achadas na VRAM, figurino escolhido), os contadores do plugin
     e o fps.
Saída: tools/recomp/logs/remake/jogo/tour_<P1>_<P2>_*.png e tour.txt (acumula).
Uso: python tools/remake/chars_tour.py "KITTY-N,HIRO,0|SHORTY,STRIKE,0" [segundos_de_árvore]
"""
from __future__ import annotations

import os
import re
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from char_tree import tree  # noqa: E402
from game_chars import instances  # noqa: E402
from game_session import LOGS, PLAYER_STATE, Session  # noqa: E402
from PIL import Image  # noqa: E402

COUNTERS = ('frames_heat', 'frames_outros', 'match_fail', 'node_roots', 'draw_tree', 'load_tim', 'load_draw',
            'inst_new', 'pages_scan', 'face_moves', 'face_move_after', 'shadow_skipped', 'floor_marks')


def counters(s):
    return {c['name'][10:]: c['count'] for c in s.cmd({'cmd': 'mod_counters'})['counters']
            if c['name'].startswith('bag2_heat.') and c['name'][10:] in COUNTERS}


def main():
    pairs = (sys.argv[1] if len(sys.argv) > 1 else 'KITTY-N,HIRO,0').split('|')
    secs = float(sys.argv[2]) if len(sys.argv) > 2 else 40.0
    out = open(os.path.join(LOGS, 'tour.txt'), 'a', encoding='utf-8')

    def log(*a):
        line = ' '.join(str(x) for x in a)
        print(line, flush=True)
        out.write(line + '\n'); out.flush()

    for pair in pairs:
        p1, p2, st = (pair.split(',') + ['0'])[:3]
        tag = f'tour_{p1}_{p2}_{st}'.replace('&', '').replace('*', '').replace(' ', '_')
        s = Session(label=tag)
        s.start()
        try:
            s.to_match_2p(p1, p2, int(st))
            names = s.who()
            log(f'=== {pair}: partida {names}')
            rd = lambda a: int.from_bytes(bytes.fromhex(s.cmd({'cmd': 'read_ram', 'addr': a, 'len': 4})['hex']), 'little')
            roots = [rd(rd(p) + 0x28) for p in PLAYER_STATE]
            c0 = counters(s)
            hist = [dict() for _ in roots]
            t0 = time.time()
            while time.time() - t0 < secs:
                for k, r in enumerate(roots):
                    for pos, i in enumerate(tree(s, r)):
                        h = hist[k].setdefault(pos, {})
                        h[i] = h.get(i, 0) + 1
                time.sleep(0.05)
            c1 = counters(s)
            log('   contadores na janela:', {k: c1.get(k, 0) - c0.get(k, 0) for k in COUNTERS if c1.get(k, 0) - c0.get(k, 0)})
            for k, name in enumerate(names):
                swaps = {pos: h for pos, h in hist[k].items() if len(h) > 1 or next(iter(h)) != pos}
                log(f'   árvore {name}: ' + '; '.join(f'pos {pos}: ' + ', '.join(f'{i}x{c}' for i, c in sorted(h.items()))
                                                    for pos, h in sorted(swaps.items())))
            chars = s.cmd({'cmd': 'mod_cmd', 'name': 'heat_chars', 'args': ''}).get('reply', '')
            for part in chars.split('||'):
                if 'jogo:' in part or 'estado=2' in part or 'loads=' in part:
                    log('  ', re.sub(r'\s+', ' ', part.strip())[:300])
            crops = {}
            for m in range(3):
                s.cmd({'cmd': 'mod_cmd', 'name': 'heat_mode', 'args': '3'})
                time.sleep(0.3)
                p = os.path.join(LOGS, f'{tag}_m{m}.png')
                s.shot(p)
                im = Image.open(p).convert('RGB')
                for name, (x0, y0, x1, y1) in instances(s):
                    w = (x1 - x0) * 1.05
                    box = [int(max(0, (x0 - w - 6) * 9)), int(max(0, (y0 - 6) * 9)), int(min(512, x1 + 6) * 9),
                           int(min(240, y1 + 6) * 9)]
                    if box[2] - box[0] > 50 and box[3] - box[1] > 50:
                        crops.setdefault(name, []).append(im.crop(box))
                s.cmd({'cmd': 'mod_cmd', 'name': 'heat_mode', 'args': '1'})
                os.remove(p)
                time.sleep(6)
            for name, cs in crops.items():
                hgt = 700
                cs = [c.resize((max(1, c.size[0] * hgt // c.size[1]), hgt)) for c in cs]
                grid = Image.new('RGB', (sum(c.size[0] for c in cs), hgt))
                x = 0
                for c in cs:
                    grid.paste(c, (x, 0)); x += c.size[0]
                f = os.path.join(LOGS, f'{tag}_{name.replace("&", "").replace(" ", "_").replace("*", "_alt")}.png')
                grid.save(f)
                log('   captura', f)
            log('  ', s.fps())
        finally:
            s.stop()
            time.sleep(2)


if __name__ == '__main__':
    main()
