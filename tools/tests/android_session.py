#!/usr/bin/env python3
"""Sessão de teste do APK Android (docs/android-apk.md) com a mesma API de tools/remake/game_session.py.

O jogo roda no tablet; o servidor TCP 4370 da build (PSX_DEBUG_TOOLS=ON) chega ao PC por `adb forward`, então
`cmd`, `press`, `to_match_2p`, `who`, `heat_*`/`stage_*` etc. funcionam iguais. O que muda:
  start()  — `adb forward tcp:4370 tcp:4370`, `am start` da BAG2Activity, espera o `ping`
  stop()   — `am force-stop`; puxa <files>/log.txt para logs/remake/jogo/<label>.log
  shot()   — `screenshot_hires` do servidor grava no aparelho (<files>/shots/) e o arquivo é puxado por adb;
             screencap() tira a captura da tela do Android (o que o jogador vê)
  fps()    — lê o log puxado (PSX_FRAME_REPORT_MS=1000 vem do manifesto, meta-data SDL_ENV.*)
Uso direto: python tools/tests/android_session.py [segundos] [p1 p2 palco]
"""
from __future__ import annotations

import os
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', 'remake'))
import game_session as gs  # noqa: E402

ADB = os.environ.get('ADB', r'C:\tools\android-sdk\platform-tools\adb.exe')
PKG = 'io.github.betinn.bag2'
FILES = f'/sdcard/Android/data/{PKG}/files'


def adb(*args, check=True, capture=False, timeout=120):
    r = subprocess.run([ADB, *args], check=check, capture_output=capture, text=True, timeout=timeout)
    return r.stdout if capture else None


class AndroidSession(gs.Session):
    def __init__(self, label='android'):
        super().__init__(build=None, game='game.toml', label=label)

    def start(self, env_extra=None):
        self.unlock = os.environ.get('BAG2_UNLOCK') == '1'
        os.makedirs(gs.LOGS, exist_ok=True)
        adb('forward', 'tcp:4370', 'tcp:4370')
        adb('shell', 'am', 'force-stop', PKG, check=False)
        adb('logcat', '-c', check=False)
        adb('shell', 'am', 'start', '-n', f'{PKG}/.SetupActivity')
        self.t0 = time.time()
        for _ in range(90):
            try:
                self.cmd({'cmd': 'ping'}, 2)
                if self.unlock:
                    for a in gs.UNLOCK:
                        self.cmd({'cmd': 'write_ram', 'addr': a, 'hex': '01'})
                return
            except OSError:
                time.sleep(1)
        raise RuntimeError('servidor TCP do tablet não respondeu (adb forward 4370)')

    def shot(self, path):
        name = os.path.basename(path)
        adb('shell', 'mkdir', '-p', f'{FILES}/shots')
        r = self.cmd({'cmd': 'screenshot_hires', 'path': f'{FILES}/shots/{name}'}, 60)
        adb('pull', f'{FILES}/shots/{name}', path, check=False)
        return r

    def screencap(self, path):
        with open(path, 'wb') as f:
            subprocess.run([ADB, 'exec-out', 'screencap', '-p'], stdout=f, check=True)

    def pull_log(self):
        dst = os.path.join(gs.LOGS, f'{self.label}.log')
        adb('pull', f'{FILES}/log.txt', dst, check=False)
        return dst

    def fps(self):
        self.pull_log()
        return super().fps()

    def logcat(self, lines=200):
        return adb('logcat', '-d', '-t', str(lines), '-s', 'psxrecomp:V', 'SDL:V', 'AndroidRuntime:E', 'DEBUG:I',
                   'libc:F', capture=True, check=False)

    def stop(self):
        try:
            self.cmd({'cmd': 'quit'}, 3)
        except OSError:
            pass
        time.sleep(2)
        adb('shell', 'am', 'force-stop', PKG, check=False)
        self.pull_log()


if __name__ == '__main__':
    secs = int(sys.argv[1]) if len(sys.argv) > 1 else 20
    s = AndroidSession(label='android_partida')
    s.start()
    try:
        if len(sys.argv) > 3:
            s.to_match_2p(sys.argv[2], sys.argv[3], int(sys.argv[4]) if len(sys.argv) > 4 else 0)
            print('personagens', s.who())
        else:
            s.to_match()
        for i in range(3):
            print(s.shot(os.path.join(gs.LOGS, f'android_partida_{i}.png')))
            time.sleep(secs / 3)
        print(s.fps())
    finally:
        s.stop()
