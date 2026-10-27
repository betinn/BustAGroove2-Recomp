#!/usr/bin/env python3
"""Exporta o HEAT para o plugin do jogo (src/bag2_heat.c + núcleo src/remake/): malha soldada, atributos
por canto, oclusão ambiente por vértice, as texturas HD (Real-ESRGAN 4x) em RGBA8, as mãos com dedos de
cada gesto (tools/remake/hands.py) como grupos de faces e as orelhas novas (tools/remake/ears.py).

Uso: python tools/remake/export_heat.py [saída]   (padrão: build-debug/mods/remake/heat.bin e build/...)
O arquivo é derivado do disco do usuário: fica fora do git (pastas build*/ são ignoradas).

Formato (little-endian), versão 4 (lido por src/remake/rmk_asset.c):
    char[4] "HTR4"; u32 versão (4), n_nodes, n_verts, n_tris, n_layers, tex_size, n_body, n_groups
    por nó:     u16 nverts (0 = sem mesh), u16 pad, u32 base_index (MeshHeader+0x98)
    por grupo (1..n_groups, gestos das mãos): u8 posição na árvore (16/19), u8 pad, u16 nverts e u32
                base_index do mesh do jogo desse gesto (nós 16/20/21, 19/22/23), u32 primeiro vértice,
                u32 nº de vértices exclusivos do grupo
    por vértice: s16 x, y, z (espaço do nó dono), u8 nó dono, u8 oclusão (255 = aberto); primeiro os n_body
                do corpo (usados por faces do corpo ou por mais de um gesto), depois os de cada grupo
    por face (na ordem em que o jogo liga as primitivas na OT: nó, lista na ordem de FUN_80029588, prim):
                u16 v[3] (orientação original do arquivo, a que o NCLIP do jogo usa, girada para começar no
                menor índice), u16 z[2] (vértices cujo Z dá o slot da OT, (z0 + z1) >> 3, como o jogo),
                u8 modo (0 cor, 1 cor + overlay, 2 só textura; + 4 = sem mapa de normal), s8 camada, u8 uv[3][2] (texels 0..255),
                u8 base[3][3], u8 over[3][3] (128 = neutro), u8 grupo (0 corpo)
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
from heat_remake import DISC, OUT, hd_layers, ps1_layers  # noqa: E402
import detail  # noqa: E402
import ears as E  # noqa: E402
import hands as H  # noqa: E402

BAG2 = os.path.join(HERE, '..', 'recomp', 'bag2')
DEFAULT_OUT = [os.path.join(BAG2, d, 'mods', 'remake', 'heat.bin') for d in ('build-debug', 'build')]
FACE_FIELDS = ('tris', 'fnode', 'nidx', 'base', 'over', 'uv', 'layer', 'mode', 'order', 'zpair', 'hand')


def canonical_corners(rig):
    """Gira os cantos de cada face (mantendo a orientação) para começar no menor índice de vértice."""
    k = np.argmin(rig.tris, axis=1)
    idx = (k[:, None] + np.arange(3)[None, :]) % 3
    rows = np.arange(len(k))[:, None]
    for name in ('tris', 'uv', 'base', 'over'):
        a = getattr(rig, name)
        setattr(rig, name, a[rows, idx])


def vertex_order(rig, n_groups):
    """Corpo primeiro (vértices de faces do corpo ou de mais de um gesto), depois os exclusivos de cada
    grupo, contíguos. Vértices sem face (as luvas originais trocadas) saem. Devolve (perm, n_body, faixas)."""
    nv = len(rig.owner)
    users = [set() for _ in range(nv)]
    for t, g in zip(rig.tris, rig.hand):
        for v in t:
            users[v].add(int(g))
    body = [v for v in range(nv) if users[v] and (0 in users[v] or len(users[v]) > 1)]
    perm, ranges = list(body), {}
    for g in range(1, n_groups + 1):
        vs = [v for v in range(nv) if users[v] == {g}]
        ranges[g] = (len(perm), len(vs))
        perm += vs
    return np.array(perm), len(body), ranges


def main():
    outs = sys.argv[1:] or DEFAULT_OUT
    name = 'HEAT'
    nodes, anims, tims, _ = load_character(DISC, CHARS.index(name))
    # sem deduplicar: o modelo tem camadas coplanares (decalques) que só ficam certas na ordem do jogo
    base_rig = build_rig(nodes, set(), two_sided=True, dedup=False)
    layers = ps1_layers(base_rig, tims)
    hd = hd_layers(layers, os.path.join(OUT, name.lower(), 'textures'))
    rig, groups = H.attach_hands(base_rig, nodes, hd, cache_dir=os.path.join(OUT, name.lower(), 'maos'))
    rig = E.attach_nose(E.attach_ears(rig))
    detail.mark_flat(rig)
    perm = np.lexsort(rig.order.T[::-1])
    for k in FACE_FIELDS:
        setattr(rig, k, getattr(rig, k)[perm])
    n_groups = max(groups)
    vperm, n_body, ranges = vertex_order(rig, n_groups)
    remap = -np.ones(len(rig.owner), int)
    remap[vperm] = np.arange(len(vperm))
    rig.owner, rig.local = rig.owner[vperm], rig.local[vperm]
    rig.tris, rig.zpair = remap[rig.tris], remap[rig.zpair]
    assert (rig.tris >= 0).all() and (rig.zpair >= 0).all()
    # oclusão ambiente na pose de descanso, por vértice (na GPU, como nos renders offline): o corpo com as
    # mãos abertas; cada gesto com o corpo e as duas mãos desse gesto
    from render import Renderer
    rd = Renderer(layers, hd)
    p, _ = pose_positions(rig, nodes)
    ao = np.ones(len(p))
    for k in range(len(H.GESTURES)):
        gl, gr = 1 + k, 1 + len(H.LEFT_NODES) + k
        m = np.isin(rig.hand, (0, gl, gr))
        a = rd.ambient_occlusion(p, rig.tris[m])
        vs = np.unique(rig.tris[m])
        if k == 0:
            ao[vs] = a[vs]
        for g in (gl, gr):
            v0, n = ranges[g]
            ao[v0:v0 + n] = a[v0:v0 + n]
    canonical_corners(rig)
    size = hd[0].shape[0]
    assert all(img.shape == (size, size, 4) for img in hd)
    buf = bytearray(b'HTR4')
    buf += struct.pack('<8I', 4, len(nodes), len(rig.owner), len(rig.tris), len(hd), size, n_body, n_groups)
    for nd in nodes:
        buf += struct.pack('<HHI', len(nd.mesh.verts) if nd.mesh else 0, 0, nd.mesh.base_index if nd.mesh else 0)
    for g in range(1, n_groups + 1):
        pos, node, nv, base = groups[g]
        v0, n = ranges[g]
        buf += struct.pack('<BBHIII', pos, 0, nv, base, v0, n)
    loc = np.round(rig.local).astype(np.int64)
    assert np.abs(loc).max() < 32768
    for (x, y, z), o, a in zip(loc, rig.owner, ao):
        buf += struct.pack('<hhhBB', x, y, z, int(o), int(np.clip(a * 255, 0, 255)))
    for f in range(len(rig.tris)):
        buf += struct.pack('<5HBb', *rig.tris[f], *rig.zpair[f], int(rig.mode[f]), int(rig.layer[f]))
        buf += bytes(int(v) for v in np.clip(np.round(rig.uv[f]), 0, 255).ravel())
        buf += bytes(int(v) for v in np.clip(rig.base[f], 0, 255).ravel())
        buf += bytes(int(v) for v in np.clip(rig.over[f], 0, 255).ravel())
        buf += bytes([int(rig.hand[f])])
    for img in hd:   # alfa do xBR: 0 ou 255; o filtro linear da GPU suaviza a borda
        buf += np.ascontiguousarray(img, np.uint8).tobytes()
    for o in outs:
        os.makedirs(os.path.dirname(o), exist_ok=True)
        with open(o, 'wb') as fh:
            fh.write(buf)
        print(f'{o}: {len(buf) / 1e6:.1f} MB, {len(rig.owner)} vértices ({n_body} do corpo), {len(rig.tris)} faces, '
              f'{len(hd)} camadas, grupos ' + ', '.join(f'{g}: nó {groups[g][1]} {ranges[g][1]} v '
                                                       f'{int((rig.hand == g).sum())} f' for g in groups))


if __name__ == '__main__':
    main()
