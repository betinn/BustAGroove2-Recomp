"""Malha soldada do personagem (vértices únicos entre ossos), pose por frame e suavização (Loop ou Phong Tessellation).

O jogo desenha cada osso como um mesh rígido; vértices de junta são copiados de um osso para outro por um
buffer global (slot = nó * 256 + vértice local, ver `refs`/`uses` em docs/model-format.md). Aqui cada
vértice ganha um id único (dono = nó que o define), então a malha inteira vira uma superfície só,
com costuras soldadas nas juntas: dá para suavizar e animar sem rasgar.
"""
from __future__ import annotations

from dataclasses import dataclass

import numpy as np
import scipy.sparse as sp

from bag2model import QUAD_TRIS, Node, node_matrices

# Modos de material por face
BASE, OVER, TEXONLY = 0, 1, 2


@dataclass
class Rig:
    owner: np.ndarray       # (V,) nó dono de cada vértice único
    local: np.ndarray       # (V, 3) posição no espaço do nó dono
    tris: np.ndarray        # (F, 3) ids de vértice
    fnode: np.ndarray       # (F,) nó que desenha a face
    nidx: np.ndarray        # (F, 3) índice global da normal original de cada canto
    gnormal: np.ndarray     # (N, 3) normais do arquivo, concatenadas na ordem dos nós
    gnowner: np.ndarray     # (N,) nó dono de cada normal (o índice das primitivas é global, como o
                            #      dos vértices: juntas reusam a normal já iluminada do osso vizinho)
    base: np.ndarray        # (F, 3, 3) cor do material base 0..255 por canto
    over: np.ndarray        # (F, 3, 3) cor de modulação do overlay/textura 0..255 (128 = neutro)
    uv: np.ndarray          # (F, 3, 2) uv em texels (0..255)
    layer: np.ndarray       # (F,) camada de textura (-1 = sem)
    mode: np.ndarray        # (F,) BASE / OVER / TEXONLY
    layers: list            # [(tex, clut)] de cada camada
    order: np.ndarray | None = None  # (F,) ordem de inserção na OT do jogo (nó, lista na ordem de FUN_80029588, prim)
    zpair: np.ndarray | None = None  # (F, 2) vértices cujo Z dá o slot da OT no jogo: (z0 + z1) >> 3
    pcorner: np.ndarray | None = None  # (F, 3) vértice da primitiva (0..3, v0..v3 do arquivo) de cada canto


# Ordem em que FUN_80029588 percorre as 32 listas (contagens em 0xAC.. na ordem das chamadas)
GAME_LIST_ORDER = [0, 6, 2, 4, 8, 1, 7, 3, 5, 9] + list(range(10, 32))


def build_rig(nodes: list[Node], skip: set[int] = frozenset(), two_sided: bool = False, dedup: bool = True) -> Rig:
    """two_sided=True mantém faces com os mesmos vértices em ordem oposta (painel de dois lados): o jogo
    faz culling por NCLIP, então cada lado é uma face. Com False (offline, sem culling) fica só uma."""
    owner, local = [], []
    slots: dict[int, int] = {}
    tris, fnode, nrm, base, over, uv, layer, mode = [], [], [], [], [], [], [], []
    order, zpair, pcorner = [], [], []
    gn, gno, gc = [], [], []
    for nd in nodes:
        if nd.mesh is not None:
            gn.append(nd.mesh.normals); gc.append(nd.mesh.ncolor); gno += [nd.index] * len(nd.mesh.normals)
    gn, gc = np.concatenate(gn), np.concatenate(gc)
    layers: dict[tuple, int] = {}
    seen = set()
    for nd in nodes:
        m = nd.mesh
        if m is None:
            continue
        ids = list(range(len(owner), len(owner) + len(m.verts)))
        owner += [nd.index] * len(m.verts)
        local += list(m.verts)
        ids += [-1] * len(m.uses)
        for src, dst in m.uses:
            while dst >= len(ids):
                ids.append(-1)
            ids[dst] = slots[src]
        for src, dst in m.refs:
            slots[dst] = ids[src]
        if nd.index in skip:
            continue
        list_pos: dict[int, int] = {}
        for p in m.prims:
            pi = list_pos.get(p.lst, 0); list_pos[p.lst] = pi + 1
            q = p.overlay or p
            corners = [[0, 1, 2]] if len(p.verts) == 3 else QUAD_TRIS
            for c in corners:
                vid = [ids[p.verts[k]] for k in c]
                if two_sided:   # mesma face = mesma ordem cíclica
                    i0 = vid.index(min(vid)); key = tuple(vid[i0:] + vid[:i0])
                else:
                    key = tuple(sorted(vid))
                if len(set(vid)) < 3 or (dedup and key in seen):   # duplicadas/degeneradas quebram a topologia
                    continue
                seen.add(key)
                tris.append(vid)
                fnode.append(nd.index)
                order.append((nd.index, GAME_LIST_ORDER.index(p.lst), pi))
                pcorner.append(list(c))
                # slot da OT: z(v0) + z(v1) nos triângulos, z(v0) + z(v2) nos quads (FUN_80036230 / FUN_80036874)
                zpair.append((ids[p.verts[0]], ids[p.verts[1 if len(p.verts) == 3 else 2]]))

                def corner_color(pr, k):
                    if pr.normals is not None:
                        ni = pr.normals[0 if len(pr.normals) == 1 else k]
                        return ni, gc[ni]
                    col = pr.colors[0 if len(pr.colors) == 1 else k]
                    return None, np.array(col, np.float32)

                nn, bb, oo = [], [], []
                for k in c:
                    n0, c0 = corner_color(p, k)
                    n1, c1 = corner_color(q, k) if q is not p else (n0, c0)
                    nn.append(n0 if n0 is not None else (n1 if n1 is not None else -1))
                    bb.append(c0); oo.append(c1)
                nrm.append(nn); base.append(bb); over.append(oo)
                if q.tex is not None and q.uv:
                    lk = (q.tex, q.clut)
                    layer.append(layers.setdefault(lk, len(layers)))
                    uv.append([q.uv[k] for k in c])
                    mode.append(OVER if p.overlay else TEXONLY)
                else:
                    layer.append(-1); uv.append([(0, 0)] * 3); mode.append(BASE)
    return Rig(np.array(owner), np.array(local, float), np.array(tris), np.array(fnode),
               np.array(nrm), gn, np.array(gno), np.array(base, np.float32), np.array(over, np.float32),
               np.array(uv, np.float32), np.array(layer), np.array(mode), list(layers),
               np.array(order), np.array(zpair), np.array(pcorner))


