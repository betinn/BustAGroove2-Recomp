#!/usr/bin/env python3
"""Segunda volta de dumps do pacote HD nos palcos (fase 6b; build-debug, TCP 4370, 2 jogadores).

A primeira volta (stage_inventory.py run) ficou 60 s em cada palco: o voo da câmera e o começo da música. Esta fica
a música quase inteira (a música começa ~16 s depois da carga e dura ~150 s) em cada palco da tela de palco do 2
jogadores, com o dump ao vivo ligado (`hd_textures dump=1`), e sai pelo QUIT antes do fim. Os PNG novos ficam em
build-debug/mods/texture-packs/SLUS-01159/dumps; depois, `python tools/hd_textures.py --filter esrgan` (incremental)
amplia só os que ainda não têm substituição. Sai pelo `quit` no fim (sem matar o processo antes, senão o último lote
de dumps se perde).
Uso: python tools/remake/stage_dump2.py [posições separadas por vírgula] [segundos depois da carga]
     (a posição 10, o palco 238, só existe com os 18 personagens: o script os libera)
"""
from __future__ import annotations

import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from game_session import Session, REPO  # noqa: E402

DUMPS = os.path.join(REPO, 'game', 'build-debug', 'mods', 'texture-packs', 'SLUS-01159', 'dumps')


def n_dumps():
    return sum(len(fs) for _, _, fs in os.walk(DUMPS)) if os.path.isdir(DUMPS) else 0


def main():
    stages = [int(x) for x in (sys.argv[1] if len(sys.argv) > 1 else ','.join(map(str, range(11)))).split(',')]
    secs = float(sys.argv[2]) if len(sys.argv) > 2 else 145.0
    if 10 in stages:
        os.environ['BAG2_UNLOCK'] = '1'   # start() guarda os cartões e stop() os devolve
    s = Session(label='palcos_dump2')
    s.start()
    d0 = n_dumps()
    try:
        s.at(60)
        print('hd dump', s.cmd({'cmd': 'hd_textures', 'dump': 1}).get('dump'), flush=True)
        for i, k in enumerate(stages):
            if i == 0:
                s.to_match_2p('HEAT', 'COMET', k)
            else:
                s.menu_to_match_2p('HEAT', 'COMET', k)
            print(k, 'partida:', s.who(), flush=True)
            time.sleep(secs)
            print(k, s.fps(), 'dumps novos até aqui:', n_dumps() - d0, flush=True)
            s.quit_match()
        for _ in range(60):   # fila de dumps vazia antes de sair
            h = s.cmd({'cmd': 'hd_textures'})
            if not any(h.get(x) for x in ('queued_dumps', 'pending_dumps', 'dumps_pending', 'dump_queue')):
                break
            time.sleep(1)
        s.cmd({'cmd': 'hd_textures', 'dump': 0})
        time.sleep(2)
    finally:
        s.stop()
    print('dumps novos:', n_dumps() - d0, flush=True)


if __name__ == '__main__':
    main()
