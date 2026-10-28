#!/usr/bin/env python3
"""Regressão e custo do remake do palco (fase 3; build-debug, TCP 4370).

1. Abertura (FMV), título, menu e seleção: present_shot e contador de marcadores do palco (tem de ficar 0).
2. Partida: tela cheia (tamanho da present_shot), A/B rápido com o efeito (stage_post "1 0" x "0 0"), custo do GP0
   na thread do jogo (`dma_hold_stats`: tempo de host das caminhadas da OT) e divisão das palavras GP0 do quadro por
   origem dos pacotes (palco, personagens, HUD), de `gpu_frame_dump`.
3. Fim da música e tela de resultado: A/B a cada ~12 s até `fim` segundos, para ver se o efeito chega lá e se o
   HUD do resultado fica intacto.
Saída: tools/recomp/logs/remake/palco/chk_*.png e chk.json.  Uso: python tools/remake/stage_check.py [fim_s]
"""
from __future__ import annotations

import json
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, r'C:/tools/psxrecomp/tools')
from game_session import Session, REPO, START, CROSS  # noqa: E402
from stage_beat import present_shot  # noqa: E402
import psx_gpu_frame as pgf  # noqa: E402

OUT = os.path.join(REPO, 'tools', 'recomp', 'logs', 'remake', 'palco')
REGIONS = [('fundo/chão 0x800D', 0x800D0000, 0x800E0000), ('céu/letreiros/HUD 0x8008', 0x80080000, 0x8008C000),
           ('adereços 3D 0x800B-0x800C', 0x800B0000, 0x800D0000), ('outro dançarino 0x801A', 0x801A0000, 0x801B4000),
           ('sombras', 0x80140000, 0x80148000), ('sombras', 0x801B4000, 0x801B8000), ('arena do plugin', 0x80800000, 0x81000000)]


def markers(s):
    for c in s.cmd({'cmd': 'mod_counters'})['counters']:
        if c['name'] == 'bag2_stage.markers':
            return c['count']
    return 0


def shot(s, name, log):
    p = os.path.join(OUT, f'chk_{name}.png')
    present_shot(s, p)
    try:
        from PIL import Image
        size = Image.open(p).size
    except Exception:
        size = None
    log.append({'shot': name, 'markers': markers(s), 'size': size, 't': round(time.time() - s.t0, 1)})
    print(log[-1], flush=True)


def gp0_split(conn):
    d = pgf.capture(conn, label='custo')
    words = {}
    total = 0
    for p in d['prims']:
        n = p.get('n_words', 1) + 1           # + tag do pacote
        a = int(p['src'], 16) | 0x80000000
        name = next((r[0] for r in REGIONS if r[1] <= a < r[2]), 'outros')
        if p['ot'] >= 4097:
            name = 'HUD (OT do HUD)'
        words[name] = words.get(name, 0) + n
        total += n
    return d['frame'], total, words


def main():
    end = float(sys.argv[1]) if len(sys.argv) > 1 else 300.0
    os.makedirs(OUT, exist_ok=True)
    s = Session(label='palco_check')
    s.start()
    log = []
    try:
        s.at(20); shot(s, 'fmv', log)
        s.at(72); shot(s, 'titulo', log)
        s.press(START); s.at(77); shot(s, 'menu', log)
        s.press(CROSS); s.at(80); shot(s, 'selecao', log)
        s.press(CROSS); s.at(83); s.press(CROSS)
        s.at(102)
        shot(s, 'partida_tudo', log)
        s.cmd({'cmd': 'mod_cmd', 'name': 'stage_post', 'args': '0 0'}); time.sleep(0.1)
        shot(s, 'partida_orig', log)
        s.cmd({'cmd': 'mod_cmd', 'name': 'stage_post', 'args': '1 8'}); time.sleep(0.1)
        shot(s, 'partida_split', log)
        s.cmd({'cmd': 'mod_cmd', 'name': 'stage_post', 'args': '1 0'})
        # custo do GP0 na thread do jogo e divisão por origem
        f0 = s.cmd({'cmd': 'frame'})['frame']; h0 = s.cmd({'cmd': 'dma_hold_stats'})
        time.sleep(10)
        f1 = s.cmd({'cmd': 'frame'})['frame']; h1 = s.cmd({'cmd': 'dma_hold_stats'})
        conn = pgf.DebugConn()
        splits = [gp0_split(conn) for _ in range(3) if time.sleep(0.4) is None]
        log.append({'dma0': h0, 'dma1': h1, 'frames': f1 - f0, 'gp0': splits})
        print('dma', {k: (h1.get(k, 0) - h0.get(k, 0)) for k in h1 if isinstance(h1.get(k), (int, float))}, 'quadros', f1 - f0)
        for fr, tot, w in splits:
            print('quadro', fr, 'palavras', tot, {k: f'{100 * v / tot:.0f}%' for k, v in sorted(w.items(), key=lambda x: -x[1])})
        k = 0
        while time.time() - s.t0 < end:
            s.at(225 + 12 * k)
            shot(s, f'fim{k}_tudo', log)
            s.cmd({'cmd': 'mod_cmd', 'name': 'stage_post', 'args': '0 0'}); time.sleep(0.1)
            shot(s, f'fim{k}_orig', log)
            s.cmd({'cmd': 'mod_cmd', 'name': 'stage_post', 'args': '1 0'})
            k += 1
        with open(os.path.join(OUT, 'chk.json'), 'w', encoding='utf-8') as f:
            json.dump(log, f, indent=1, default=str)
        print(s.fps())
    finally:
        s.stop()


if __name__ == '__main__':
    main()
