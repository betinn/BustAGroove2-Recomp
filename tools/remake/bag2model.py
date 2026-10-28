#!/usr/bin/env python3
"""Leitura do modelo, animações e texturas de um personagem do Bust A Groove 2 direto do disco.

Porta em Python de web/src/bag2/model.ts e anim.ts (formato em notes/model-format.md).
Uso como módulo: `load_character(bin_path, k)` devolve (modelo, animações, TIMs de corpo).
k = índice do container na ordem alfabética (HEAT = 3): modelo em 123 + 2k, texturas em 87 + 2k.
"""
from __future__ import annotations

import os
import struct
import sys
from dataclasses import dataclass, field

import numpy as np

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
from bpe import bpe_decode  # noqa: E402
from bust2_extract import Disc  # noqa: E402

CHARS = ['BI-O', 'CAPOEIRA', 'COMET', 'HEAT', 'HIRO', 'KELLY', 'KITTY-N', 'PANDER', 'ROBO-Z GOLD', 'SHORTY',
         'STRIKE', 'TSUTOMU', 'CHICHI&SALLY', 'COLUMBO', 'HUSTLE KONG', 'McLOAD', 'MICHAEL DOI', 'SUSHI BOY']


# ---------------------------------------------------------------- disco / BUST2.BIN
def _table(b: bytes):
    n = struct.unpack_from('<I', b, 4)[0]
    return [struct.unpack_from('<II', b, 8 + i * 8) for i in range(n)]


def _unbpe(b: bytes) -> bytes:
    return bpe_decode(b) if b[:4] == b'BPE_' else b


def read_container(bin_path: str, index: int) -> list[bytes]:
    """Filhos (já descomprimidos) da entrada `index` de BUST2.BIN."""
    disc = Disc(bin_path)
    ent = next(e for e in disc.root() if e[0].upper() == 'BUST2.BIN')
    lba = ent[1]
    hdr = disc.read(lba, 8)
    n = struct.unpack_from('<I', hdr, 4)[0]
    off, size = _table(disc.read(lba, 8 + n * 8))[index]
    start = lba + off // 2048
    blob = disc.read(start, off % 2048 + size)[off % 2048:]
    blob = _unbpe(blob)
    if blob[:4] not in (b'DFS_', b'DF2_'):
        return [blob]
    return [_unbpe(blob[o:o + s]) for o, s in _table(blob)]


# ---------------------------------------------------------------- TIM
@dataclass
class Tim:
    bpp: int
    w: int            # largura em texels
    h: int
    idx: np.ndarray   # (h, w) índices (4/8 bpp) ou cores 16 bits
    clut: np.ndarray  # (linhas, entradas) u16

    def rgba(self, row: int = 0) -> np.ndarray:
        """(h, w, 4) uint8 com a linha `row` da CLUT. Cor 0x0000 = transparente (como no PS1)."""
        pal = self.clut[min(row, len(self.clut) - 1)] if self.clut is not None else None
        v = pal[self.idx] if pal is not None else self.idx
        return c16_to_rgba(v)


def c16_to_rgba(v: np.ndarray) -> np.ndarray:
    v = v.astype(np.uint32)
    r = (v & 31) << 3; g = ((v >> 5) & 31) << 3; b = ((v >> 10) & 31) << 3
    out = np.stack([r | r >> 5, g | g >> 5, b | b >> 5, np.where(v == 0, 0, 255)], -1)
    return out.astype(np.uint8)


def parse_tim(b: bytes) -> Tim:
    assert b[:4] == b'\x10\x00\x00\x00'
    flags = struct.unpack_from('<I', b, 4)[0]
    bpp = [4, 8, 16, 24][flags & 3]
    off = 8
    clut = None
    if flags & 8:
        cl, cx, cy, cw, ch = struct.unpack_from('<IHHHH', b, off)
        clut = np.frombuffer(b, '<u2', cw * ch, off + 12).reshape(ch, cw)
        off += cl
    bl, x, y, w, h = struct.unpack_from('<IHHHH', b, off)
    raw = np.frombuffer(b, np.uint8, w * h * 2, off + 12).reshape(h, w * 2)
    if bpp == 4:
        idx = np.stack([raw & 15, raw >> 4], -1).reshape(h, w * 4)
    elif bpp == 8:
        idx = raw
    else:
        idx = raw.view('<u2')
    return Tim(bpp, idx.shape[1], h, idx.astype(np.int32), clut)


