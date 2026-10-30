#!/usr/bin/env python3
"""Buffer de saída do áudio ([audio] buffer_ms do game.toml): atraso do som x estalos (build-debug, TCP 4370).

O padrão do framework é 180 ms, que num jogo de ritmo põe a batida ouvida ~190 ms depois da batida que o jogo julga.
Para cada valor pedido, roda uma sessão inteira (abertura, título, menu, seleção, carregamento, partida até o fim
da música e a tela seguinte) com um game.toml temporário na build-debug, amostra o `audio_stats` a cada ~0,5 s e
relata, por fase: enchimento do buffer (fill_ms) e underruns (cada um é um trecho de silêncio com fade, ouvido
como estalo). O valor certo é o menor sem underrun em nenhuma fase.
Rota: 2 jogadores (BAG2_MATCH; a fase "carregamento" vai da tela de palco até a partida começar).
Uso: python tools/tests/audio_buffer_check.py "60|90" [segundos depois da partida começar]
"""
from __future__ import annotations

import os
import re
import sys
import threading
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'remake'))
from game_session import Session, BUILD  # noqa: E402


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
        phases, cur, last, stop = {}, ['boot'], [None], [False]
        def sample():
            o = s.cmd({'cmd': 'audio_stats'}).get('out', {})
            u = o.get('underruns', 0) or 0
            ph = phases.setdefault(cur[0], {'fill': [], 'du': 0})
            ph['fill'].append(o.get('fill_ms') or 0.0)
            if last[0] is not None:
                ph['du'] += u - last[0]          # o que entrou desde a amostra anterior vai para a fase atual
            last[0] = u
            return o
        def sampler():                           # amostra a cada ~0,5 s enquanto a rota anda
            while not stop[0]:
                try:
                    sample()
                except OSError:
                    pass
                time.sleep(0.5)
        def phase(name):
            cur[0] = name
        try:
            o = sample()
            print(f'buffer {ms} ms: alvo {o.get("target_ms")} ms', flush=True)
            th = threading.Thread(target=sampler, daemon=True)
            th.start()
            s.at(70); phase('abertura/título')
            # rota do 2 jogadores (BAG2_MATCH); fases: menu/seleção até a tela de palco, depois carregamento
            steps = {'menu': 'menu/seleção', 'palco': 'carregamento'}
            s.enter_match(on_step=lambda name: phase(steps[name]) if name in steps else None)
            s.at(s.t_match + 16); phase('partida')
            s.at(s.t_match + 16 + after - 20); phase('fim da música/tela seguinte')
            s.at(s.t_match + 16 + after)
            stop[0] = True
            th.join(2)
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
