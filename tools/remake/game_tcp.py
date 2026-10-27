#!/usr/bin/env python3
"""Controle avulso da build-debug para afinar efeitos ao vivo (TCP 4370).

  python tools/remake/game_tcp.py start [rótulo]   abre o jogo, entra na partida e deixa rodando
  python tools/remake/game_tcp.py mod <comando> "<args>"   mod_cmd (ex.: stage_post "1 8 3 0.45")
  python tools/remake/game_tcp.py shot <nome> [hr]   present_shot (janela) e, com "hr", screenshot_hires
  python tools/remake/game_tcp.py raw '<json>'
  python tools/remake/game_tcp.py stop
Capturas em tools/recomp/logs/remake/palco/.
"""
from __future__ import annotations

import json
import os
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from game_session import Session, REPO  # noqa: E402
from stage_beat import present_shot  # noqa: E402

OUT = os.path.join(REPO, 'tools', 'recomp', 'logs', 'remake', 'palco')


def main():
    op = sys.argv[1]
    s = Session(label=sys.argv[2] if op == 'start' and len(sys.argv) > 2 else 'palco_tcp')
    if op == 'start':
        s.start()
        s.to_match()
        print('na partida', s.fps())
    elif op == 'mod':
        print(s.cmd({'cmd': 'mod_cmd', 'name': sys.argv[2], 'args': sys.argv[3] if len(sys.argv) > 3 else ''}).get('reply'))
    elif op == 'shot':
        os.makedirs(OUT, exist_ok=True)
        print(present_shot(s, os.path.join(OUT, sys.argv[2] + '.png')))
        if len(sys.argv) > 3 and sys.argv[3] == 'hr':
            print(s.shot(os.path.join(OUT, sys.argv[2] + '_hr.png')))
    elif op == 'raw':
        print(json.dumps(s.cmd(json.loads(sys.argv[2]), 30))[:4000])
    elif op == 'stop':
        try:
            s.cmd({'cmd': 'quit'}, 3)
        except OSError:
            pass
        subprocess.run(['powershell', '-NoProfile', '-Command',
                        'Stop-Process -Name Bust_A_Groove_2_Recompiled -Force -ErrorAction SilentlyContinue'])


if __name__ == '__main__':
    main()