# ---------------------------------------------------------------- modelo
# (palavras, quad?, lit?, offset normal/cor, uv offsets por vértice, tex, clut, overlay: (uv, offset das normais))
# Quads: vértices v0..v3 = bytes 1, 2, 3, 0 da palavra 0; UV, normais e cores ficam em sequência v0, v1, v2, v3,
# mas a ordem na GPU é v0, v3, v1, v2 (FUN_80036874 põe o vértice do byte 0 em xy1), então a diagonal
# do quad é v1-v3: triângulos (v0, v1, v3) e (v3, v1, v2) — ver QUAD_TRIS.
L = [
    (2, 0, 1, 4, None, None, None, None), (2, 1, 1, 4, None, None, None, None),
    (4, 0, 1, 4, [8, 10, 12], 14, 15, None), (4, 1, 1, 4, [6, 8, 10, 12], 14, 15, None),
    (5, 0, 1, 4, [12, 14, 16], 18, 19, None), (6, 1, 1, 4, [12, 14, 16, 18], 20, 21, None),
    (3, 0, 1, 4, None, None, None, None), (3, 1, 1, 4, None, None, None, None),
    (6, 0, 1, 4, None, 22, 23, ([16, 18, 20], 10)), (8, 1, 1, 4, None, 28, 29, ([20, 22, 24, 26], 12)),
    (4, 0, 0, 4, [8, 10, 12], 14, 15, None), (5, 1, 0, 4, [8, 10, 12, 14], 16, 17, None),
    (5, 0, 1, 4, None, 18, 19, ([12, 14, 16], None)), (6, 1, 1, 4, None, 20, 21, ([12, 14, 16, 18], None)),
    (4, 0, 0, 4, None, None, None, None), (5, 1, 1, 4, None, 16, 17, ([8, 10, 12, 14], None)),
    (2, 0, 0, 4, None, None, None, None), (2, 1, 0, 4, None, None, None, None),
    (2, 0, 0, 4, None, None, None, None), (2, 1, 0, 4, None, None, None, None),
    (3, 0, 1, 4, None, None, None, None), (4, 1, 1, 4, None, None, None, None),
    (2, 0, 0, 4, None, None, None, None), (2, 1, 0, 4, None, None, None, None),
    (4, 0, 0, 4, None, None, None, None), (5, 1, 0, 4, None, None, None, None),
    (6, 0, 0, 4, [16, 18, 20], 22, 23, None), (8, 1, 0, 4, [20, 22, 24, 26], 28, 29, None),
    (6, 0, 0, 4, None, 22, 23, ([16, 18, 20], None)), (8, 1, 0, 4, None, 28, 29, ([20, 22, 24, 26], None)),
    (4, 0, 0, 4, None, None, None, None), (5, 1, 0, 4, None, None, None, None),
]
QUAD_TRIS = [[0, 1, 3], [3, 1, 2]]
# Atributo (UV, normal, cor) de cada vértice v0..v3 do quad, como índice na lista lida dos offsets acima.
# Medido na GPU em 2026-10-27 (gpu_frame_dump + heat_dump do mesmo quadro, 229 GT4/FT4 do HEAT casados com
# ~1 px de erro, tipos 3, 5 e 9): os slots A, B, C, D do GP0 recebem os vértices v2, v3, v1, v0 e os
# atributos 0, 3, 1, 2. Ou seja, v0 e v2 trocam de atributo em relação à sequência; v1 e v3 ficam. Sem isso
# a calça e os sapatos saíam com a chama em cacos. Triângulos: identidade (540 casados).
QUAD_ATTR = [2, 1, 0, 3]
GOURAUD = {4, 5, 6, 7, 8, 9, 12, 13, 20, 21, 24, 25, 26, 27, 28, 29, 30, 31}


