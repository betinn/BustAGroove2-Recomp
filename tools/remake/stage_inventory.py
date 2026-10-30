#!/usr/bin/env python3
"""Inventário dos palcos (fase 6, peça 0; build-debug, TCP 4370, rota do 2 jogadores).

Duas etapas:
  run [n_palcos] [segundos]   uma sessão: para cada palco da tela de palco do 2 jogadores (0, 1, 2... toques para a
                              direita a partir do palco do HEAT), entra na partida HEAT x COMET, captura a tela de
                              palco e a partida, um quadro da GPU (gpu_frame_dump) e a RAM inteira, e sai pelo QUIT.
                              Com o dump do pacote HD ligado ao vivo (`hd_textures dump=1`) a mesma volta cobre a peça 1
                              (os PNG ficam em build-debug/mods/texture-packs/SLUS-01159/dumps). Sai pelo `quit` no fim
                              (sem matar o processo antes, senão o último lote de dumps se perde).
  analyze                     lê o que a etapa run gravou e escreve inventario.json / inventario.txt: qual overlay
                              (container 231..245) cada palco carrega, onde o modelo dele está na RAM, quantos nós com
                              mesh o jogo desenha por FUN_80029588 (pacotes no buffer do nó, +0xA8) e quantas primitivas
                              vêm de cada origem (adereços pelos nós, código do overlay, 2D/HUD), mais uma imagem da
                              atribuição por palco (vermelho = nós, verde = overlay, azul = resto).
Saída em tools/recomp/logs/remake/palcos/ (fora do git).
"""
from __future__ import annotations

import collections
import json
import os
import struct
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, r'C:/tools/psxrecomp/tools')
from game_session import Session, REPO  # noqa: E402

OUT = os.path.join(REPO, 'tools', 'recomp', 'logs', 'remake', 'palcos')
BIN = os.path.join(REPO, 'disc', 'Bust A Groove 2 (USA).bin')
STAGE_OVERLAY = 0x801DB1B8
STAGE_CONTAINERS = range(231, 246)
NODE = 0xB0


def run(n_stages=16, secs=60.0):
    import psx_gpu_frame as pgf
    os.makedirs(OUT, exist_ok=True)
    s = Session(label='palcos_inventario')
    s.start()
    try:
        s.at(60)
        print('hd dump', s.cmd({'cmd': 'hd_textures', 'dump': 1}).get('dump'), flush=True)
        for k in range(n_stages):
            shot = lambda name: s.shot(os.path.join(OUT, f'p{k:02d}_{name}.png')) if name == 'palco' else None
            if k == 0:
                s.to_match_2p('HEAT', 'COMET', k, on_step=shot)
            else:
                s.menu_to_match_2p('HEAT', 'COMET', k, on_step=shot)
            print(k, 'partida:', s.who(), flush=True)
            time.sleep(6)
            s.shot(os.path.join(OUT, f'p{k:02d}_a.png'))
            d = pgf.capture(pgf.DebugConn(), label=f'palco{k}')
            pgf.save_dump(d, os.path.join(OUT, f'p{k:02d}_gpu.json'))
            r = s.cmd({'cmd': 'read_ram', 'addr': '80000000', 'len': 0x200000}, 60)
            with open(os.path.join(OUT, f'p{k:02d}_ram.bin'), 'wb') as f:
                f.write(bytes.fromhex(r.get('hex') or r.get('data')))
            time.sleep(max(0.0, secs / 2 - 6))
            s.shot(os.path.join(OUT, f'p{k:02d}_b.png'))
            time.sleep(secs / 2)
            print(k, s.fps(), flush=True)
            s.quit_match()
        for _ in range(60):   # fila de dumps vazia antes de sair
            h = s.cmd({'cmd': 'hd_textures'})
            if not any(h.get(x) for x in ('queued_dumps', 'pending_dumps', 'dumps_pending', 'dump_queue')):
                break
            time.sleep(1)
        s.cmd({'cmd': 'hd_textures', 'dump': 0})
        time.sleep(2)
    finally:
        try:
            s.cmd({'cmd': 'quit'}, 3)
        except OSError:
            pass
        for _ in range(20):
            if s.p.poll() is not None:
                break
            time.sleep(0.5)
        s.stop()


