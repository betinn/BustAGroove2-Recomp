#!/usr/bin/env python3
"""Exporta um palco para o plugin do jogo (src/bag2_stage.c + núcleo src/remake/), fase 6 do remake.

O palco é um modelo no mesmo formato dos personagens (notes/model-format.md), no overlay do palco (containers 231..245
do BUST2.BIN: DBI_ do código, SEDT, TIMs, o modelo e as trilhas de câmera): uma lista de objetos rígidos (nós de tipo 1,
quase todos raízes, posição no mundo). O jogo desenha cada nó por FUN_80029588 (um pacote por primitiva no buffer do nó,
+0xA8, na ordem das listas de GAME_LIST_ORDER; as listas com decalque geram dois pacotes, base e textura); fundo, chão e
peças especiais de alguns palcos saem do código do overlay (não entram aqui). O plugin troca, na OT do quadro, cada
sequência de pacotes dos nós pelo desenho de host do remake, na mesma ordem de pintor (notes/remake-visual.md, "Fase 6").

Saída (fora do git, derivada do disco do usuário), em build-debug/ e build/ mods/remake/:
  palco<c>.bin  asset HTR7 (src/remake/rmk_asset.c): malha por nó (vértices divididos nos vincos, para a normal de uma
                caixa não virar a de uma almofada), oclusão ambiente por vértice, camadas HD (Real-ESRGAN 4x) com o STP
                no alfa; um "osso" por nó com mesh (dono do vértice = posição do nó na lista de nós com mesh).
  palco<c>.stg  tabelas do palco (src/remake/rmk_stage.c), little-endian:
      char[4] "STG1"; u32 versão (1), container, n_nodes (com mesh), n_layers, n_keys, model_bytes;
      u32 FNV-1a dos 512 primeiros bytes do overlay (RAM 0x801DB1B8), u32 FNV-1a dos 256 primeiros bytes do modelo
      por nó:      u32 deslocamento do MeshHeader no modelo, u32 bytes do buffer de pacotes, u32 nº de primitivas,
                   u32 primeira primitiva (índice global)
      por primitiva (na ordem do buffer): u16 palavras do(s) pacote(s) (com a tag), u8 nº de pacotes (1 ou 2: decalque),
                   u8 nº de faces (0..2), u32 primeira face; u8 canto[2][3]: pacote (vértice 0..3 no GP0) de cada canto
                   das duas faces (lido do pacote vivo: cor, UV), u8 pad[2]
      por chave:   u16 tpage (bits 0..4 e 7..8 do pacote), u16 clut (palavra do pacote), u8 camada, u8 pad[3]
                   (a camada HD de cada textura e CLUT que o jogo usa, pela posição de cada TIM na VRAM)
Uso: python tools/remake/export_stage.py 234 [235 ...]   (sem argumento: todos, 231..245)
"""
from __future__ import annotations

import os
import struct
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(HERE, '..'))

import bag2model as bm  # noqa: E402
from rig import GAME_LIST_ORDER, build_rig, pose_positions, smooth_normals  # noqa: E402
from heat_remake import DISC, OUT, hd_layers, ps1_layers  # noqa: E402
from export_char import SEMI, canonical_corners, stp_layers  # noqa: E402

BAG2 = os.path.join(HERE, '..', 'recomp', 'bag2')
STAGES = range(231, 246)
CREASE_DEG = 40.0                  # vinco: faces vizinhas com mais que isso entre as normais não dividem o vértice
TEXTURED = {2, 3, 4, 5, 10, 11, 26, 27}
OVER = {8, 9, 12, 13, 15, 28, 29}  # decalque: pacote base + pacote com textura
# vértice de cada slot do GP0 num quad (A, B, C, D = v2, v3, v1, v0: notes/model-format.md, QUAD_ATTR); slot do vértice
QUAD_SLOT = {0: 3, 1: 2, 2: 0, 3: 1}


