#!/usr/bin/env python3
"""Exporta o HEAT para o plugin do jogo (src/bag2_heat.c + núcleo src/remake/): malha soldada, atributos
por canto, oclusão ambiente por vértice e as texturas HD (Real-ESRGAN 4x) em RGBA8.

Uso: python tools/remake/export_heat.py [saída]   (padrão: build-debug/mods/remake/heat.bin e build/...)
O arquivo é derivado do disco do usuário: fica fora do git (pastas build*/ são ignoradas).

Formato (little-endian), versão 3 (lido por src/remake/rmk_asset.c):
    char[4] "HTR3"; u32 versão (3), n_nodes, n_verts, n_tris, n_layers, tex_size
    por nó:     u16 nverts (0 = sem mesh), u16 pad, u32 base_index (MeshHeader+0x98)
    por vértice: s16 x, y, z (espaço do nó dono), u8 nó dono, u8 oclusão (255 = aberto)
    por face (na ordem em que o jogo liga as primitivas na OT: nó, lista na ordem de FUN_80029588, prim):
                u16 v[3] (orientação original do arquivo, a que o NCLIP do jogo usa, girada para começar no
                menor índice), u16 z[2] (vértices cujo Z dá o slot da OT, (z0 + z1) >> 3, como o jogo),
                u8 modo (0 cor, 1 cor + overlay, 2 só textura), s8 camada, u8 uv[3][2] (texels 0..255),
                u8 base[3][3], u8 over[3][3] (128 = neutro)
    por camada: tex_size x tex_size RGBA8 (alfa 0 = transparente)
A rotação canônica dos cantos faz faces com os mesmos vértices (decalques coplanares) terem a mesma
ordem de cantos: no shader, a mesma conta dá a mesma profundidade, e a ordem de desenho decide (LEQUAL).
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


def canonical_corners(rig):
    """Gira os cantos de cada face (mantendo a orientação) para começar no menor índice de vértice."""
    k = np.argmin(rig.tris, axis=1)
    idx = (k[:, None] + np.arange(3)[None, :]) % 3
    rows = np.arange(len(k))[:, None]
    for name in ('tris', 'uv', 'base', 'over'):
        a = getattr(rig, name)
        setattr(rig, name, a[rows, idx])


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
    canonical_corners(rig)
    size = hd[0].shape[0]
    assert all(img.shape == (size, size, 4) for img in hd)
    buf = bytearray(b'HTR3')
    buf += struct.pack('<6I', 3, len(nodes), len(rig.owner), len(rig.tris), len(hd), size)
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
    for img in hd:   # alfa do xBR: 0 ou 255; o filtro linear da GPU suaviza a borda
        buf += np.ascontiguousarray(img, np.uint8).tobytes()
    for o in outs:
        os.makedirs(os.path.dirname(o), exist_ok=True)
        with open(o, 'wb') as fh:
            fh.write(buf)
        print(f'{o}: {len(buf) / 1e6:.1f} MB, {len(rig.owner)} vértices, {len(rig.tris)} faces, {len(hd)} camadas')


if __name__ == '__main__':
    main()
