#!/usr/bin/env python3
"""Conferência do elenco numa sessão (fase 5; build-debug, TCP 4370): partidas do 2 jogadores em sequência, com QUIT
entre elas (game_session.quit_match / menu_to_match_2p).

Para cada par "P1,P2,palco" (padrão: partidas espelhadas, X contra X: o 1P no figurino normal e o 2P no alternativo):
  1. confere quem entrou (`who`) e lê as árvores de ossos dos dois por `segundos` (char_tree: qual nó o jogo liga em
     cada posição), contando os quadros que caíram no original (`bag2_heat.match_fail`) e os desenhados;
  2. a cada troca de expressão do rosto (casas convertidas para as páginas do personagem, `heat_chars`), uma captura
     no modo 3 (original à esquerda, remake à direita) recortada na cabeça de cada um;
  3. 3 capturas no modo 3 recortadas por personagem (corpo inteiro);
  4. o estado do plugin (assets carregados, GPU) e o fps.
Uso: BAG2_UNLOCK=1 python tools/tests/chars_check.py ["P1,P2,palco|..."] [segundos]
     BAG2_UNLOCK=1 libera os 18 personagens só na RAM (os cartões do usuário voltam no fim; game_session).
Saída: logs/remake/jogo/check_<P1>_<P2>_*.png e check.txt (acumula).
"""
from __future__ import annotations

import os
import re
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'remake'))
from char_tree import tree  # noqa: E402
from game_chars import instances  # noqa: E402
from game_session import LOGS, PLAYER_STATE, Session  # noqa: E402
from PIL import Image  # noqa: E402

COUNTERS = ('frames_heat', 'frames_outros', 'match_fail', 'load_tim', 'load_clut', 'load_draw', 'face_moves',
            'evict', 'shadow_skipped', 'floor_marks')


def safe(name: str) -> str:
    return name.replace('&', '').replace(' ', '_').replace('*', '_alt')


def counters(s):
    return {c['name'][10:]: c['count'] for c in s.cmd({'cmd': 'mod_counters'})['counters']
            if c['name'].startswith('bag2_heat.') and c['name'][10:] in COUNTERS}


def chars_state(s):
    """[(nome, bbox, casas, cabeça (x, y, raio, deslocamento))] dos personagens na partida."""
    out = []
    for part in s.cmd({'cmd': 'mod_cmd', 'name': 'heat_chars', 'args': ''}).get('reply', '').split('||'):
        m = re.search(r'jogo: (.+?) raiz=\w+ ha=(\d+) .*cabeca=([-\d.]+),([-\d.]+),([-\d.]+) desl=([-\d.]+) '
                      r'bbox=([-\d.]+),([-\d.]+),([-\d.]+),([-\d.]+) casas=(\d+)', part)
        if m and int(m.group(2)) < 8:
            out.append((m.group(1), [float(m.group(k)) for k in range(7, 11)], int(m.group(11)),
                        [float(m.group(k)) for k in range(3, 7)]))
    return out


def head_pair(im, head, bbox):
    """Rosto original (sem deslocamento) e remake (deslocado no modo 3), lado a lado, em pixels da captura 9x. Centro: x
    da cabeça (o osso com o maior mesh) e y perto do alto do retângulo (o maior mesh nem sempre é a cabeça)."""
    x, _, r, shift = head
    h = max(14.0, 0.17 * (bbox[3] - bbox[1]))
    y = bbox[1] + h
    cs = []
    for cx in (x - shift, x):
        box = [int((cx - h) * 9), int((y - h) * 9), int((cx + h) * 9), int((y + h) * 9)]
        if box[0] < 0 or box[1] < 0 or box[2] > im.size[0] or box[3] > im.size[1]:
            return None
        cs.append(im.crop(box).resize((400, 400)))
    out = Image.new('RGB', (800, 400))
    out.paste(cs[0], (0, 0)); out.paste(cs[1], (400, 0))
    return out


def crop(im, bbox):
    x0, y0, x1, y1 = bbox
    w = (x1 - x0) * 1.05
    y_end = y1 + 6
    box = [int(max(0, (x0 - w - 6) * 9)), int(max(0, (y0 - 6) * 9)), int(min(512, x1 + 6) * 9), int(min(240, y_end) * 9)]
    return im.crop(box) if box[2] - box[0] > 50 and box[3] - box[1] > 50 else None


