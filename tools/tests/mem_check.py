#!/usr/bin/env python3
"""Memória do elenco (fase 5; build-debug, TCP 4370): uma sessão com várias partidas do 2 jogadores seguidas.

Para cada par "P1,P2,palco": entra na partida (a primeira pela abertura, as outras pelo menu depois do QUIT), mede
20 s depois, pausa e sai (QUIT) e mede no menu principal 4, 8 e 14 s depois. Em cada ponto: memória dedicada da GPU
do processo (contador do Windows "GPU Process Memory", em MB), memória privada do processo (MB) e o resumo do
`heat_chars` (assets carregados, GPU e CPU que o plugin pediu, quem está carregado).
Uso: python tools/tests/mem_check.py ["P1,P2,palco|..."] [descarga 0|1]
     descarga 0 = `heat_evict 0` (nada descarrega: o comportamento da fase 4, para comparar na mesma build).
Saída: logs/remake/jogo/mem_<descarga>.txt (acumula).
"""
from __future__ import annotations

import os
import re
import subprocess
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'remake'))
from game_session import LOGS, Session  # noqa: E402

DEFAULT = 'HEAT,COMET,0|KITTY-N,HIRO,1|SHORTY,STRIKE,0|BI-O,BI-O,0|TSUTOMU,KELLY,0|HEAT,COMET,0'


def proc_mem(pid: int) -> tuple[float, float]:
    """(GPU dedicada, privada) em MB. O nome do contador da GPU não é traduzido; o do processo vem do Get-Process."""
    ps = (f"$g = (Get-Counter '\\GPU Process Memory(pid_{pid}*)\\Dedicated Usage' -ErrorAction SilentlyContinue)."
          f"CounterSamples | Measure-Object CookedValue -Sum; $p = Get-Process -Id {pid} -ErrorAction SilentlyContinue;"
          "'{0} {1}' -f $g.Sum, $p.PrivateMemorySize64")
    out = subprocess.run(['powershell', '-NoProfile', '-Command', ps], capture_output=True, text=True).stdout.split()
    try:
        return float(out[0]) / 2**20, float(out[1]) / 2**20
    except (IndexError, ValueError):
        return -1.0, -1.0


def chars_summary(s) -> str:
    r = s.cmd({'cmd': 'mod_cmd', 'name': 'heat_chars', 'args': ''}).get('reply', '')
    head = r.split('||')[0]
    m = re.search(r'carregados=(\d+) gpu_mb=(\d+) cpu_mb=(\d+) gl_apagados=(\d+)', head)
    loaded = re.findall(r'\| (\S+(?: \S+)?) estado=([123])', head)
    return (f'carregados={m.group(1)} plugin_gpu={m.group(2)} MB plugin_cpu={m.group(3)} MB gl_apagados={m.group(4)} '
            f'[{", ".join(n + ("" if e == "2" else f"({e})") for n, e in loaded)}]') if m else head[:200]


def main():
    pairs = (sys.argv[1] if len(sys.argv) > 1 else DEFAULT).split('|')
    evict = sys.argv[2] if len(sys.argv) > 2 else '1'
    out = open(os.path.join(LOGS, f'mem_{evict}.txt'), 'a', encoding='utf-8')

    def log(*a):
        line = ' '.join(str(x) for x in a)
        print(line, flush=True)
        out.write(line + '\n'); out.flush()

    s = Session(label=f'mem_{evict}')
    s.start()
    pid = s.p.pid
    log(f'=== sessão {time.strftime("%Y-%m-%d %H:%M")}, descarga={evict}, pares {pairs}')

    def sample(tag):
        g, p = proc_mem(pid)
        log(f'{tag:28s} gpu_dedicada={g:6.0f} MB privada={p:6.0f} MB  {chars_summary(s)}')

    try:
        s.at(8)
        s.cmd({'cmd': 'mod_cmd', 'name': 'heat_evict', 'args': evict})
        s.at(45)                               # TIMs de personagem que o jogo pôs na VRAM durante a abertura
        vr = [p for p in s.cmd({'cmd': 'mod_cmd', 'name': 'heat_chars', 'args': ''}).get('reply', '').split('||')
              if 'vram_tim' in p]
        log('abertura (t=45)', vr[0].strip() if vr else '?')
        s.at(70); sample('abertura (t=70)')
        for k, pair in enumerate(pairs):
            p1, p2, st = (pair.split(',') + ['0'])[:3]
            if k == 0:
                s.to_match_2p(p1, p2, int(st))
            else:
                s.menu_to_match_2p(p1, p2, int(st))
            time.sleep(4)
            sample(f'{p1}x{p2} partida+20')
            log('   who', s.who(), s.fps())
            log('  ', s.cmd({'cmd': 'mod_cmd', 'name': 'heat_hits', 'args': ''}).get('reply', '?'))
            s.quit_match()                     # volta 4 s depois do QUIT
            sample('   menu +4s')
            time.sleep(4); sample('   menu +8s')
            time.sleep(6); sample('   menu +14s')
    finally:
        s.stop()


if __name__ == '__main__':
    main()
