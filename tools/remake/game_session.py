#!/usr/bin/env python3
"""Automação da build-debug do BAG2 (servidor TCP 4370) para testar o remake do HEAT no jogo.

Uso como módulo:
    s = Session(); s.start(); s.to_match()   # abre o exe, passa abertura/menus e entra numa partida
    s.cmd({'cmd': 'mod_cmd', 'name': 'heat_dump', 'args': 'C:/.../dump.txt'})
    s.shot('C:/.../tela.png'); s.stop()
Uso direto: python tools/remake/game_session.py [segundos_de_partida]  (só entra na partida e tira capturas)
A rota de botões é a de tools/recomp/bag2perf.py (START, depois X três vezes; o cursor da seleção começa
no HEAT).
"""
from __future__ import annotations

import json
import os
import socket
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, '..', '..'))
BUILD = os.path.join(REPO, 'tools', 'recomp', 'bag2', 'build-debug')
LOGS = os.path.join(REPO, 'tools', 'recomp', 'logs', 'remake', 'jogo')
DISC = r'C:\Users\sem-m\OneDrive\Documentos\My Games\Bust a groove 2\Bust A Groove 2 (USA).cue'
START, CROSS, CIRCLE = 0xFFF7, 0xBFFF, 0xDFFF


class Session:
    def __init__(self, build=BUILD, game='game.toml', label='run'):
        self.build, self.game, self.label = build, game, label
        self.p = None
        self._id = 0
        os.makedirs(LOGS, exist_ok=True)

    def cmd(self, obj, timeout=10.0):
        self._id += 1
        obj = dict(obj, id=self._id)
        with socket.create_connection(('127.0.0.1', 4370), timeout=timeout) as s:
            s.sendall((json.dumps(obj) + '\n').encode())
            buf = b''
            while not buf.endswith(b'\n'):
                c = s.recv(1 << 20)
                if not c:
                    break
                buf += c
        return json.loads(buf.decode(errors='replace') or '{}')

    def start(self, env_extra=None):
        self.log = open(os.path.join(LOGS, f'{self.label}.log'), 'w')
        env = dict(os.environ, PSX_FRAME_REPORT_MS='1000', **(env_extra or {}))
        self.p = subprocess.Popen([os.path.join(self.build, 'Bust_A_Groove_2_Recompiled.exe'), '--game', self.game,
                                   '--disc', DISC], cwd=self.build, stdout=self.log, stderr=subprocess.STDOUT, env=env)
        self.t0 = time.time()
        for _ in range(60):
            try:
                self.cmd({'cmd': 'ping'}, 2)
                return
            except OSError:
                time.sleep(1)
        raise RuntimeError('servidor TCP não respondeu')

    def at(self, t):
        d = self.t0 + t - time.time()
        if d > 0:
            time.sleep(d)

    def press(self, mask, frames=15):
        return self.cmd({'cmd': 'press', 'buttons': mask, 'frames': frames})

    def to_match(self):
        """Rota do bag2perf.py: abertura (~1 min), START, X (modo), X (personagem = HEAT), X.
        O jogo grava no cartão o último personagem escolhido e o cursor da seleção começa nele: BAG2_SELECT no
        ambiente (U/D/L/R) move o cursor antes do X (ex.: BAG2_SELECT=U volta ao HEAT depois de um dump com "D")."""
        self.at(75); self.press(START); self.at(78); self.press(CROSS)
        self.at(80.5)
        moves = {'U': 0xFFEF, 'D': 0xFFBF, 'L': 0xFF7F, 'R': 0xFFDF}
        for c in os.environ.get('BAG2_SELECT', ''):
            self.press(moves[c]); time.sleep(0.5)
        self.at(81 + 0.5 * len(os.environ.get('BAG2_SELECT', ''))); self.press(CROSS)
        self.at(84 + 0.5 * len(os.environ.get('BAG2_SELECT', ''))); self.press(CROSS)
        self.at(100)

    def shot(self, path):
        return self.cmd({'cmd': 'screenshot_hires', 'path': path.replace('\\', '/')}, 30)

    def fps(self):
        try:
            with open(os.path.join(LOGS, f'{self.label}.log'), errors='replace') as f:
                lines = [l for l in f if 'fps=' in l]
            return lines[-1].strip() if lines else '?'
        except OSError:
            return '?'

    def stop(self):
        try:
            self.cmd({'cmd': 'quit'}, 3)
        except OSError:
            pass
        time.sleep(2)
        subprocess.run(['powershell', '-NoProfile', '-Command',
                        'Stop-Process -Name Bust_A_Groove_2_Recompiled -Force -ErrorAction SilentlyContinue'])


if __name__ == '__main__':
    secs = int(sys.argv[1]) if len(sys.argv) > 1 else 20
    s = Session(label='partida')
    s.start()
    try:
        s.to_match()
        for i in range(3):
            print(s.shot(os.path.join(LOGS, f'partida_{i}.png')))
            time.sleep(secs / 3)
        print(s.fps())
    finally:
        s.stop()
