#!/usr/bin/env python3
"""Capturas original × melhorado do mesmo quadro, para a apresentação do README (build-debug, TCP 4370).

1. Sessão melhorada (game.toml e mods da build-debug, tudo ligado): para cada cena entra numa partida do 2 jogadores,
   espera, salva um savestate, carrega o mesmo estado e captura a tela larga na resolução interna
   (`screenshot_wide_hires`). Cada estado é copiado para logs/comparacao/<cena>.pst.
2. Sessão original: renderizador de software na resolução nativa, 4:3, sem filtro, com o dithering do PS1, sem PGXP e
   sem tela larga nem texturas HD (mods/state.toml trocado durante a sessão e devolvido no fim) e com o remake desligado
   por comando (heat_mode 0, stage_geo 0, stage_post 0: o jogo desenha tudo como no PS1). Cada estado é
   posto de volta no slot, carregado, e a captura é o quadro do PS1 decodificado da VRAM (`screenshot_file`).

As duas capturas de uma cena saem do mesmo estado, o mesmo número de quadros depois da carga. O laço do jogo chama
rand() enquanto espera o DrawSync, então peças com movimento aleatório podem sair em outro lugar; câmera, dançarinos e
HUD batem.
Saída: logs/comparacao/<cena>_original.png e <cena>_melhorado.png (montagem: tools/tests/compare_montage.py).
Uso: python tools/tests/compare_shots.py [cenas separadas por vírgula]   (nomes de SCENES; padrão: todas)
     COMPARE_ONLY=original|melhorado roda só uma das sessões (a outra reaproveita os .pst da pasta).
"""
from __future__ import annotations

import os
import re
import shutil
import sys
import time

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'remake'))
from game_session import Session, REPO, BUILD  # noqa: E402

OUT = os.path.join(REPO, 'logs', 'comparacao')
SAVES = os.path.join(REPO, 'game', 'saves', 'openbios')
SLOT = 10
SLOT_FILE = os.path.join(SAVES, f'state_80038270_slot{SLOT:02d}.pst')
# cena: (P1, P2, toques para a direita na tela de palco, segundos de partida até salvar)
SCENES = {
    'heat_comet': ('HEAT', 'COMET', 0, 34.0),
    'kitty_hiro': ('KITTY-N', 'HIRO', 2, 40.0),
    'strike_shorty': ('STRIKE', 'SHORTY', 5, 45.0),
    'kelly_capoeira': ('KELLY', 'CAPOEIRA', 8, 38.0),
    'tsutomu_bio': ('TSUTOMU', 'BI-O', 4, 50.0),
    'shorty_kelly': ('SHORTY', 'KELLY', 1, 42.0),
    'comet_tsutomu': ('COMET', 'TSUTOMU', 6, 36.0),
}
# pacotes desligados no modo original. Os dois do remake ficam ligados porque reservam memória de mod, e o savestate só
# carrega com o mesmo layout dessa memória; o efeito deles sai pelos comandos de REMAKE_OFF.
FEATURES = (('bag2.enhancement.widescreen', 'widescreen'), ('psx.enhancement.hd-textures', 'hd-textures'),
            ('psx.enhancement.pgxp', 'pgxp'))
REMAKE_OFF = (('heat_mode', '0'), ('stage_geo', '0'), ('stage_post', '0'))
# [video] do modo original: o que o PS1 mostra numa TV (o resto do game.toml fica igual)
ORIGINAL_VIDEO = {
    'renderer': '"software"', 'internal_resolution': '"native"', 'texture_filtering': '"nearest"',
    'dithering': '"on"', 'antialiasing_mode': '"off"', 'aspect_ratio': '"4:3"', 'geometry_correction': 'false',
    'perspective_texturing': 'false', 'render_thread': 'false',
}