def pose_positions(rig: Rig, nodes: list[Node], pose=None):
    """Posições mundiais (V,3) e matrizes dos nós para uma pose (None = descanso)."""
    mats = np.array(node_matrices(nodes, pose))
    M = mats[rig.owner]
    p = np.einsum('vij,vj->vi', M[:, :3, :3], rig.local) + M[:, :3, 3]
    return p, mats


def orient_faces(rig: Rig, nodes: list[Node]) -> int:
    """Vira as faces cuja ordem de vértices discorda das normais do arquivo (o PS1 não depende da
    ordem: o jogo desenha sem culling). Com orientação coerente as normais suaves não se cancelam e o
    contorno por casco invertido funciona. Devolve quantas faces viraram."""
    p, mats = pose_positions(rig, nodes)
    t = rig.tris
    g = np.cross(p[t[:, 1]] - p[t[:, 0]], p[t[:, 2]] - p[t[:, 0]])
    n = original_normals(rig, mats).sum(1)
    flip = np.einsum('fd,fd->f', g, n) < 0
    for a in (rig.tris, rig.nidx, rig.base, rig.over, rig.uv):
        a[flip, 1], a[flip, 2] = a[flip, 2].copy(), a[flip, 1].copy()
    return int(flip.sum())


def original_normals(rig: Rig, mats) -> np.ndarray:
    """Normais por canto (F,3,3) do arquivo, rotacionadas pelo nó dono de cada normal.
    Cantos sem normal (primitivas de cor fixa) ficam com normal zero."""
    R = mats[rig.gnowner][:, :3, :3]
    g = np.einsum('nij,nj->ni', R, rig.gnormal)
    n = g[np.maximum(rig.nidx, 0)]
    n[rig.nidx < 0] = 0
    return n / np.maximum(np.linalg.norm(n, axis=-1, keepdims=True), 1e-9)


# ---------------------------------------------------------------- subdivisão
@dataclass
class Subdiv:
    S: sp.csr_matrix        # (V', V) posições novas = S @ posições originais
    tris: np.ndarray        # (F', 3)
    parent: np.ndarray      # (F',) face original
    bary: np.ndarray        # (F', 3, 3) baricêntricas de cada canto na face original


