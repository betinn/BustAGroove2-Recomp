"""Corpo híbrido do experimento de IA (fase 2): a roupa vem da malha da IA (archive/ai/ai_mesh.py, encaixada por
archive/ai/ai_fit.py na pose em A) e o resto continua (cabeça, peça das costas, sapatos, mãos, orelhas, nariz).

- Cada vértice da IA fica rígido no osso do vértice original mais próximo (pose em A), com uma passada de maioria
  entre vizinhos para limpar a fronteira; as faces ficam só onde os 3 vértices caem nos ossos da roupa. É o mesmo
  esquema do PS1 (vértice rígido num osso, faces da junta esticam), então nada rasga na dança.
- Textura: cada face da IA pega a face original mais próxima (preferindo as texturizadas, onde estão as chamas e
  os decalques) e o UV, as cores e a camada dela por baricêntricas presas.
- Enrolamento do jogo (normal para dentro), grupo 0 (corpo).
"""
from __future__ import annotations

import copy

import numpy as np
from scipy.spatial import cKDTree

# ossos da roupa (quadril, tronco, ombros, gola, braços e antebraços, coxas e pernas); fora: 5/7 sapatos,
# 12 peça das costas, 13 cabeça, 16/19 mãos (e as alternativas)
CLOTH = (0, 1, 2, 3, 4, 6, 8, 9, 10, 11, 14, 15, 17, 18)


def _closest_on_tri(p, a, b, c):
    """Baricêntricas (presas ao triângulo) do ponto mais próximo de p em (a, b, c); vetorizado (N, 3)."""
    ab, ac = b - a, c - a
    n = np.cross(ab, ac)
    nn = np.maximum((n * n).sum(1), 1e-20)
    q = p - n * (((p - a) * n).sum(1) / nn)[:, None]
    v0, v1, v2 = ab, ac, q - a
    d00 = (v0 * v0).sum(1); d01 = (v0 * v1).sum(1); d11 = (v1 * v1).sum(1)
    d20 = (v2 * v0).sum(1); d21 = (v2 * v1).sum(1)
    den = np.maximum(d00 * d11 - d01 * d01, 1e-20)
    l1 = (d11 * d20 - d01 * d21) / den; l2 = (d00 * d21 - d01 * d20) / den
    L = np.clip(np.stack([1 - l1 - l2, l1, l2], 1), 0, 1)
    L /= L.sum(1, keepdims=True)
    proj = a * L[:, :1] + b * L[:, 1:2] + c * L[:, 2:]
    return L, np.linalg.norm(p - proj, axis=1)


def attach_ai_body(rig, nodes, W, F, pose, cloth=CLOTH):
    from rig import pose_positions
    r = copy.copy(rig)
    p0, mats = pose_positions(rig, nodes, pose)
    body = getattr(rig, 'hand', np.zeros(len(rig.tris), int)) == 0
    drawn = body | np.isin(rig.hand, (1, 4)) if getattr(rig, 'hand', None) is not None else body
    ref = np.unique(rig.tris[drawn])
    tree = cKDTree(p0[ref])
    _, j = tree.query(W)
    own = rig.owner[ref[j]].copy()
    # maioria entre vizinhos (1 passada): tira ilhas de um vértice na fronteira entre ossos
    nb = [set() for _ in range(len(W))]
    for t in F:
        for k in range(3):
            nb[t[k]].update((t[(k + 1) % 3], t[(k + 2) % 3]))
    o2 = own.copy()
    for v in range(len(W)):
        vals = [own[u] for u in nb[v]]
        if vals:
            u, c = np.unique(vals, return_counts=True)
            if c.max() > len(vals) / 2 and u[c.argmax()] != own[v]:
                o2[v] = u[c.argmax()]
    own = o2
    keep = np.isin(own[F], cloth).all(1)
    # face entre ossos que não são vizinhos no esqueleto (uma perna e a outra, braço e tronco longe da junta)
    # vira membrana esticada na dança: fora
    par = {nd.index: nd.parent for nd in nodes}
    def adj(a, b):
        return a == b or par.get(a) == b or par.get(b) == a
    o3 = own[F]
    keep &= np.array([adj(x, y) and adj(y, z) and adj(x, z) for x, y, z in o3])
    F = F[keep]
    used = np.unique(F)
    remap = -np.ones(len(W), int); remap[used] = np.arange(len(used))
    Wv, own, F = W[used], own[used], remap[F]
    # espaço local do osso dono (pose em A)
    inv = np.linalg.inv(mats[own])
    local = np.einsum('vij,vj->vi', inv[:, :3, :3], Wv) + inv[:, :3, 3]
    # textura: face original mais próxima entre as da roupa, preferindo as texturizadas
    of = np.nonzero(drawn & np.isin(rig.fnode, cloth))[0]
    A, B, C = p0[rig.tris[of, 0]], p0[rig.tris[of, 1]], p0[rig.tris[of, 2]]
    cen = (A + B + C) / 3
    ctree = cKDTree(cen)
    fc = Wv[F].mean(1)
    k = 12
    _, cand = ctree.query(fc, k=k)
    best = np.full(len(F), np.inf); pick = np.zeros(len(F), int)
    h = p0[ref][:, 1].max() - p0[ref][:, 1].min()
    tol = 0.02 * h
    for i in range(k):
        f = of[cand[:, i]]
        _, d = _closest_on_tri(fc, A[cand[:, i]], B[cand[:, i]], C[cand[:, i]])
        score = d + np.where((rig.mode[f] & 3) == 0, tol, 0.0) - 1e-6 * rig.order[f, 1]
        m = score < best
        best[m] = score[m]; pick[m] = f[m]
    uv = np.zeros((len(F), 3, 2), np.float32); base = np.zeros((len(F), 3, 3), np.float32)
    over = np.zeros((len(F), 3, 3), np.float32)
    PA, PB, PC = p0[rig.tris[pick, 0]], p0[rig.tris[pick, 1]], p0[rig.tris[pick, 2]]
    for c in range(3):
        L, _ = _closest_on_tri(Wv[F[:, c]], PA, PB, PC)
        uv[:, c] = np.einsum('fj,fjd->fd', L, rig.uv[pick])
        base[:, c] = np.einsum('fj,fjd->fd', L, rig.base[pick])
        over[:, c] = np.einsum('fj,fjd->fd', L, rig.over[pick])
    # enrolamento do jogo: a malha da IA sai com a normal para fora
    T = F[:, ::-1] + len(rig.local)
    uv, base, over = uv[:, ::-1], base[:, ::-1], over[:, ::-1]
    # tira as faces originais da roupa; o resto (e as mãos, orelhas e nariz) fica
    drop = np.isin(rig.fnode, cloth) & (getattr(rig, 'hand', np.zeros(len(rig.tris), int)) == 0)
    sel = np.nonzero(~drop)[0]
    n = len(T)
    add = dict(tris=T, fnode=own[F[:, 0]], nidx=np.full((n, 3), -1), base=base, over=over, uv=uv,
               layer=rig.layer[pick], mode=rig.mode[pick],
               order=np.stack([own[F[:, 0]], np.full(n, 45), np.arange(n)], 1), zpair=T[:, :2])
    for name, v in add.items():
        setattr(r, name, np.concatenate([getattr(rig, name)[sel], v]))
    if getattr(rig, 'hand', None) is not None:
        r.hand = np.concatenate([rig.hand[sel], np.zeros(n, int)])
    r.local = np.concatenate([rig.local, local])
    r.owner = np.concatenate([rig.owner, own])
    return r, dict(faces=n, verts=len(Wv))
