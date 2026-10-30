#!/usr/bin/env python3
"""Árvore de ossos dos dois dançarinos ao longo de uma partida (fase 4; build-debug, TCP 4370).

O jogo troca a mão (e outros pedaços) religando ponteiros da árvore de ossos na RAM (filho +0x94, irmão +0x98; os
nós ficam num vetor de 0xB0 bytes a partir da raiz). Este script lê o vetor de nós dos dois jogadores várias vezes
por segundo e conta, por posição na árvore (ordem de desenho, só nós com mesh), qual nó do arquivo estava ligado
ali. Serve para saber onde entram as mãos alternativas que não se soldam ao antebraço (export_char.ALT_POS).
Uso: python tools/tests/char_tree.py [segundos]   (BAG2_MATCH="P1 P2 palco", padrão HEAT COMET 0; GAME_TCP=1 usa o
     jogo já aberto na partida, game_tcp.py start, e não fecha no fim)
"""
from __future__ import annotations

import os
import struct
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'remake'))
from game_session import PLAYER_STATE, Session  # noqa: E402

NODE = 0xB0
MAX_NODES = 64


def tree(s, root):
    raw = bytes.fromhex(s.cmd({'cmd': 'read_ram', 'addr': root, 'len': NODE * MAX_NODES})['hex'])
    u32 = lambda i, off: struct.unpack_from('<I', raw, i * NODE + off)[0]
    out = []

    def walk(a, depth=0):
        while a and depth < 64 and len(out) < 48:
            i = (a - root) // NODE
            if not 0 <= i < MAX_NODES or (a - root) % NODE:
                out.append(-1)
                return
            if u32(i, 0xA0) == 1:
                out.append(i)
            walk(u32(i, 0x94), depth + 1)
            a = u32(i, 0x98)
    walk(root)
    return tuple(out)


def main():
    secs = float(sys.argv[1]) if len(sys.argv) > 1 else 150.0
    live = os.environ.get('GAME_TCP') == '1'
    s = Session(label='arvore')
    if not live:
        s.start()
    try:
        if not live:
            s.enter_match()
        rd = lambda a: struct.unpack('<I', bytes.fromhex(s.cmd({'cmd': 'read_ram', 'addr': a, 'len': 4})['hex']))[0]
        roots = [rd(rd(p) + 0x28) for p in PLAYER_STATE]
        names = s.who()
        hist = [dict() for _ in roots]
        lens = [dict() for _ in roots]
        t0 = time.time()
        n = 0
        while time.time() - t0 < secs:
            for k, r in enumerate(roots):
                t = tree(s, r)
                lens[k][len(t)] = lens[k].get(len(t), 0) + 1
                for pos, i in enumerate(t):
                    h = hist[k].setdefault(pos, {})
                    h[i] = h.get(i, 0) + 1
            n += 1
            time.sleep(0.05)
        print('amostras', n)
        for k, (name, r) in enumerate(zip(names, roots)):
            print(f'{name} (raiz {r:08X}): tamanhos da árvore {lens[k]}')
            for pos in sorted(hist[k]):
                h = hist[k][pos]
                if len(h) > 1 or next(iter(h)) != pos:
                    print(f'   posição {pos}: ' + ', '.join(f'nó {i} x{c}' for i, c in sorted(h.items())))
        print(s.fps())
    finally:
        if not live:
            s.stop()


if __name__ == '__main__':
    main()
