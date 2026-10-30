#!/usr/bin/env python3
"""Atraso entre o tick da música do jogo e o som (fase 3, passo 0), e variável de cena (menu x partida).

Do boot até a seleção amostra a ~4 Hz a cena (ponteiro em 0x8008A2C8 e o valor apontado) e o tick; depois
do último X amostra o tick a cada quadro até a música andar ~20 s, e grava o áudio do SPU desde antes da
música começar (o primeiro som da música x o quadro em que o tick sai do zero).
Saída: logs/remake/palco/sync.json e sync.wav.
"""
from __future__ import annotations

import json
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'remake'))
from game_session import Session, REPO, START, CROSS  # noqa: E402

OUT = os.path.join(REPO, 'logs', 'remake', 'palco')


def rd(s, a, n):
    return bytes.fromhex(s.cmd({'cmd': 'read_ram', 'addr': a, 'len': n}, 10)['hex'])


def u32(b, o=0):
    return int.from_bytes(b[o:o + 4], 'little')


def sample(s):
    f = s.cmd({'cmd': 'frame'})['frame']
    mus = rd(s, 0x8005FB40, 0x160)
    p = u32(rd(s, 0x8008A2C8, 4))
    scene = u32(rd(s, p, 4)) if 0x80000000 <= p < 0x80200000 else -1
    return {'f': f, 't': time.time(), 'tick': u32(mus, 0x74), 'beat': u32(mus, 0x98) >> 16,
            'fc7c': u32(mus, 0x13C) >> 16, 'p': p, 'scene': scene}


def counters(s):
    r = s.cmd({'cmd': 'mod_counters'})
    return {c['name']: c['count'] for c in r.get('counters', []) if c['name'].startswith('bag2_heat.draw_tree')}


def main():
    os.makedirs(OUT, exist_ok=True)
    s = Session(label='palco_sync')
    s.start()
    log = []
    try:
        plan = [(75, START), (78, CROSS), (81, CROSS), (84, CROSS)]
        for t, b in plan:
            while time.time() - s.t0 < t:
                log.append(dict(sample(s), fase='menu', c=counters(s)))
                time.sleep(0.25)
            s.press(b)
        a0 = int(s.cmd({'cmd': 'audio_wav', 'path': os.path.join(OUT, 'marca.wav').replace('\\', '/'), 'count': 1})['tap_total'])
        f0 = s.cmd({'cmd': 'frame'})['frame']
        while time.time() - s.t0 < 125:
            log.append(dict(sample(s), fase='partida'))
        f1 = s.cmd({'cmd': 'frame'})['frame']
        a1 = int(s.cmd({'cmd': 'audio_wav', 'path': os.path.join(OUT, 'marca.wav').replace('\\', '/'), 'count': 1})['tap_total'])
        r = s.cmd({'cmd': 'audio_wav', 'path': os.path.join(OUT, 'sync.wav').replace('\\', '/'), 'start': str(a0),
                   'count': str(a1 - a0)}, 60)
        log.append({'fase': 'contadores', 'c': counters(s)})
        with open(os.path.join(OUT, 'sync.json'), 'w', encoding='utf-8') as f:
            json.dump({'a0': a0, 'a1': a1, 'f0': f0, 'f1': f1, 'audio': r, 'log': log}, f, indent=0)
        print('ok', len(log), r, s.fps())
    finally:
        s.stop()


if __name__ == '__main__':
    main()