def stage_models():
    """{container: (bytes do overlay para comparar, bytes do modelo, nós)} dos 15 overlays de palco do disco."""
    import bag2model as bm
    out = {}
    for c in STAGE_CONTAINERS:
        ch = bm.read_container(BIN, c)
        rest = [b for b in ch[2:] if b[:4] != b'\x10\x00\x00\x00']
        out[c] = (ch[0][12:12 + 512], rest[0], bm.parse_model(rest[0]),
                  sum(1 for b in ch if b[:4] == b'\x10\x00\x00\x00'))
    return out


def analyze():
    from PIL import Image, ImageDraw
    models = stage_models()
    inv = []
    k = 0
    while os.path.exists(os.path.join(OUT, f'p{k:02d}_ram.bin')):
        ram = open(os.path.join(OUT, f'p{k:02d}_ram.bin'), 'rb').read()
        u32 = lambda a: struct.unpack_from('<I', ram, a & 0x1FFFFF)[0]
        ov = ram[STAGE_OVERLAY & 0x1FFFFF:(STAGE_OVERLAY & 0x1FFFFF) + 512]
        cont = [c for c, m in models.items() if m[0] == ov]
        e = {'palco': k, 'containers': cont}
        if cont:
            _, model, nodes, ntim = models[cont[0]]
            i = ram.find(model[4:68])
            base = 0x80000000 + i - 4 if i >= 4 else None
            e.update(model_ram=f'{base:08X}' if base else None, nos=len(nodes),
                     nos_mesh=sum(1 for n in nodes if n.mesh), tims=ntim,
                     verts=sum(len(n.mesh.verts) for n in nodes if n.mesh),
                     prims=sum(len(n.mesh.prims) for n in nodes if n.mesh))
            bufs = []
            if base:
                end = base + len(model)
                for a in range(0, len(ram) - NODE, 4):
                    if u32(a + 0xA0) == 1 and base <= u32(a + 0x8C) < end:
                        bufs.append((u32(a + 0xA8) & 0x1FFFFF, u32(a + 0xA4)))
            e['nos_ram'] = len(bufs)
            gpu = json.load(open(os.path.join(OUT, f'p{k:02d}_gpu.json')))
            cnt = collections.Counter()
            ranks = collections.defaultdict(list)
            im = Image.new('RGB', (640, 240), (0, 0, 0))   # área de desenho 512 x 240 (tela larga) + margem
            dr = ImageDraw.Draw(im, 'RGBA')
            for p in gpu['prims']:
                if p.get('kind') != 'poly' or len(p['verts']) < 3:
                    continue
                src = int(p['src'], 16) & 0x1FFFFF
                if any(b <= src < b + n for b, n in bufs):
                    who, col = 'nos', (255, 80, 80, 90)
                elif 0x0C4000 <= src < 0x0E0000:
                    who, col = 'overlay', (80, 255, 80, 90)
                else:
                    who, col = 'outros', (80, 80, 255, 90)
                cnt[who] += 1
                ranks[who].append(p['ot'])
                v = [(x + 64, y) for x, y in p['verts']]
                if len(v) == 4:
                    v = [v[0], v[1], v[3], v[2]]
                dr.polygon(v, fill=col, outline=col[:3] + (200,))
            im.save(os.path.join(OUT, f'p{k:02d}_atrib.png'))
            e['prims_quadro'] = dict(cnt)
            e['ranks'] = {w: [min(r), max(r)] for w, r in ranks.items()}
        inv.append(e)
        k += 1
    with open(os.path.join(OUT, 'inventario.json'), 'w', encoding='utf-8') as f:
        json.dump(inv, f, indent=1)
    with open(os.path.join(OUT, 'inventario.txt'), 'w', encoding='utf-8') as f:
        for e in inv:
            line = (f"palco {e['palco']:2d}: container {e['containers']} modelo {e.get('model_ram')} nós {e.get('nos')} "
                    f"(mesh {e.get('nos_mesh')}, na RAM {e.get('nos_ram')}) vértices {e.get('verts')} prims {e.get('prims')} "
                    f"TIMs {e.get('tims')} | no quadro {e.get('prims_quadro')} ranks {e.get('ranks')}")
            print(line)
            f.write(line + '\n')


if __name__ == '__main__':
    if len(sys.argv) > 1 and sys.argv[1] == 'run':
        run(int(sys.argv[2]) if len(sys.argv) > 2 else 16, float(sys.argv[3]) if len(sys.argv) > 3 else 60.0)
    else:
        analyze()