def fnv(b: bytes) -> int:
    h = 0x811C9DC5
    for x in b:
        h = ((h ^ x) * 0x01000193) & 0xFFFFFFFF
    return h


def packet_words(li: int) -> tuple[int, int]:
    """(palavras com as tags, nº de pacotes) da primitiva da lista `li` no buffer do nó: F/G conforme GOURAUD, T se tem
    textura; decalque = base sem textura + o mesmo com textura (conferido nas RAMs do inventário, fase 6)."""
    n = 4 if bm.L[li][1] else 3
    g = li in bm.GOURAUD

    def size(tex):
        return 1 + (n if g else 1) + n + (n if tex else 0)
    if li in OVER:
        return size(False) + size(True), 2
    return size(li in TEXTURED), 1


def mesh_offsets(model: bytes, nodes) -> dict[int, int]:
    """Deslocamento do MeshHeader de cada nó com mesh no arquivo do modelo (o ponteiro +0x8C do nó na RAM aponta para
    o modelo carregado + isto)."""
    offs = {}
    n = struct.unpack_from('<I', model, 0)[0]
    p = 4 + n * 16
    for nd in nodes:
        t = struct.unpack_from('<I', model, p)[0]
        if t == 0:
            p += 4
        elif t == 2:
            p += 8
        elif t == 3:
            p += 12 + struct.unpack_from('<I', model, p + 8)[0] * 8
        elif t == 1:
            offs[nd.index] = p
            _, p = bm._parse_mesh(model, p)
        else:
            raise ValueError(f'nó {nd.index}: tipo {t} num palco')
        p += 40
    return offs


def load_stage(c: int):
    ch = bm.read_container(DISC, c)
    tim_raw = [b for b in ch if b[:4] == b'\x10\x00\x00\x00']
    rest = [b for b in ch[2:] if b[:4] != b'\x10\x00\x00\x00']
    return ch[0][12:], rest[0], bm.parse_model(rest[0]), [bm.parse_tim(b) for b in tim_raw], tim_raw


def tim_vram(raw: bytes):
    """(x, y) da imagem e (x, y) da CLUT na VRAM, do cabeçalho do TIM."""
    flags = struct.unpack_from('<I', raw, 4)[0]
    off, cx, cy = 8, -1, -1
    if flags & 8:
        cl, cx, cy = struct.unpack_from('<IHH', raw, off)
        off += cl
    _, x, y = struct.unpack_from('<IHH', raw, off)
    return x, y, cx, cy, [4, 8, 16, 24][flags & 3]


def split_creases(rig, p_rest):
    """Um vértice por (vértice, grupo de suavização): a normal que o núcleo calcula por vértice (soma das faces) fica
    lisa dentro do grupo e viva nos vincos. Devolve o mapa canto -> vértice novo e os donos/posições novos."""
    cn = smooth_normals(p_rest, rig.tris, CREASE_DEG)          # (F, 3, 3) normal por canto
    q = np.round(cn * 64).astype(np.int64)
    keys = {}
    tris = np.zeros_like(rig.tris)
    src = []
    for f in range(len(rig.tris)):
        for k in range(3):
            key = (int(rig.tris[f, k]), *q[f, k])
            if key not in keys:
                keys[key] = len(src)
                src.append(int(rig.tris[f, k]))
            tris[f, k] = keys[key]
    src = np.array(src)
    return tris, src


