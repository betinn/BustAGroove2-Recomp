#!/usr/bin/env python3
"""Automação da build-debug do BAG2 (servidor TCP 4370) para testar o remake do HEAT no jogo.

Uso como módulo:
    s = Session(); s.start(); s.to_match()   # abre o exe, passa abertura/menus e entra numa partida
    s.cmd({'cmd': 'mod_cmd', 'name': 'heat_dump', 'args': 'C:/.../dump.txt'})
    s.shot('C:/.../tela.png'); s.stop()
Uso direto: python tools/remake/game_session.py [segundos_de_partida]  (só entra na partida e tira capturas)

Rotas:
- `to_match()`: 1 jogador (START, X três vezes; o oponente e o Stage 1 são sorteados, o cursor começa no
  personagem que o cartão lembra).
- `to_match_2p(p1, p2, palco)`: **2 jogadores** (padrão dos testes desde a fase 4): os dois personagens e o palco
  escolhidos, partida repetível. O pad 2 fica ligado por `set_input` persistente na porta 2 (um `press` na porta 2
  expira e o jogo pausa com "CONTROLLER REMOVED"). Grade de seleção (cartão do usuário, 10 liberados):
      linha 0:        HEAT  COMET          (sobre as colunas 1 e 2)
      linha 1: SHORTY STRIKE TSUTOMU CAPOEIRA
      linha 2: BI-O   HIRO   KITTY-N KELLY
      (com BAG2_UNLOCK=1, os 18:) linha 3: COLUMBO ROBO-Z_GOLD PANDER SUSHI_BOY; linha 4: McLOAD CHICHI&SALLY
      MICHAEL_DOI HUSTLE_KONG
  O cursor vai primeiro à âncora BI-O (baixo, baixo, esquerda × 3: a grade não dá a volta) e de lá ao alvo,
  um passo por toque de 6 quadros (15 quadros andam 2 casas). Palco: a tela de palco começa no palco do
  personagem do 1P; `palco` = nº de toques para a direita. Depois da partida carregar, `who()` confere os
  personagens pela assinatura do nó 0 (estados dos jogadores em 0x80060E30 e 0x80060E38).
- Partida inteira: a música dura ~150 s a partir de ~t = 100 s. Para encurtar: `quit_match()` (pausa, QUIT: menu
  principal) e `menu_to_match_2p(p1, p2, palco)` para a próxima partida na mesma sessão.
"""
from __future__ import annotations

import json
import os
import shutil
import socket
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, '..', '..'))
BUILD = os.path.join(REPO, 'tools', 'recomp', 'bag2', 'build-debug')
LOGS = os.path.join(REPO, 'tools', 'recomp', 'logs', 'remake', 'jogo')
DISC = os.path.join(REPO, 'disc', 'Bust A Groove 2 (USA).cue')   # disco local (fora do git; disc/README.md)
START, CROSS, CIRCLE = 0xFFF7, 0xBFFF, 0xDFFF
UP, DOWN, LEFT, RIGHT = 0xFFEF, 0xFFBF, 0xFF7F, 0xFFDF
# caminho na grade da seleção do 2 jogadores a partir da âncora BI-O (linha 2, coluna 0)
GRID_2P = {'BI-O': '', 'HIRO': 'R', 'KITTY-N': 'RR', 'KELLY': 'RRR', 'SHORTY': 'U', 'STRIKE': 'UR',
           'TSUTOMU': 'URR', 'CAPOEIRA': 'URRR', 'HEAT': 'URU', 'COMET': 'URRU'}
# com os 18 liberados (BAG2_UNLOCK=1) a grade ganha duas linhas abaixo do BI-O
GRID_2P.update({'COLUMBO': 'D', 'ROBO-Z GOLD': 'DR', 'PANDER': 'DRR', 'SUSHI BOY': 'DRRR', 'McLOAD': 'DD',
                'CHICHI&SALLY': 'DDR', 'MICHAEL DOI': 'DDRR', 'HUSTLE KONG': 'DDRRR'})
# assinatura do personagem na RAM: nº de vértices do mesh do nó 0 e índice-base de normais do 1º osso seguinte
CHAR_SIG = {(43, 52): 'BI-O', (33, 33): 'CAPOEIRA', (43, 82): 'COMET', (56, 90): 'HEAT', (44, 55): 'HIRO',
            (49, 82): 'KELLY', (49, 106): 'KITTY-N', (37, 43): 'PANDER', (45, 45): 'ROBO-Z GOLD',
            (35, 47): 'SHORTY', (115, 139): 'STRIKE', (51, 68): 'TSUTOMU', (182, 297): 'CHICHI&SALLY',
            (54, 54): 'COLUMBO', (39, 38): 'HUSTLE KONG', (44, 84): 'McLOAD', (80, 109): 'MICHAEL DOI',
            (50, 114): 'SUSHI BOY',
            # figurino alternativo (o do jogador 2 numa partida espelhada), quando o modelo muda
            (67, 141): 'COMET*', (35, 50): 'HEAT*', (76, 187): 'HIRO*', (36, 58): 'KELLY*', (49, 103): 'KITTY-N*',
            (53, 53): 'SHORTY*', (110, 77): 'STRIKE*'}
