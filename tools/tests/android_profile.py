#!/usr/bin/env python3
"""Perfil de CPU do jogo no tablet com simpleperf (docs/android-apk.md §7; o manifesto tem profileable).

Entra na partida (BAG2_MATCH, padrão o palco do ROBO-Z), manda os comandos de `pré` e grava 10 s do processo inteiro
(`simpleperf record --app ... -g`; no Samsung só o evento padrão funciona). O .data vem para logs/remake/android/.
Relatório no PC (símbolos da .so sem strip em game/build-android):
    cd logs/remake/android
    python <NDK>/simpleperf/binary_cache_builder.py -i <rótulo>.data -lib ../../../game/build-android
    <NDK>/simpleperf/bin/windows/x86_64/simpleperf.exe report -i <rótulo>.data --symfs binary_cache --sort comm,tid
    ... --tids <tid da render thread> --children --sort symbol
Uso: python tools/tests/android_profile.py [rótulo] ["comando args;comando args"]
"""
import os
import subprocess
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from android_session import ADB, PKG, AndroidSession, adb  # noqa: E402
import game_session as gs  # noqa: E402

OUT = os.path.join(gs.REPO, 'logs', 'remake', 'android')
SP = os.path.join(os.environ.get('ANDROID_NDK_HOME', r'C:\tools\android-sdk\ndk\28.2.13676358'),
                  'simpleperf', 'bin', 'android', 'arm64', 'simpleperf')
os.environ.setdefault('BAG2_UNLOCK', '1')
os.environ.setdefault('BAG2_STAGE_IDX', '11')
os.environ.setdefault('BAG2_MATCH', 'HEAT ROBO-Z_GOLD 0')


def main():
    tag = sys.argv[1] if len(sys.argv) > 1 else 'perfil'
    pre = sys.argv[2] if len(sys.argv) > 2 else ''
    os.makedirs(OUT, exist_ok=True)
    adb('push', SP, '/data/local/tmp/simpleperf')
    adb('shell', 'chmod', '755', '/data/local/tmp/simpleperf')
    s = AndroidSession(label=tag)
    s.start()
    try:
        print('personagens', s.enter_match_checked(), flush=True)
        for c in filter(None, pre.split(';')):
            n, _, a = c.strip().partition(' ')
            print(n, s.cmd({'cmd': 'mod_cmd', 'name': n, 'args': a}).get('reply', '')[:200], flush=True)
        time.sleep(8)
        r = subprocess.run([ADB, 'shell', '/data/local/tmp/simpleperf', 'record', '--app', PKG, '-g', '--duration', '10',
                            '-o', f'/data/local/tmp/{tag}.data'], capture_output=True, text=True, timeout=120)
        print(r.stdout[-2000:], r.stderr[-2000:], flush=True)
        adb('pull', f'/data/local/tmp/{tag}.data', os.path.join(OUT, f'{tag}.data'), check=False)
    finally:
        s.stop()


if __name__ == '__main__':
    main()
