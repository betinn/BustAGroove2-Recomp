#!/usr/bin/env python3
"""Memória da GPU com a geometria do palco (fase 6; build-debug, TCP 4370, 2 jogadores).

Uma sessão com várias partidas (palcos pedidos, HEAT x COMET), QUIT entre elas; em cada partida mede a memória
dedicada da GPU do processo (contador do Windows, mem_check.proc_mem) e o que os plugins pediram (heat_chars,
stage_geo "s") na carga, depois de 20 s, depois de N cargas de savestate (o mesmo quadro, como stage_geo_pause.py) e
no menu depois do QUIT. Saída: tools/recomp/logs/remake/palcos/mem.txt.
Uso: python tools/remake/stage_mem.py [palcos separados por vírgula] [cargas de savestate por partida]
"""
from __future__ import annotations

import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from game_session import Session, REPO  # noqa: E402
from mem_check import proc_mem  # noqa: E402

OUT = os.path.join(REPO, 'tools', 'recomp', 'logs', 'remake', 'palcos')


def main():
    stages = [int(x) for x in (sys.argv[1] if len(sys.argv) > 1 else '0,1').split(',')]
    loads = int(sys.argv[2]) if len(sys.argv) > 2 else 4
    out = open(os.path.join(OUT, 'mem.txt'), 'a', encoding='utf-8')
    s = Session(label='palcos_mem')
    s.start()

    def log(tag):
        g, p = proc_mem(s.p.pid)
        st = s.cmd({'cmd': 'mod_cmd', 'name': 'stage_geo', 'args': 's'}).get('reply', '')
        st = st[st.find('|', st.find('rt ')) + 1:] if 'rt ' in st else st
        hc = s.cmd({'cmd': 'mod_cmd', 'name': 'heat_chars', 'args': ''}).get('reply', '')[:160]
        line = f'{time.time() - s.t0:6.1f} s {tag}: GPU {g:.0f} MB, privada {p:.0f} MB | palco:{st} | {hc}'
        print(line, flush=True)
        out.write(line + '\n')
        out.flush()
    try:
        s.at(70)
        log('abertura')
        for i, k in enumerate(stages):
            if i == 0:
                s.to_match_2p('HEAT', 'COMET', k)
            else:
                s.menu_to_match_2p('HEAT', 'COMET', k)
            log(f'palco {k} carregando')
            time.sleep(20)
            log(f'palco {k} partida')
            s.cmd({'cmd': 'savestate', 'slot': 9, 'op': 'save'})
            for n in range(loads):
                s.cmd({'cmd': 'savestate', 'slot': 9, 'op': 'load'})
                time.sleep(1.0)
                log(f'palco {k} savestate {n + 1}')
            s.quit_match()
            time.sleep(5)
            log(f'palco {k} menu')
    finally:
        s.stop()
        out.close()


if __name__ == '__main__':
    main()
