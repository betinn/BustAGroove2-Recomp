#!/usr/bin/env python3
"""Batida da música no Bust A Groove 2 (fase 3 do remake, passo 0): amostra as variáveis candidatas a cada
quadro e grava o áudio do SPU no mesmo trecho, para conferir a fase da batida do jogo contra o som.

Grava em tools/recomp/logs/remake/palco/: batida.npz (quadro e bytes de cada região por amostra), batida.wav
(saída do SPU), batida.json (pares quadro x amostra de áudio). Depois, se PSX_POST_SHADER estiver no ambiente,
tira present_shot (com pós-efeitos) e, com `bloom` ao vivo, capturas sem e com o bloom do framework.
Uso: python tools/remake/stage_beat.py [segundos] [env PSX_POST_SHADER=...]
"""
from __future__ import annotations

import json
import os
import sys
import time

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from game_session import Session, REPO  # noqa: E402

OUT = os.path.join(REPO, 'tools', 'recomp', 'logs', 'remake', 'palco')
REGIONS = [(0x8005E000, 0x2800), (0x80088000, 0x3800), (0x8009EE00, 0x40), (0x801DA000, 0x20),
           (0x801E3E00, 0x1400)]


def read(s, a, n):
    return bytes.fromhex(s.cmd({'cmd': 'read_ram', 'addr': a, 'len': n}, 10)['hex'])


def audio_point(s, scratch):
    fr = s.cmd({'cmd': 'frame'})['frame']
    r = s.cmd({'cmd': 'audio_wav', 'path': scratch, 'count': 1}, 10)
    fr2 = s.cmd({'cmd': 'frame'})['frame']
    return (fr + fr2) / 2.0, int(r['tap_total'])


def present_shot(s, path, timeout=5.0):
    seq = s.cmd({'cmd': 'present_shot_seq'}).get('seq', 0)
    s.cmd({'cmd': 'present_shot', 'path': path.replace('\\', '/')})
    t = time.time()
    while time.time() - t < timeout:
        r = s.cmd({'cmd': 'present_shot_seq'})
        if r.get('seq', 0) != seq:
            return r
        time.sleep(0.05)
    return None


def main():
    secs = float(sys.argv[1]) if len(sys.argv) > 1 else 20.0
    os.makedirs(OUT, exist_ok=True)
    scratch = os.path.join(OUT, 'marca.wav').replace('\\', '/')
    s = Session(label='palco_batida')
    s.start()
    try:
        s.to_match()
        pts = [audio_point(s, scratch)]
        frames, data = [], {a: [] for a, _ in REGIONS}
        t0 = time.time()
        k = 0
        while time.time() - t0 < secs:
            f0 = s.cmd({'cmd': 'frame'})['frame']
            for a, n in REGIONS:
                data[a].append(np.frombuffer(read(s, a, n), dtype=np.uint8))
            f1 = s.cmd({'cmd': 'frame'})['frame']
            frames.append((f0 + f1) / 2.0)
            k += 1
            if k % 200 == 0:
                pts.append(audio_point(s, scratch))
        pts.append(audio_point(s, scratch))
        a0 = pts[0][1] - 44100
        r = s.cmd({'cmd': 'audio_wav', 'path': os.path.join(OUT, 'batida.wav').replace('\\', '/'),
                   'start': str(a0), 'count': str(pts[-1][1] - a0)}, 60)
        print('audio', r, 'amostras', len(frames))
        np.savez_compressed(os.path.join(OUT, 'batida.npz'), frames=np.array(frames),
                            **{f'r{a:08X}': np.stack(v) for a, v in data.items()})
        with open(os.path.join(OUT, 'batida.json'), 'w', encoding='utf-8') as f:
            json.dump({'pontos': pts, 'audio_inicio': a0, 'audio': r}, f, indent=1)
        if os.environ.get('PSX_POST_SHADER'):
            print('shot mascara', present_shot(s, os.path.join(OUT, 'mascara.png')))
        for k, st in enumerate((0.0, 1.0, 2.0)):
            print('bloom', s.cmd({'cmd': 'bloom', 'strength': st}))
            time.sleep(0.3)
            print(present_shot(s, os.path.join(OUT, f'bloom_fw_{k}.png')))
        print(s.fps())
    finally:
        s.stop()


if __name__ == '__main__':
    main()
