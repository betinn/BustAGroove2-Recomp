#!/usr/bin/env python3
"""Dumps incrementais de texturas dos palcos (fase 3, peça 5; build-debug, TCP 4370).

Para cada personagem pedido (deslocamento do cursor da seleção a partir do HEAT: "R" direita, "D" baixo), entra
numa partida com o dump do pacote HD ligado ao vivo (`hd_textures dump=1`), deixa a câmera rodar o palco, espera a
fila de dumps esvaziar e sai pelo `quit` (sem matar o processo antes, senão o último lote se perde).
Os PNG ficam em build-debug/mods/texture-packs/SLUS-01159/dumps; depois:
    python tools/hd_textures.py --dumps <essa pasta> --filter esrgan
Uso: python tools/remake/stage_dumps.py "R|RR|RRR|D" [segundos_de_partida]
"""
from __future__ import annotations

import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from game_session import Session, REPO, START, CROSS  # noqa: E402

RIGHT, DOWN = 0xFFDF, 0xFFBF
DUMPS = os.path.join(REPO, 'tools', 'recomp', 'bag2', 'build-debug', 'mods', 'texture-packs', 'SLUS-01159', 'dumps')


def count():
    return len([f for f in os.listdir(DUMPS) if f.lower().endswith('.png')]) if os.path.isdir(DUMPS) else 0


def main():
    routes = (sys.argv[1] if len(sys.argv) > 1 else 'R').split('|')
    secs = float(sys.argv[2]) if len(sys.argv) > 2 else 50.0
    for r in routes:
        n0 = count()
        s = Session(label=f'palco_dump_{r}')
        s.start()
        try:
            s.at(60)
            print(r, 'hd', s.cmd({'cmd': 'hd_textures', 'dump': 1}).get('dump'), flush=True)
            s.at(75); s.press(START); s.at(78); s.press(CROSS); s.at(81)
            for c in r:
                s.press(RIGHT if c == 'R' else DOWN)
                time.sleep(0.5)
            s.press(CROSS); s.at(84 + 0.5 * len(r)); s.press(CROSS)
            time.sleep(15 + secs)
            for _ in range(60):
                h = s.cmd({'cmd': 'hd_textures'})
                pend = [h.get(k) for k in ('queued_dumps', 'pending_dumps', 'dumps_pending', 'dump_queue') if k in h]
                if not any(pend):
                    break
                time.sleep(1)
            s.cmd({'cmd': 'hd_textures', 'dump': 0})
            time.sleep(2)
        finally:
            try:
                s.cmd({'cmd': 'quit'}, 3)
            except OSError:
                pass
            for _ in range(20):
                if s.p.poll() is not None:
                    break
                time.sleep(0.5)
            s.stop()
        print(r, 'dumps novos', count() - n0, 'total', count(), flush=True)


if __name__ == '__main__':
    main()