def strip(images, out, hgt=700):
    images = [c.resize((max(1, c.size[0] * hgt // c.size[1]), hgt)) for c in images]
    grid = Image.new('RGB', (sum(c.size[0] for c in images), hgt))
    x = 0
    for c in images:
        grid.paste(c, (x, 0)); x += c.size[0]
    grid.save(out)


def mode3_shot(s, path):
    s.cmd({'cmd': 'mod_cmd', 'name': 'heat_mode', 'args': '3'})
    time.sleep(0.3)
    s.shot(path)
    state = chars_state(s)
    s.cmd({'cmd': 'mod_cmd', 'name': 'heat_mode', 'args': '1'})
    return Image.open(path).convert('RGB'), state


def main():
    pairs = (sys.argv[1] if len(sys.argv) > 1 else 'HEAT,HEAT,0').split('|')
    secs = float(sys.argv[2]) if len(sys.argv) > 2 else 40.0
    out = open(os.path.join(LOGS, 'check.txt'), 'a', encoding='utf-8')

    def log(*a):
        line = ' '.join(str(x) for x in a)
        print(line, flush=True)
        out.write(line + '\n'); out.flush()

    s = Session(label='chars_check')
    s.start()
    log(f'=== sessão {time.strftime("%Y-%m-%d %H:%M")} pares {pairs}')
    rd = lambda a: int.from_bytes(bytes.fromhex(s.cmd({'cmd': 'read_ram', 'addr': a, 'len': 4})['hex']), 'little')
    try:
        for k, pair in enumerate(pairs):
            p1, p2, st = (pair.split(',') + ['0'])[:3]
            if k == 0:
                s.to_match_2p(p1, p2, int(st))
            else:
                s.menu_to_match_2p(p1, p2, int(st))
            names = s.who()
            tag = f'check_{safe(p1)}_{safe(p2)}'
            log(f'--- {pair}: partida {names}')
            roots = [rd(rd(p) + 0x28) for p in PLAYER_STATE]
            c0 = counters(s)
            hist = [dict() for _ in roots]
            faces, seen = {}, set()
            every = float(os.environ.get('BAG2_FACE_EVERY', '0'))   # > 0: recorte da cabeça a cada N s também
            next_face = time.time() + every
            t0 = time.time()
            while time.time() - t0 < secs:
                for i, r in enumerate(roots):
                    for pos, n in enumerate(tree(s, r)):
                        h = hist[i].setdefault(pos, {})
                        h[n] = h.get(n, 0) + 1
                periodic = every > 0 and time.time() >= next_face
                if periodic:
                    next_face = time.time() + every
                for name, bbox, nc, _ in chars_state(s):
                    if (nc and (name, nc) not in seen) or periodic:   # expressão nova (ou periódico): original x remake
                        seen.add((name, nc))
                        p = os.path.join(LOGS, f'{tag}_tmp.png')
                        im, state = mode3_shot(s, p)
                        for n2, b2, _, head in state:
                            if n2 == name and (c := head_pair(im, head, b2)):
                                faces.setdefault(name, []).append(c)
                        os.remove(p)
                time.sleep(0.1)
            c1 = counters(s)
            log('   contadores:', {c: c1.get(c, 0) - c0.get(c, 0) for c in COUNTERS if c1.get(c, 0) - c0.get(c, 0)})
            for i, name in enumerate(names):
                swaps = {pos: h for pos, h in hist[i].items() if len(h) > 1 or next(iter(h)) != pos}
                log(f'   árvore {name}: ' + ('; '.join(f'pos {pos}: ' + ', '.join(f'{n}x{c}' for n, c in sorted(h.items()))
                                                     for pos, h in sorted(swaps.items())) or 'sem trocas'))
            for name, cs in faces.items():
                f = os.path.join(LOGS, f'{tag}_rosto_{safe(name)}.png')
                strip(cs[:8], f, 400)
                log(f'   rosto {name}: {len(cs)} expressões -> {f}')
            bodies = {}
            for m in range(3):
                p = os.path.join(LOGS, f'{tag}_m{m}.png')
                im, state = mode3_shot(s, p)
                for name, bbox, _, _ in state:
                    if (c := crop(im, bbox)):
                        bodies.setdefault(name, []).append(c)
                os.remove(p)
                time.sleep(4)
            for name, cs in bodies.items():
                f = os.path.join(LOGS, f'{tag}_{safe(name)}.png')
                strip(cs, f)
                log(f'   corpo {name}: {f}')
            head = s.cmd({'cmd': 'mod_cmd', 'name': 'heat_chars', 'args': ''}).get('reply', '').split('||')[0]
            log('  ', re.sub(r'\s+', ' ', head)[:400])
            log('  ', s.fps())
            s.quit_match()
    finally:
        s.stop()


if __name__ == '__main__':
    main()