def export(c: int, outs_dir: list[str]):
    ov, model, nodes, tims, tim_raw = load_stage(c)
    mesh_nodes = [nd.index for nd in nodes if nd.mesh]
    assert len(mesh_nodes) <= 64, f'{c}: {len(mesh_nodes)} nós com mesh'
    pos_of = {n: i for i, n in enumerate(mesh_nodes)}
    offs = mesh_offsets(model, nodes)
    rig = build_rig(nodes, set(), two_sided=True, dedup=False)
    layers = ps1_layers(rig, tims)
    hd = hd_layers(stp_layers(rig, tims, layers), os.path.join(OUT, 'palcos', f'palco{c}', 'textures'))
    rig.mode = rig.mode | stage_semi(rig, nodes)
    # faces na ordem do buffer de pacotes: (nó, lista na ordem do jogo, primitiva)
    perm = np.lexsort(rig.order.T[::-1])
    for f in ('tris', 'fnode', 'nidx', 'base', 'over', 'uv', 'layer', 'mode', 'order', 'zpair', 'pcorner'):
        setattr(rig, f, getattr(rig, f)[perm])
    slots = face_slots(rig, nodes)
    p_rest, _ = pose_positions(rig, nodes)
    # oclusão ambiente na pose de descanso, nos vértices originais (os divididos ficam com a do vértice de origem)
    from render import Renderer
    rd = Renderer(layers, hd)
    ao_src = rd.ambient_occlusion(p_rest, rig.tris, res=1024)
    tris, src = split_creases(rig, p_rest)
    owner = rig.owner[src]
    local = rig.local[src]
    ao = ao_src[src]
    first_copy = {}
    for i, o in enumerate(src):
        first_copy.setdefault(int(o), i)
    zpair = np.vectorize(lambda v: first_copy.get(int(v), 0))(rig.zpair)
    rig.tris = tris
    rot_corners(rig, slots)
    size = hd[0].shape[0]
    # primitivas por nó, na ordem do buffer
    prims, first_prim, node_rows = [], 0, []
    face_of = {}
    for f, (n, lo, pi) in enumerate(rig.order):
        face_of.setdefault((int(n), int(lo), int(pi)), []).append(f)
    for n in mesh_nodes:
        m = nodes[n].mesh
        cnt = {}
        for p in m.prims:
            cnt[p.lst] = cnt.get(p.lst, 0) + 1
        words = 0
        rows = []
        for lo, li in enumerate(GAME_LIST_ORDER):
            for pi in range(cnt.get(li, 0)):
                w, npk = packet_words(li)
                fs = face_of.get((n, lo, pi), [])
                assert len(fs) <= 2 and (not fs or fs == list(range(fs[0], fs[0] + len(fs))))
                rows.append((w, npk, len(fs), fs[0] if fs else 0, [slots[f] for f in fs]))
                words += w
        node_rows.append((offs[n], 4 * words, len(rows), first_prim))
        first_prim += len(rows)
        prims += rows
    # camada de cada (tpage, clut) que o jogo usa: posição de cada TIM na VRAM
    keys = []
    for li_, (tex, clut) in enumerate(rig.layers):
        x, y, cx, cy, bpp = tim_vram(tim_raw[tex])
        tp = (x // 64) | ((y // 256) << 4) | ({4: 0, 8: 1, 16: 2}[bpp] << 7)
        cw = (cx // 16) | ((cy + clut) << 6) if cx >= 0 else 0
        keys.append((tp, cw, li_))
    # ---- asset HTR7
    buf = bytearray(b'HTR7')
    buf += struct.pack('<10I', 7, len(mesh_nodes), len(owner), len(rig.tris), len(hd), size, len(owner), 0, 0, 100 + c)
    buf += f'PALCO{c}'.encode('ascii').ljust(16, b'\0')
    for n in mesh_nodes:
        m = nodes[n].mesh
        buf += struct.pack('<HBBI', len(m.verts), 0, 1, m.base_index)
    buf += bytes([255]) * len(hd)   # página de cada camada: nenhuma (n_pages = 0; sem expressão do rosto)
    loc = np.round(local).astype(np.int64)
    assert np.abs(loc).max() < 32768
    for (x, y, z), o, a in zip(loc, owner, ao):
        buf += struct.pack('<hhhBB', x, y, z, pos_of[int(o)], int(np.clip(a * 255, 0, 255)))
    for f in range(len(rig.tris)):
        buf += struct.pack('<5HBb', *rig.tris[f], *zpair[f], int(rig.mode[f]), int(rig.layer[f]))
        buf += bytes(int(v) for v in np.clip(np.round(rig.uv[f]), 0, 255).ravel())
        buf += bytes(int(v) for v in np.clip(rig.base[f], 0, 255).ravel())
        buf += bytes(int(v) for v in np.clip(rig.over[f], 0, 255).ravel())
        buf += bytes([0])
    for img in hd:
        buf += np.ascontiguousarray(img, np.uint8).tobytes()
    # ---- tabelas
    stg = bytearray(b'STG1')
    stg += struct.pack('<6I', 1, c, len(mesh_nodes), len(hd), len(keys), len(model))
    stg += struct.pack('<2I', fnv(ov[:512]), fnv(model[:256]))
    for r in node_rows:
        stg += struct.pack('<4I', *r)
    for w, npk, nf, f0, sl in prims:
        cs = [list(s) for s in sl] + [[0, 0, 0]] * (2 - len(sl))
        stg += struct.pack('<HBBI', w, npk, nf, f0) + bytes(cs[0] + cs[1]) + b'\0\0'
    for tp, cw, li_ in keys:
        stg += struct.pack('<HHB3x', tp, cw, li_)
    for d in outs_dir:
        os.makedirs(d, exist_ok=True)
        with open(os.path.join(d, f'palco{c}.bin'), 'wb') as fh:
            fh.write(buf)
        with open(os.path.join(d, f'palco{c}.stg'), 'wb') as fh:
            fh.write(stg)
    print(f'palco {c}: {len(buf) / 1e6:.1f} MB, {len(mesh_nodes)} nós, {len(owner)} vértices ({len(rig.owner)} antes dos '
          f'vincos), {len(rig.tris)} faces ({int(((rig.mode & SEMI) != 0).sum())} semi), {len(hd)} camadas, '
          f'{len(prims)} primitivas, chaves {[(hex(a), hex(b)) for a, b, _ in keys]}', flush=True)


def stage_semi(rig, nodes):
    semi = np.zeros(len(rig.tris), int)
    cache = {}
    for f, (n, lo, pi) in enumerate(rig.order):
        key = (int(n), int(lo))
        if key not in cache:
            cache[key] = [p for p in nodes[n].mesh.prims if p.lst == GAME_LIST_ORDER[lo]]
        lst = cache[key]
        if pi < len(lst) and (lst[pi].overlay or lst[pi]).semi_bits:
            semi[f] = SEMI
    return semi


def face_slots(rig, nodes):
    """Slot do GP0 (vértice 0..3 do pacote) de cada canto de cada face, na ordem dos cantos de rig.tris: o vértice da
    primitiva no triângulo; num quad o jogo põe v2, v3, v1, v0 nos slots A..D (QUAD_SLOT)."""
    quad = np.array([bm.L[GAME_LIST_ORDER[int(lo)]][1] for _, lo, _ in rig.order], bool)
    qs = np.array([QUAD_SLOT[k] for k in range(4)])
    return np.where(quad[:, None], qs[rig.pcorner], rig.pcorner)


def rot_corners(rig, slots):
    """canonical_corners (o canto de menor índice primeiro) levando junto o slot de cada canto."""
    k = np.argmin(rig.tris, axis=1)
    idx = (k[:, None] + np.arange(3)[None, :]) % 3
    rows = np.arange(len(k))[:, None]
    slots[:] = slots[rows, idx]
    canonical_corners(rig)


def main():
    cs = [int(a) for a in sys.argv[1:]] or list(STAGES)
    outs = [os.path.join(BAG2, d, 'mods', 'remake') for d in ('build-debug', 'build')]
    for c in cs:
        export(c, outs)


if __name__ == '__main__':
    main()