@dataclass
class Prim:
    lst: int
    verts: list[int]                 # quad: v0,v1,v2,v3 -> triângulos QUAD_TRIS
    normals: list[int] | None = None
    colors: list[int] | None = None
    uv: list[tuple[int, int]] | None = None
    tex: int | None = None
    clut: int = 0
    semi: bool = False
    semi_bits: int = 0               # byte de textura >> 5 (0 = opaco)
    overlay: 'Prim | None' = None


@dataclass
class Mesh:
    verts: np.ndarray                # (n, 3) int
    normals: np.ndarray              # (m, 3) float (unitário)
    ncolor: np.ndarray               # (m, 3) material RGB 0..255 de cada normal
    refs: list[tuple[int, int]]      # local src -> global dst
    uses: list[tuple[int, int]]      # global src -> local dst
    prims: list[Prim]
    base_index: int = 0              # MeshHeader+0x98 (identifica o mesh também na RAM do jogo)


@dataclass
class Node:
    index: int
    type: int
    parent: int
    rot: tuple = (0, 0, 0)
    scale: tuple = (4096, 4096, 4096)
    pos: tuple = (0, 0, 0)
    mesh: Mesh | None = None
    instance_of: int | None = None
    shadow: dict | None = None


def _parse_mesh(b: bytes, p: int):
    u32 = lambda o: struct.unpack_from('<I', b, o)[0]
    nv = u32(p + 0x94)
    q = p + 0x120
    verts = np.frombuffer(b, '<i2', nv * 4, q).reshape(nv, 4)[:, :3].astype(np.int32)
    q += nv * 8
    normals, ncol = [], []
    for _ in range(u32(p + 0x9C) + u32(p + 0xA0)):
        r, g, bb, code = b[q:q + 4]
        n = u32(q + 4); q += 8
        nn = np.frombuffer(b, '<i2', n * 4, q).reshape(n, 4)[:, :3] / 4096.0
        q += n * 8
        normals.append(nn); ncol += [(r, g, bb)] * n
    refs = []
    for _ in range(u32(p + 0xA4)):
        w = u32(q); q += 4; refs.append((w & 0xFF, w >> 16))
    uses = []
    for _ in range(u32(p + 0xA8)):
        w = u32(q); q += 4; uses.append((w & 0xFFFF, w >> 16))
    prims = []
    for li in range(32):
        cnt = struct.unpack_from('<h', b, p + 0xAC + li * 2)[0]
        words, quad, lit, at, uvo, texo, cluto, over = L[li]
        for _ in range(cnt):
            w0 = u32(q)
            vs = [(w0 >> 8) & 255, (w0 >> 16) & 255, w0 >> 24] + ([w0 & 255] if quad else [])
            nv_ = len(vs)
            pr = Prim(li, vs)
            per_vertex = li in GOURAUD
            k = nv_ if per_vertex else 1
            if lit:
                pr.normals = [struct.unpack_from('<H', b, q + at + i * 2)[0] for i in range(k)]
            else:
                pr.colors = [tuple(b[q + at + i * 4:q + at + i * 4 + 3]) for i in range(k)]
            rd_uv = lambda offs: [(b[q + o], b[q + o + 1]) for o in offs]
            if uvo:
                pr.uv = rd_uv(uvo)
            if texo is not None:
                t = b[q + texo]
                tex, semi, cl = t & 0x1F, (t >> 5) != 0, b[q + cluto]
                if over is None:
                    pr.tex, pr.semi, pr.clut, pr.semi_bits = tex, semi, cl, t >> 5
                else:
                    ouv, onat = over
                    ov = Prim(li, vs, uv=rd_uv(ouv), tex=tex, semi=semi, clut=cl, semi_bits=t >> 5)
                    ov.normals = ([struct.unpack_from('<H', b, q + onat + i * 2)[0] for i in range(nv_)]
                                  if onat is not None else pr.normals)
                    ov.colors = pr.colors
                    pr.overlay = ov
            if quad:   # atributos por canto do quad: o vértice k usa o atributo QUAD_ATTR[k] (ver QUAD_ATTR)
                for o in (pr, pr.overlay):
                    if o is None:
                        continue
                    for name in ('uv', 'normals', 'colors'):
                        a = getattr(o, name)
                        if a is not None and len(a) == 4:
                            setattr(o, name, [a[i] for i in QUAD_ATTR])
            prims.append(pr)
            q += words * 4
    mesh = Mesh(verts, np.concatenate(normals) if normals else np.zeros((0, 3)),
                np.array(ncol, np.float32).reshape(-1, 3), refs, uses, prims, u32(p + 0x98))
    return mesh, q


