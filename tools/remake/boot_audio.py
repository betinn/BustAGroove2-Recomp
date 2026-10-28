#!/usr/bin/env python3
"""Underruns do áudio no boot (fase 5; build-debug, TCP 4370).

`audio_buffer_check.py` mede ~700 underruns nos primeiros 70 s de algumas sessões (e 0 depois). Este script amostra o
`audio_stats` a cada 0,2 s do lançamento até `segundos`, marca o segundo de cada underrun, o enchimento do buffer e o
contador de quadros do jogo (parado = vídeo de abertura), e guarda o áudio que saiu (`audio_wav`) para ver se os
underruns caem sobre som ou sobre silêncio.
Uso: python tools/remake/boot_audio.py [segundos] [sessões]
Saída: tools/recomp/logs/remake/ritmo/boot_<n>.txt e boot_<n>.wav
"""
from __future__ import annotations

import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from game_session import REPO, Session  # noqa: E402

OUT = os.path.join(REPO, 'tools', 'recomp', 'logs', 'remake', 'ritmo')


def main():
    secs = float(sys.argv[1]) if len(sys.argv) > 1 else 80.0
    runs = int(sys.argv[2]) if len(sys.argv) > 2 else 1
    os.makedirs(OUT, exist_ok=True)
    for r in range(runs):
        s = Session(label=f'boot_{r}')
        t_launch = time.time()
        s.start()
        t_ready = time.time() - t_launch
        rows, last_u = [], None
        try:
            while time.time() - s.t0 < secs:
                o = s.cmd({'cmd': 'audio_stats'}).get('out', {})
                fc = int.from_bytes(bytes.fromhex(s.cmd({'cmd': 'read_ram', 'addr': 0x8006EE78, 'len': 2})['hex']), 'little')
                u = o.get('underruns', 0) or 0
                rows.append((time.time() - s.t0, u, u - (last_u if last_u is not None else u), o.get('fill_ms') or 0.0, fc, o))
                last_u = u
                time.sleep(0.2)
            wav = os.path.join(OUT, f'boot_{r}.wav')
            w = s.cmd({'cmd': 'audio_wav', 'path': wav.replace('\\', '/'), 'start': -1}, 30)
        finally:
            s.stop()
        with open(os.path.join(OUT, f'boot_{r}.txt'), 'w', encoding='utf-8') as f:
            f.write(f'servidor TCP pronto {t_ready:.1f} s depois do lançamento; audio_wav: {w}\n')
            f.write(f'chaves do audio_stats: {sorted(rows[0][5]) if rows else []}\n')
            for t, u, du, fill, fc, _ in rows:
                if du or int(t * 5) % 10 == 0:
                    f.write(f't={t:6.1f} s underruns={u:5d} (+{du:3d}) fill={fill:6.1f} ms quadro_jogo={fc}\n')
        total = rows[-1][1] if rows else 0
        bursts = [round(t, 1) for t, _, du, *_ in rows if du]
        print(f'sessão {r}: {total} underruns; trechos com underrun (s): {bursts[:40]}', flush=True)


if __name__ == '__main__':
    main()
