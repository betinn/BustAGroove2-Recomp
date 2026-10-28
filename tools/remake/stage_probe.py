#!/usr/bin/env python3
"""Sonda do palco (fase 3 do remake, passo 0): como o palco é desenhado e onde fica a batida da música.

Entra numa partida com o HEAT (build-debug, TCP 4370) e grava em tools/recomp/logs/remake/palco/:
  - gpu_<n>.json / gpu_<n>_resumo.json: GP0 de quadros da partida, com a função do jogo que emitiu cada
    primitiva (psx_gpu_frame.py do PSXRecomp) e o rank na OT;
  - ram_<n>.npy + ram_tempos.json: a RAM inteira em ~N momentos seguidos (para achar contadores de tempo/batida);
  - audio.wav: a saída do SPU no mesmo trecho (batida de referência, para não conferir "a olho");
  - tela_<n>.png: capturas.
Uso: python tools/remake/stage_probe.py [n_amostras_ram] [savestate_slot]
"""
from __future__ import annotations

import json
import os
import sys
import time

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, r'C:/tools/psxrecomp/tools')
from game_session import Session, REPO  # noqa: E402
import psx_gpu_frame as pgf  # noqa: E402

OUT = os.path.join(REPO, 'tools', 'recomp', 'logs', 'remake', 'palco')


def read_ram(s, addr=0x80000000, n=0x200000):
    r = s.cmd({'cmd': 'read_ram', 'addr': addr, 'len': n}, 30)
    h = r.get('data') or r.get('hex') or ''
    return np.frombuffer(bytes.fromhex(h), dtype=np.uint8)


def audio_mark(s, path):
    """Grava o fim do anel de áudio e devolve (quadro, total de amostras do SPU)."""
    fr = s.cmd({'cmd': 'frame'}).get('frame')
    r = s.cmd({'cmd': 'audio_wav', 'path': path.replace('\\', '/'), 'count': 1}, 30)
    return fr, r.get('tap_total'), r.get('rate')


def main():
    nram = int(sys.argv[1]) if len(sys.argv) > 1 else 40
    slot = int(sys.argv[2]) if len(sys.argv) > 2 else -1
    os.makedirs(OUT, exist_ok=True)
    s = Session(label='palco_sonda')
    s.start()
    try:
        s.enter_match()
        if slot >= 0:
            print('savestate', s.cmd({'cmd': 'savestate', 'op': 'save', 'slot': slot}))
        s.shot(os.path.join(OUT, 'tela_0.png'))
        conn = pgf.DebugConn()
        for k in range(2):
            d = pgf.capture(conn, label=f'partida{k}')
            pgf.save_dump(d, os.path.join(OUT, f'gpu_{k}.json'))
            sm = pgf.save_summary(d, os.path.join(OUT, f'gpu_{k}_resumo.json'))
            print('gpu', k, 'quadro', d['frame'], 'pacotes', sm['packets'], 'desenho', sm['drawing'], sm['modes'])
            time.sleep(0.7)
        # RAM em sequência + marcas de áudio no começo e no fim
        a0 = audio_mark(s, os.path.join(OUT, 'marca0.wav'))
        tempos = []
        for i in range(nram):
            f0 = s.cmd({'cmd': 'frame'}).get('frame')
            t0 = time.time()
            ram = read_ram(s)
            f1 = s.cmd({'cmd': 'frame'}).get('frame')
            np.save(os.path.join(OUT, f'ram_{i}.npy'), ram)
            tempos.append({'i': i, 'f0': f0, 'f1': f1, 't': t0})
        a1 = audio_mark(s, os.path.join(OUT, 'marca1.wav'))
        # áudio do trecho inteiro (anel do SPU): as últimas ~ (a1 - a0) amostras + folga
        n_aud = (a1[1] - a0[1]) + 44100 * 2 if a0[1] and a1[1] else 44100 * 30
        r = s.cmd({'cmd': 'audio_wav', 'path': os.path.join(OUT, 'audio.wav').replace('\\', '/'), 'count': int(n_aud)}, 60)
        print('audio', r)
        with open(os.path.join(OUT, 'ram_tempos.json'), 'w', encoding='utf-8') as f:
            json.dump({'amostras': tempos, 'audio0': a0, 'audio1': a1, 'audio': r}, f, indent=1)
        s.shot(os.path.join(OUT, 'tela_1.png'))
        print(s.fps())
    finally:
        s.stop()


if __name__ == '__main__':
    main()