def parse_model(b: bytes) -> list[Node]:
    n = struct.unpack_from('<I', b, 0)[0]
    nodes = []
    for i in range(n):
        parent = struct.unpack_from('<i', b, 4 + i * 16)[0]
        nodes.append(Node(i, 0, parent))
    p = 4 + n * 16
    for nd in nodes:
        t = struct.unpack_from('<I', b, p)[0]
        nd.type = t
        if t == 0:
            p += 4
        elif t == 2:
            nd.instance_of = struct.unpack_from('<i', b, p + 4)[0]; p += 8
        elif t == 3:
            p += 12 + struct.unpack_from('<I', b, p + 8)[0] * 8
        elif t == 4:
            nv = struct.unpack_from('<I', b, p + 16)[0]
            nt, nq = struct.unpack_from('<hh', b, p + 20)
            q = p + 0x4C
            sv = np.frombuffer(b, '<i2', nv * 4, q).reshape(nv, 4)[:, :3].astype(np.int32); q += nv * 8
            tris, quads = [], []
            for _ in range(nt):
                w = struct.unpack_from('<I', b, q)[0]; q += 8; tris.append([(w >> 8) & 255, (w >> 16) & 255, w >> 24])
            for _ in range(nq):
                w = struct.unpack_from('<I', b, q)[0]; q += 8
                quads.append([(w >> 8) & 255, (w >> 16) & 255, w >> 24, w & 255])
            nd.shadow = dict(verts=sv, tris=tris, quads=quads)
            p = q
        elif t == 1:
            nd.mesh, p = _parse_mesh(b, p)
        else:
            raise ValueError(f'nó {nd.index}: tipo {t} desconhecido em {p}')
        nd.rot = struct.unpack_from('<hhh', b, p)
        nd.scale = struct.unpack_from('<iii', b, p + 8)
        nd.pos = struct.unpack_from('<iii', b, p + 24)
        p += 40
    if p != len(b):
        print(f'aviso: modelo lido até {p} de {len(b)} bytes', file=sys.stderr)
    return nodes


# ---------------------------------------------------------------- animação
@dataclass
class Anim:
    frames: int
    step: int
    bone_map: list[int]
    keys: np.ndarray      # (nkeys, 3 + 3*bones) int


def split_anims(b: bytes) -> list[bytes | None]:
    out, p = [], 0
    while p + 4 <= len(b):
        n = struct.unpack_from('<I', b, p)[0]
        if n == 0:
            break
        d = b[p + 4:p + 4 + n]
        out.append(d if n >= 0x20 and struct.unpack_from('<I', d, 0)[0] == 0x1C else None)
        p += 4 + n
    return out


def _s16(v): return ((v & 0xFFFF) ^ 0x8000) - 0x8000


