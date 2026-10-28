#!/usr/bin/env python3
"""A/B da geometria do palco no mesmo quadro (fase 6; build-debug, TCP 4370, 2 jogadores).

A câmera do palco anda até na pausa; então o quadro é fixado por savestate: salva um estado na partida e, para cada
variante do stage_geo, carrega o mesmo estado e captura 60 quadros depois. O laço principal do jogo chama rand()
enquanto espera o DrawSync, e a troca dos pacotes muda esse tempo: com o remake ligado e desligado o sorteio diverge
(peças com movimento aleatório, como a gaiola do palco do HEAT, saem em outro lugar); câmera e dançarinos batem. Para cada palco pedido entra
na partida (HEAT x COMET), espera e tira uma captura por variante (padrão: original, remake só com a textura HD e a cor
do jogo, remake completo), mais recortes do centro em tamanho real. Slot 9 do savestate (não é o do usuário).
Saída em tools/recomp/logs/remake/palcos/pausa_<k>_<variante>.png e a montagem pausa_<k>.png.
Uso: python tools/remake/stage_geo_pause.py [palcos separados por vírgula] [segundos até a pausa] ["var1|var2|..."]
     (variante = args do stage_geo; "0" desliga)
"""
from __future__ import annotations

import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from game_session import Session, REPO  # noqa: E402

OUT = os.path.join(REPO, 'tools', 'recomp', 'logs', 'remake', 'palcos')


def main():
    stages = [int(x) for x in (sys.argv[1] if len(sys.argv) > 1 else '0').split(',')]
    wait = float(sys.argv[2]) if len(sys.argv) > 2 else 30.0
    variants = (sys.argv[3] if len(sys.argv) > 3 else '0|1 1|1 0').split('|')
    os.makedirs(OUT, exist_ok=True)
    s = Session(label='palcos_pausa')
    s.start()
    try:
        for i, k in enumerate(stages):
            if i == 0:
                s.to_match_2p('HEAT', 'COMET', k)
            else:
                s.menu_to_match_2p('HEAT', 'COMET', k)
            time.sleep(wait)
            print(k, 'salvar', s.cmd({'cmd': 'savestate', 'slot': 9, 'op': 'save'}).get('ok'), flush=True)
            time.sleep(0.5)
            s.cmd({'cmd': 'savestate', 'slot': 9, 'op': 'load'})   # aquecimento: a 1ª carga demora mais
            time.sleep(1.0)
            paths = []
            for v in variants:
                s.cmd({'cmd': 'mod_cmd', 'name': 'stage_geo', 'args': v})
                s.cmd({'cmd': 'savestate', 'slot': 9, 'op': 'load'})
                f0 = s.cmd({'cmd': 'frame'}).get('frame', 0)
                # o mesmo nº de quadros depois da carga; as primeiras OTs depois dela foram montadas antes (sem remake)
                while s.cmd({'cmd': 'frame'}).get('frame', 0) < f0 + 60:
                    time.sleep(0.005)
                p = os.path.join(OUT, f'pausa_{k:02d}_{v.replace(" ", "_")}.png')
                s.shot(p)
                paths.append(p)
                st = s.cmd({'cmd': 'mod_cmd', 'name': 'stage_geo', 'args': 'sz'}).get('reply', '')
                print(k, v, st[:70], st[st.find('palco'):][:90], flush=True)
            s.cmd({'cmd': 'mod_cmd', 'name': 'stage_geo', 'args': '1 0'})
            print(k, s.cmd({'cmd': 'mod_cmd', 'name': 'stage_geo', 'args': 's'}).get('reply', ''), flush=True)
            montage(paths, os.path.join(OUT, f'pausa_{k:02d}.png'))
            s.quit_match()
    finally:
        s.stop()


def montage(paths, out):
    from PIL import Image
    ims = [Image.open(p).convert('RGB') for p in paths if os.path.exists(p)]
    if not ims:
        return
    W, H = ims[0].size
    w, h = W // 2, H // 2
    m = Image.new('RGB', (w * len(ims), h * 2))
    for c, im in enumerate(ims):
        m.paste(im.resize((w, h)), (c * w, 0))
        m.paste(im.crop((W // 4, H // 4, W // 4 + w, H // 4 + h)), (c * w, h))   # centro em tamanho real
    m.save(out)


if __name__ == '__main__':
    main()