PLAYER_STATE = (0x80060E30, 0x80060E38)
# "Unlock All Characters" do CodeBreaker (NTSC-U): 8 bytes = 1 a partir de 0x8004F667 (PANDER, ROBO-Z GOLD, COLUMBO,
# SUSHI BOY, CHICHI&SALLY, MICHAEL DOI, McLOAD, HUSTLE KONG). Com BAG2_UNLOCK=1 a sessão escreve na RAM antes do menu;
# o jogo grava o cartão depois das partidas, então start() guarda uma cópia dos cartões e stop() a devolve.
UNLOCK = range(0x8004F667, 0x8004F66F)
CARDS = os.path.join(REPO, 'tools', 'recomp', 'bag2', 'saves')
# Índice do palco escolhido (byte) e as cópias da partida: 0 = 234, 1 = 243, 2 = 233, 3 = 244, 4 = 236, 5 = 242, 6 = 238,
# 7 = 237, 8 = 235, 9 = 231, 10 = 232, 11 = 241, 12 = 245 (os dois últimos não estão na tela de palco). Com
# BAG2_STAGE_IDX=<índice> a rota do 2 jogadores escreve o índice logo depois de confirmar o palco (tools/remake/stage_hidden.py).
STAGE_IDX = (0x8005FB56, 0x8008A308, 0x8008A350)


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
        self.unlock = os.environ.get('BAG2_UNLOCK') == '1'
        if self.unlock:   # cópia dos cartões: o jogo grava o desbloqueio neles depois das partidas
            self.card_backup = os.path.join(LOGS, 'cartao_backup')
            os.makedirs(self.card_backup, exist_ok=True)
            for f in ('card1.mcd', 'card2.mcd'):
                shutil.copy2(os.path.join(CARDS, f), os.path.join(self.card_backup, f))
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
        self.t_match = 84 + 0.5 * len(os.environ.get('BAG2_SELECT', ''))
        self.at(100)

    def press2(self, mask, hold=0.12):
        """Toque no pad 2: set_input persistente (o pad continua ligado depois), solto em `hold` segundos."""
        self.cmd({'cmd': 'set_input', 'buttons': f'{mask:04X}', 'port': 2, 'connected': 1})
        time.sleep(hold)
        self.cmd({'cmd': 'set_input', 'buttons': 'FFFF', 'port': 2, 'connected': 1})

    def enter_match(self, on_step=None):
        """Rota padrão dos testes: ambiente BAG2_MATCH="P1 P2 palco" (padrão "HEAT COMET 0": 2 jogadores, HEAT
        contra COMET no palco do HEAT) ou BAG2_MATCH=1p (rota antiga do 1 jogador). Deixa self.t_match = segundos
        desde o início em que a partida começou a carregar (a música começa ~16 s depois e dura ~150 s)."""
        spec = os.environ.get('BAG2_MATCH', 'HEAT COMET 0').split()
        if spec and spec[0].lower() == '1p':
            self.to_match()
            return
        p1, p2, st = (spec + ['HEAT', 'COMET', '0'][len(spec):])[:3]
        self.to_match_2p(p1, p2, int(st), on_step=on_step)
        print('partida:', self.who(), 'palco +', st, flush=True)

    def to_match_2p(self, p1='HEAT', p2='COMET', stage=0, menu_at=75.0, on_step=None):
        """Rota do 2 jogadores (ver o topo do arquivo). `stage` = toques para a direita na tela de palco.
        on_step(nome) é chamado em 'menu', 'selecao' e 'palco' (capturas da regressão). Deixa self.t_match."""
        self.cmd({'cmd': 'set_input', 'buttons': 'FFFF', 'port': 2, 'connected': 1})
        self.at(menu_at - 1)
        if getattr(self, 'unlock', False):
            self.unlock_all()
        self.at(menu_at); self.press(START)
        self.at(menu_at + 3)
        self.menu_to_match_2p(p1, p2, stage, on_step)

    def unlock_all(self):
        """Libera os 18 personagens na RAM (não no cartão; ver UNLOCK)."""
        for a in UNLOCK:
            self.cmd({'cmd': 'write_ram', 'addr': f'{a:08X}', 'val': '1'})

    def menu_to_match_2p(self, p1='HEAT', p2='COMET', stage=0, on_step=None):
        """Do menu principal com o cursor em "1player game" (depois do START na abertura ou do `quit_match`) até a
        partida do 2 jogadores começar a carregar. Deixa self.t_match."""
        moves = {'U': UP, 'D': DOWN, 'L': LEFT, 'R': RIGHT}
        up = {k.upper(): k for k in GRID_2P}
        p1, p2 = up.get(p1.upper(), p1), up.get(p2.upper(), p2)
        for name in (p1, p2):
            if name not in GRID_2P:
                raise ValueError(f'{name}: não está na grade do 2 jogadores ({", ".join(GRID_2P)})')
        step = on_step or (lambda name: None)
        self.cmd({'cmd': 'set_input', 'buttons': 'FFFF', 'port': 2, 'connected': 1})
        step('menu'); self.press(DOWN, 6)
        time.sleep(0.6); self.press(CROSS)
        time.sleep(3.0); step('selecao')
        for port, name in ((1, p1), (2, p2)):
            for c in 'DDLLL' + GRID_2P[name]:
                if port == 1:
                    self.press(moves[c], 6)
                else:
                    self.press2(moves[c])
                time.sleep(0.4)
        time.sleep(0.3)
        self.press(CROSS); time.sleep(0.8); self.press2(CROSS, 0.25)   # personagens
        time.sleep(1.5)
        self.press(CROSS); time.sleep(0.8); self.press2(CROSS, 0.25)   # dificuldade (NORMAL)
        time.sleep(2.0)
        for _ in range(stage):
            self.press(RIGHT, 6); time.sleep(0.5)
        step('palco')
        self.press(CROSS)
        self.t_match = time.time() - self.t0
        idx = os.environ.get('BAG2_STAGE_IDX')
        if idx:   # palco fora da tela de palco (241 = 11, 245 = 12): o índice em laço logo depois da confirmação
            t_end = time.time() + 4.0
            while time.time() < t_end:
                for a in STAGE_IDX:
                    self.cmd({'cmd': 'write_ram', 'addr': f'{a:08X}', 'val': f'{int(idx):X}'})
                time.sleep(0.01)
        time.sleep(16.0 if not idx else 12.0)

    def quit_match(self):
        """Pausa (START) e QUIT (baixo, X): o jogo volta ao menu principal com o cursor em "1player game". Na pausa
        os dançarinos não são desenhados (o estado dos jogadores continua)."""
        self.press(START, 6); time.sleep(1.0)
        self.press(DOWN, 6); time.sleep(0.6)
        self.press(CROSS, 6); time.sleep(4.0)

    def who(self):
        """Personagens em partida pela assinatura (None = estado vazio ou desconhecido): nº de vértices do mesh do nó 0
        e índice-base do 2º nó com mesh na ordem da árvore (como o plugin; juntas sem mesh no caminho não contam)."""
        import struct
        rd = lambda a: int.from_bytes(bytes.fromhex(self.cmd({'cmd': 'read_ram', 'addr': a, 'len': 4})['hex']), 'little')
        ok = lambda a: 0x80000000 <= a < 0x80200000
        out = []
        for p in PLAYER_STATE:
            st = rd(p)
            root = rd(st + 0x28) if ok(st) else 0
            if not ok(root):
                out.append(None); continue
            raw = bytes.fromhex(self.cmd({'cmd': 'read_ram', 'addr': root, 'len': 0xB0 * 48})['hex'])
            u32 = lambda a, off: struct.unpack_from('<I', raw, a - root + off)[0] if 0 <= a - root < len(raw) - 0xB0 else 0
            mesh_nodes = []
            def walk(a, depth=0):
                while a and depth < 64 and len(mesh_nodes) < 2 and 0 <= a - root < len(raw) - 0xB0:
                    if u32(a, 0xA0) == 1:
                        mesh_nodes.append(a)
                    walk(u32(a, 0x94), depth + 1)
                    a = u32(a, 0x98)
            walk(root)
            if len(mesh_nodes) < 2:
                out.append(None); continue
            m0, m1 = u32(mesh_nodes[0], 0x8C), u32(mesh_nodes[1], 0x8C)
            out.append(CHAR_SIG.get((rd(m0 + 0x94), rd(m1 + 0x98))) if ok(m0) and ok(m1) else None)
        return out

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
        if getattr(self, 'unlock', False):   # devolve os cartões do usuário
            time.sleep(1)
            for f in ('card1.mcd', 'card2.mcd'):
                shutil.copy2(os.path.join(self.card_backup, f), os.path.join(CARDS, f))


if __name__ == '__main__':
    # python game_session.py [segundos] [p1 p2 palco]   (com p1/p2: rota do 2 jogadores)
    secs = int(sys.argv[1]) if len(sys.argv) > 1 else 20
    s = Session(label='partida')
    s.start()
    try:
        if len(sys.argv) > 3:
            s.to_match_2p(sys.argv[2], sys.argv[3], int(sys.argv[4]) if len(sys.argv) > 4 else 0)
            print('personagens', s.who())
        else:
            s.to_match()
        for i in range(3):
            print(s.shot(os.path.join(LOGS, f'partida_{i}.png')))
            time.sleep(secs / 3)
        print(s.fps())
    finally:
        s.stop()