def loop_step(tris: np.ndarray, nv: int, crease: set, pin_open: bool = True):
    """Um passo de Loop com vincos. `crease` = arestas vivas (pares ordenados), tratadas como borda.
    Devolve (S, tris novos, filho->pai, bary local dos cantos, vincos do nível seguinte)."""
    F = len(tris)
    e = np.concatenate([tris[:, [0, 1]], tris[:, [1, 2]], tris[:, [2, 0]]])   # aresta k da face f em k*F+f
    es = np.sort(e, 1)
    uniq, inv, cnt = np.unique(es, axis=0, return_inverse=True, return_counts=True)
    inv = inv.ravel()
    ne = len(uniq)
    opp = np.concatenate([tris[:, 2], tris[:, 0], tris[:, 1]])               # vértice oposto a cada aresta
    open_e = cnt != 2                                                        # borda ou não-manifold
    sharp = open_e | np.array([(x, y) in crease for x, y in uniq], bool)    # ... ou vinco
    rows, cols, vals = [], [], []
    # vértices de aresta
    a, b = uniq[:, 0], uniq[:, 1]
    rows += [nv + np.arange(ne)] * 2; cols += [a, b]
    vals += [np.where(sharp, 0.5, 0.375)] * 2
    inner = ~sharp[inv]
    rows.append(nv + inv[inner]); cols.append(opp[inner]); vals.append(np.full(inner.sum(), 0.125))
    # vértices antigos
    nb = [set() for _ in range(nv)]
    snb = [set() for _ in range(nv)]
    pinned = np.zeros(nv, bool)
    for (x, y), sh, op, c in zip(uniq, sharp, open_e, cnt):
        nb[x].add(y); nb[y].add(x)
        if sh:
            snb[x].add(y); snb[y].add(x)
        if op and (pin_open or c != 1):          # não-manifold sempre parado; borda simples opcional
            pinned[x] = pinned[y] = True
    r2, c2, v2 = [], [], []
    for v in range(nv):
        k = len(snb[v])
        if pinned[v]:
            # vértice de borda aberta: fica parado. O modelo do PS1 é cheio de painéis sobrepostos e
            # junções em T (930 de 2492 arestas do HEAT são borda); a regra de borda do Loop encolhe essas
            # bordas e abre buracos onde antes um painel cobria o outro.
            r2.append(v); c2.append(v); v2.append(1.0); continue
        if k == 2:                                   # vértice de vinco/borda: curva B-spline
            r2 += [v, v, v]; c2 += [v, *snb[v]]; v2 += [0.75, 0.125, 0.125]
            continue
        n = len(nb[v])
        if k > 2 or n == 0:                          # quina: fica parado (pontas de cabelo, etc.)
            r2.append(v); c2.append(v); v2.append(1.0); continue
        beta = 3 / 16 if n == 3 else 3 / (8 * n)
        r2 += [v] + [v] * n; c2 += [v, *nb[v]]; v2 += [1 - n * beta] + [beta] * n
    rows.append(np.array(r2)); cols.append(np.array(c2)); vals.append(np.array(v2))
    S = sp.csr_matrix((np.concatenate(vals), (np.concatenate(rows), np.concatenate(cols))), shape=(nv + ne, nv))
    m01, m12, m20 = nv + inv[:F], nv + inv[F:2 * F], nv + inv[2 * F:]
    t0, t1, t2 = tris[:, 0], tris[:, 1], tris[:, 2]
    new = np.concatenate([np.stack([t0, m01, m20], 1), np.stack([m01, t1, m12], 1),
                          np.stack([m20, m12, t2], 1), np.stack([m01, m12, m20], 1)])
    parent = np.tile(np.arange(F), 4)
    I = np.eye(3); h01, h12, h20 = (I[0] + I[1]) / 2, (I[1] + I[2]) / 2, (I[2] + I[0]) / 2
    bl = np.array([[I[0], h01, h20], [h01, I[1], h12], [h20, h12, I[2]], [h01, h12, h20]])
    bary = np.repeat(bl, F, axis=0)
    nc = set()
    for i in np.nonzero(sharp & (cnt == 2))[0]:
        x, y, m = int(uniq[i, 0]), int(uniq[i, 1]), nv + int(i)
        nc.add((x, m)); nc.add((y, m))           # m > x, y: pares já ordenados
    return S, new, parent, bary, nc


def crease_edges(p: np.ndarray, tris: np.ndarray, deg: float) -> set:
    """Arestas cujo ângulo diedro (entre normais das faces) passa de `deg` graus."""
    fn = np.cross(p[tris[:, 1]] - p[tris[:, 0]], p[tris[:, 2]] - p[tris[:, 0]])
    fn /= np.maximum(np.linalg.norm(fn, axis=1, keepdims=True), 1e-12)
    faces: dict = {}
    for f, t in enumerate(tris):
        for k in range(3):
            faces.setdefault(tuple(sorted((int(t[k]), int(t[(k + 1) % 3])))), []).append(f)
    c = np.cos(np.radians(deg))
    return {e for e, fs in faces.items() if len(fs) == 2 and fn[fs[0]] @ fn[fs[1]] < c}


