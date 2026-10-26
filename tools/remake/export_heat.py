#!/usr/bin/env python3
"""Exporta o HEAT para o plugin do jogo (src/bag2_heat.c): malha soldada, atributos por canto, oclusão
ambiente por vértice e as texturas HD (Real-ESRGAN 4x) convertidas para 15 bits.

Uso: python tools/remake/export_heat.py [saída]   (padrão: build-debug/mods/remake/heat.bin e build/...)
O arquivo é derivado do disco do usuário: fica fora do git (pastas build*/ são ignoradas).

Formato (little-endian), versão 2:
    char[4] "HTR2"; u32 n_nodes, n_verts, n_tris, n_layers, hd_scale
    por nó:     u16 nverts (0 = sem mesh), u16 pad, u32 base_index (MeshHeader+0x98)
    por vértice: s16 x, y, z (espaço do nó dono), u8 nó dono, u8 oclusão (255 = aberto)
    por face (na ordem em que o jogo liga as primitivas na OT: nó, lista na ordem de FUN_80029588, prim):
                u16 v[3] (ordem original do arquivo: é a que o NCLIP do jogo usa), u16 z[2] (vértices
                cujo Z dá o slot da OT, (z0 + z1) >> 3, como o jogo), u8 modo (0 cor,
                1 cor + overlay, 2 só textura), s8 camada, u8 uv[3][2] (texels 0..255), u8 base[3][3],
                u8 over[3][3] (128 = neutro)
    por camada: u16 largura, u16 altura, u16 texels[altura][largura] (PS1 15 bits; 0 = transparente)
"""
from __future__ import annotations

import os
import struct
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(HERE, '..'))

from bag2model import CHARS, load_character  # noqa: E402
from rig import build_rig, pose_positions  # noqa: E402
from heat_remake import DISC, OUT, SKIP, hd_layers, ps1_layers  # noqa: E402

BAG2 = os.path.join(HERE, '..', 'recomp', 'bag2')
DEFAULT_OUT = [os.path.join(BAG2, d, 'mods', 'remake', 'heat.bin') for d in ('build-debug', 'build')]


def to15(rgba: np.ndarray) -> np.ndarray:
    r, g, b, a = (rgba[..., i].astype(np.uint32) for i in range(4))
    v = (r >> 3) | ((g >> 3) << 5) | ((b >> 3) << 10)
    v = np.where(v == 0, 0x8000, v)            # preto opaco: bit STP, senão 0x0000 vira transparente
    return np.where(a >= 128, v, 0).astype('<u2')


def main():
    outs = sys.argv[1:] or DEFAULT_OUT
    name = 'HEAT'
    nodes, anims, tims, _ = load_character(DISC, CHARS.index(name))
    # sem deduplicar: o modelo tem camadas coplanares (decalques) que só ficam certas na ordem do jogo
    rig = build_rig(nodes, SKIP[name], two_sided=True, dedup=False)
    perm = np.lexsort(rig.order.T[::-1])
    for k in ('tris', 'fnode', 'nidx', 'base', 'over', 'uv', 'layer', 'mode', 'order', 'zpair'):
        setattr(rig, k, getattr(rig, k)[perm])
    layers = ps1_layers(rig, tims)
    hd = hd_layers(layers, os.path.join(OUT, name.lower(), 'textures'))
    # oclusão ambiente na pose de descanso, por vértice (na GPU, como nos renders offline)
    from render import Renderer
    rd = Renderer(layers, hd)
    p, _ = pose_positions(rig, nodes)
    ao = np.ones(len(p))
    used = np.unique(rig.tris)
    ao_used = rd.ambient_occlusion(p, rig.tris)
    ao[used] = ao_used[used]
    buf = bytearray(b'HTR2')
    buf += struct.pack('<5I', len(nodes), len(rig.owner), len(rig.tris), len(hd), 4)
    for nd in nodes:
        buf += struct.pack('<HHI', len(nd.mesh.verts) if nd.mesh else 0, 0, nd.mesh.base_index if nd.mesh else 0)
    loc = np.round(rig.local).astype(np.int64)
    assert np.abs(loc).max() < 32768
    for (x, y, z), o, a in zip(loc, rig.owner, ao):
        buf += struct.pack('<hhhBB', x, y, z, int(o), int(np.clip(a * 255, 0, 255)))
    for f in range(len(rig.tris)):
        buf += struct.pack('<5HBb', *rig.tris[f], *rig.zpair[f], int(rig.mode[f]), int(rig.layer[f]))
        buf += bytes(int(v) for v in np.clip(rig.uv[f], 0, 255).ravel())
        buf += bytes(int(v) for v in np.clip(rig.base[f], 0, 255).ravel())
        buf += bytes(int(v) for v in np.clip(rig.over[f], 0, 255).ravel())
    for img in hd:
        h, w = img.shape[:2]
        buf += struct.pack('<HH', w, h) + to15(img).tobytes()
    for o in outs:
        os.makedirs(os.path.dirname(o), exist_ok=True)
        with open(o, 'wb') as fh:
            fh.write(buf)
        print(f'{o}: {len(buf) / 1e6:.1f} MB, {len(rig.owner)} vértices, {len(rig.tris)} faces, {len(hd)} camadas')


if __name__ == '__main__':
    main()
