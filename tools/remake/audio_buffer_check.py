#!/usr/bin/env python3
"""Buffer de saída do áudio ([audio] buffer_ms do game.toml): atraso do som x estalos (build-debug, TCP 4370).

O padrão do framework é 180 ms, que num jogo de ritmo põe a batida ouvida ~190 ms depois da batida que o jogo julga.
Para cada valor pedido, roda uma sessão inteira (abertura, título, menu, seleção, carregamento, partida até o fim
da música e a tela seguinte) com um game.toml temporário na build-debug, amostra o `audio_stats` a cada ~0,5 s e
relata, por fase: enchimento do buffer (fill_ms) e underruns (cada um é um trecho de silêncio com fade, ouvido
como estalo). O valor certo é o menor sem underrun em nenhuma fase.
Uso: python tools/remake/audio_buffer_check.py "60|90" [segundos depois da partida começar]
"""
from __future__ import annotations

import os
import re
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from game_session import Session, BUILD, START, CROSS  # noqa: E402


def toml_with_buffer(ms):
    src = open(os.path.join(BUILD, 'game.toml'), encoding='utf-8').read()
    src = re.sub(r'(?ms)^\[audio\].*?(?=^\[|\Z)', '', src)
    name = f'game_buf{ms}.toml'
    with open(os.path.join(BUILD, name), 'w', encoding='utf-8', newline='\n') as f:
        f.write(src.rstrip() + f'\n\n[audio]\nbuffer_ms = {ms}\n')
    return name


def main():
    values = [int(v) for v in (sys.argv[1] if len(sys.argv) > 1 else '60|90').split('|')]
    after = float(sys.argv[2]) if len(sys.argv) > 2 else 175.0
    for ms in values:
        s = Session(game=toml_with_buffer(ms), label=f'buf{ms}')
        s.start()
        phases, cur, last = {}, ['boot'], [None]
        def sample():
            o = s.cmd({'cmd': 'audio_stats'}).get('out', {})
            u = o.get('underruns', 0) or 0
            ph = phases.setdefault(cur[0], {'fill': [], 'du': 0})
            ph['fill'].append(o.get('fill_ms') or 0.0)
            if last[0] is not None:
                ph['du'] += u - last[0]          # o que entrou desde a amostra anterior vai para a fase atual
            last[0] = u
            return o
        def wait_until(t):
            while time.time() - s.t0 < t:
                sample(); time.sleep(0.5)
        try:
            o = sample()
            print(f'buffer {ms} ms: alvo {o.get("target_ms")} ms', flush=True)
            wait_until(70); cur[0] = 'abertura/título'
            wait_until(75); s.press(START); cur[0] = 'menu/seleção'
            wait_until(78); s.press(CROSS)
            wait_until(81); s.press(CROSS)
            wait_until(84); s.press(CROSS); cur[0] = 'carregamento'
            wait_until(100); cur[0] = 'partida'
            wait_until(100 + after - 20); cur[0] = 'fim da música/tela seguinte'
            wait_until(100 + after)
            total = 0
            for k, v in phases.items():
                f = v['fill'] or [0.0]
                du = v['du']
                total += du
                print(f'  {k:28s} fill méd {sum(f) / len(f):6.1f} mín {min(f):6.1f} ms | underruns +{du}', flush=True)
            print(f'  total underruns {total} | {s.fps()}', flush=True)
        finally:
            s.stop()


if __name__ == '__main__':
    main()