def write_original_toml():
    src = open(os.path.join(BUILD, 'game.toml'), encoding='utf-8').read()
    for k, v in ORIGINAL_VIDEO.items():
        src, n = re.subn(rf'(?m)^{k}\s*=.*$', f'{k} = {v}', src)
        if n == 0:
            src = src.replace('[video]\n', f'[video]\n{k} = {v}\n', 1)
    path = os.path.join(BUILD, 'game_original.toml')
    open(path, 'w', encoding='utf-8').write(src)
    return 'game_original.toml'


def wait_frames(s, n):
    f0 = s.cmd({'cmd': 'frame'}).get('frame', 0)
    while s.cmd({'cmd': 'frame'}).get('frame', 0) < f0 + n:
        time.sleep(0.005)


def load_and_shot(s, pst, cmd, png):
    shutil.copy2(pst, SLOT_FILE)
    r = s.cmd({'cmd': 'savestate', 'slot': SLOT, 'op': 'load'})
    if not r.get('ok'):
        raise RuntimeError(f'load: {r}')
    wait_frames(s, 60)
    r = s.cmd({'cmd': cmd, 'path': png.replace('\\', '/')}, 60)
    print(os.path.basename(png), r.get('ok'), r.get('width', r.get('w')), r.get('height', r.get('h')), flush=True)


def run_enhanced(names):
    s = Session(label='comparacao_melhorado')
    s.start()
    try:
        for i, name in enumerate(names):
            p1, p2, stage, wait = SCENES[name]
            (s.to_match_2p if i == 0 else s.menu_to_match_2p)(p1, p2, stage)
            print(name, s.who(), flush=True)
            time.sleep(wait)
            if not s.cmd({'cmd': 'savestate', 'slot': SLOT, 'op': 'save'}).get('ok'):
                raise RuntimeError('save')
            time.sleep(1.5)
            pst = os.path.join(OUT, f'{name}.pst')
            shutil.copy2(SLOT_FILE, pst)
            s.cmd({'cmd': 'savestate', 'slot': SLOT, 'op': 'load'})   # aquecimento: a 1ª carga demora mais
            time.sleep(1.0)
            load_and_shot(s, pst, 'screenshot_wide_hires', os.path.join(OUT, f'{name}_melhorado.png'))
            s.quit_match()
    finally:
        s.stop()


def run_original(names):
    state = os.path.join(BUILD, 'mods', 'state.toml')
    backup = state + '.comparacao'
    shutil.copy2(state, backup)
    with open(state, 'w', encoding='utf-8') as f:
        f.write('format_version = 2\n')
        for pkg, feat in FEATURES:
            f.write(f'\n[[feature]]\npackage_id = "{pkg}"\nid = "{feat}"\nenabled = false\n')
    try:
        s = Session(game=write_original_toml(), label='comparacao_original')
        s.start()
        try:
            s.at(75)   # abertura: o estado carrega por cima do jogo já rodando
            for name, args in REMAKE_OFF:
                print(name, s.cmd({'cmd': 'mod_cmd', 'name': name, 'args': args}).get('reply', '')[:60], flush=True)
            for name in names:
                pst = os.path.join(OUT, f'{name}.pst')
                load_and_shot(s, pst, 'screenshot_file', os.path.join(OUT, f'{name}_original.png'))
        finally:
            s.stop()
    finally:
        shutil.move(backup, state)


def main():
    names = (sys.argv[1].split(',') if len(sys.argv) > 1 else list(SCENES))
    os.makedirs(OUT, exist_ok=True)
    only = os.environ.get('COMPARE_ONLY')
    keep = open(SLOT_FILE, 'rb').read() if os.path.exists(SLOT_FILE) else None
    try:
        if only != 'original':
            run_enhanced(names)
        if only != 'melhorado':
            run_original(names)
    finally:   # devolve o slot como estava
        if keep is not None:
            open(SLOT_FILE, 'wb').write(keep)
        else:
            for ext in ('.pst', '.thumb'):
                if os.path.exists(SLOT_FILE[:-4] + ext):
                    os.remove(SLOT_FILE[:-4] + ext)


if __name__ == '__main__':
    main()