def subdivide(tris: np.ndarray, nv: int, levels: int, p_rest: np.ndarray | None = None,
              crease_deg: float = 80.0, pin_open: bool = True) -> Subdiv:
    S = sp.identity(nv, format='csr')
    cur, parent, bary = tris, np.arange(len(tris)), np.tile(np.eye(3), (len(tris), 1, 1))
    crease = crease_edges(p_rest, tris, crease_deg) if p_rest is not None and crease_deg < 180 else set()
    for _ in range(levels):
        s, cur, par, bl, crease = loop_step(cur, S.shape[0], crease, pin_open)
        S = s @ S
        bary = np.einsum('fij,fjk->fik', bl, bary[par])
        parent = parent[par]
    return Subdiv(S.tocsr(), cur, parent, bary)


def tess_pattern(level: int) -> np.ndarray:
    """Baricêntricas (C, 3, 3) dos cantos dos level² triângulos de uma face dividida `level` vezes por aresta."""
    out = []
    L = level
    for i in range(L):
        for j in range(L - i):
            a = np.array([L - i - j, i, j]); b = np.array([L - i - j - 1, i + 1, j]); c = np.array([L - i - j - 1, i, j + 1])
            out.append([a, b, c])
            if j < L - i - 1:
                d = np.array([L - i - j - 2, i + 1, j + 1])
                out.append([b, d, c])
    return np.array(out, float) / L


def phong_tessellate(p: np.ndarray, tris: np.ndarray, vn: np.ndarray, level: int, alpha: float = 0.6):
    """Phong Tessellation (Boubekeur & Alexa 2008): cada ponto da face é a média baricêntrica das suas
    projeções nos planos tangentes dos 3 vértices. Só depende de posição + normal de cada vértice, então
    funciona numa "sopa" de painéis abertos (o caso do PS1) e não abre buracos: a curva de uma aresta
    depende só dos seus dois vértices. Devolve (pontos (F*C,3,3), filho->pai, bary (F*C,3,3))."""
    B = tess_pattern(level)                       # (C,3,3)
    F, C = len(tris), len(B)
    P = p[tris]                                   # (F,3,3) vértice k da face
    N = vn[tris]
    q = np.einsum('ckj,fjd->fckd', B, P)          # ponto linear (F,C,3,3)
    # projeção de q no plano tangente de cada vértice j: q - ((q - Pj)·Nj) Nj
    d = np.einsum('fckjd,fjd->fckj', q[:, :, :, None, :] - P[:, None, None], N)
    proj = q[:, :, :, None, :] - d[..., None] * N[:, None, None]
    ph = np.einsum('ckj,fckjd->fckd', B, proj)
    out = (1 - alpha) * q + alpha * ph
    return out.reshape(F * C, 3, 3), np.repeat(np.arange(F), C), np.tile(B, (F, 1, 1))


def vertex_normals(p: np.ndarray, tris: np.ndarray) -> np.ndarray:
    """Normal por vértice (média das faces ponderada por área)."""
    fn = np.cross(p[tris[:, 1]] - p[tris[:, 0]], p[tris[:, 2]] - p[tris[:, 0]])
    acc = np.zeros((len(p), 3))
    for k in range(3):
        np.add.at(acc, tris[:, k], fn)
    return acc / np.maximum(np.linalg.norm(acc, axis=1, keepdims=True), 1e-12)


def smooth_normals(p: np.ndarray, tris: np.ndarray, crease_deg: float = 75.0) -> np.ndarray:
    """Normais por canto (F,3,3): média ponderada por área das faces vizinhas, quebrando em vincos."""
    fn = np.cross(p[tris[:, 1]] - p[tris[:, 0]], p[tris[:, 2]] - p[tris[:, 0]])
    fu = fn / np.maximum(np.linalg.norm(fn, axis=1, keepdims=True), 1e-12)
    V = len(p)
    acc = np.zeros((V, 3))
    for k in range(3):
        np.add.at(acc, tris[:, k], fn)
    vn = acc / np.maximum(np.linalg.norm(acc, axis=1, keepdims=True), 1e-12)
    out = vn[tris]
    # vinco: se a normal suave diverge muito da face, refaz a média só com faces próximas
    cosl = np.cos(np.radians(crease_deg))
    bad = np.einsum('fkd,fd->fk', out, fu) < cosl
    if bad.any():
        inc = [[] for _ in range(V)]
        for f, t in enumerate(tris):
            for v in t:
                inc[v].append(f)
        for f, k in zip(*np.nonzero(bad)):
            fs = inc[tris[f, k]]
            sel = [g for g in fs if fu[g] @ fu[f] > cosl]
            s = fn[sel].sum(0)
            out[f, k] = s / max(np.linalg.norm(s), 1e-12)
    return out