def parse_anim(b: bytes) -> Anim:
    hdr = struct.unpack_from('<I', b, 0)[0]
    frames = struct.unpack_from('<H', b, 4)[0]
    step = max(1, b[6]); bc = b[7]
    bone_map = list(b[8:8 + bc])
    p = hdr + 4
    ns = 3 + 3 * bc
    streams = []
    for _ in range(ns):
        size = struct.unpack_from('<H', b, p)[0]
        streams.append((b[p + 2] | b[p + 3] << 8, b[p + 4:p + size]))
        p += size
    nkeys = (frames - 2) // step + 2 if step > 1 else frames
    cur = [_s16(s[0]) for s in streams]
    pos = [0] * ns
    keys = [list(cur)]
    for _ in range(1, nkeys):
        for i in range(ns):
            s = streams[i][1]; q = pos[i]
            if q >= len(s):
                continue
            c = s[q]; q += 1
            if i < 3:
                if c != 0x80:
                    d = c - 256 if c > 127 else c
                else:
                    d = _s16(s[q] << 8 | s[q + 1]); q += 2
            elif (c & 0xF0) == 0xF0:
                d = ((c << 8 | s[q]) & 0xFFF); d = d - 4096 if d & 0x800 else d; q += 1
            else:
                d = c - 0x78
            cur[i] = _s16(cur[i] + d); pos[i] = q
        keys.append(list(cur))
    return Anim(frames, step, bone_map, np.array(keys, np.int32))


def pose_at(a: Anim, frame: float):
    """(rootPos(3), {nó: (rx,ry,rz)}) com interpolação linear entre keyframes."""
    f = int(frame) % a.frames
    k0 = f if a.step == 1 else ((a.frames - 2) // a.step + 1 if f == a.frames - 1 else f // a.step)
    k0 = min(k0, len(a.keys) - 1); k1 = min(k0 + 1, len(a.keys) - 1)
    t = (frame - int(frame)) if a.step == 1 else ((f % a.step) + (frame - int(frame))) / a.step
    if f == a.frames - 1:
        k1 = k0
    A, B = a.keys[k0].astype(float), a.keys[k1].astype(float)
    d = (B - A)
    d[3:] = (d[3:] + 2048) % 4096 - 2048
    v = A + d * t
    root = v[:3] / 2
    rot = {a.bone_map[i]: tuple(v[3 + 3 * i:6 + 3 * i]) for i in range(len(a.bone_map))}
    return root, rot


# ---------------------------------------------------------------- esqueleto
def _ang(a): return a / 4096.0 * 2 * np.pi


def rot_matrix(rx, ry, rz):
    """R = Rz·Ry·Rx (FUN_8001EEF8)."""
    cx, sx, cy, sy, cz, sz = np.cos(_ang(rx)), np.sin(_ang(rx)), np.cos(_ang(ry)), np.sin(_ang(ry)), np.cos(_ang(rz)), np.sin(_ang(rz))
    Rx = np.array([[1, 0, 0], [0, cx, -sx], [0, sx, cx]])
    Ry = np.array([[cy, 0, sy], [0, 1, 0], [-sy, 0, cy]])
    Rz = np.array([[cz, -sz, 0], [sz, cz, 0], [0, 0, 1]])
    return Rz @ Ry @ Rx


def node_matrices(nodes: list[Node], pose=None) -> list[np.ndarray]:
    out = []
    root, rots = pose if pose else (None, {})
    for nd in nodes:
        rot = rots.get(nd.index, nd.rot)
        pos = np.array(nd.pos, float)
        if root is not None and nd.index == 0:
            pos = pos + root
        M = np.eye(4)
        M[:3, :3] = rot_matrix(*rot) @ np.diag(np.array(nd.scale) / 4096.0)
        M[:3, 3] = pos
        out.append(out[nd.parent] @ M if nd.parent >= 0 else M)
    return out


def load_character(bin_path: str, k: int, alt: bool = False):
    """alt=True: figurino alternativo (modelo 195 + 2k, texturas 159 + 2k), o do jogador 2 numa partida espelhada."""
    mc = read_container(bin_path, (195 if alt else 123) + 2 * k)
    tc = read_container(bin_path, (159 if alt else 87) + 2 * k)
    nodes = parse_model(mc[0])
    anims = [parse_anim(c) if c else None for c in split_anims(mc[1])] if len(mc) > 1 else []
    tims = [parse_tim(c) for c in tc if c[:4] == b'\x10\x00\x00\x00']
    if tims and tims[0].h <= 64:   # retrato 64x56 não entra no índice `tex`
        tims = tims[1:]
    return nodes, anims, tims, mc
