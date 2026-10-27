"""Orelhas de anime (borda e concha) para o remake do HEAT (notes/remake-plano.md, fase 2).

A orelha original é um cone de 16 faces de pele (cor 230,156,102) no osso da cabeça (nó 13): 7 vértices da
base presos à cabeça e 6 só dela. A orelha nova é uma folha pontuda gerada por código no mesmo osso:
- a orelha velha é uma placa no plano dela (a borda de trás é soldada ao cabelo quase até a ponta): os
  vértices só dela vão para esse plano, e a nova cobre a placa inteira (pé embaixo, ponta na da original);
- frente (lado de fora) com borda levantada (hélice) e concha funda no meio; costas arredondadas;
- a raiz entra um pouco na cabeça (a junção fica escondida) e a ponta se afasta da cabeça;
Cor de pele lisa; a concha e a dobra da borda escurecem pela cor do vértice (a oclusão ambiente do asset
ajuda). Faces no enrolamento do jogo (normal para dentro pela regra da mão direita).
"""
from __future__ import annotations

import copy

import numpy as np

HEAD = 13
SKIN = (230, 156, 102)
# faces da orelha do lado +x (índices na malha do build_rig sem deduplicar, two_sided)
EAR_FACES_PX = [780, 807, 828, 829, 853, 855, 891, 892, 903, 904, 905, 906, 921, 922, 943, 944]


def _ear_faces(rig):
    """Faces de pele da orelha de cada lado (+x e espelho em -x), pela cor e posição no nó 13."""
    f = np.nonzero((rig.fnode == HEAD) & (rig.mode == 0))[0]
    skin = f[(np.abs(rig.base[f, 0] - SKIN) < 1).all(1)]
    c = rig.local[rig.tris[skin]].mean(1)
    sides = []
    for sgn in (1, -1):
        m = (c[:, 0] * sgn > 1700) & (c[:, 1] > 500) & (c[:, 1] < 3000)
        sides.append(skin[m])
    return sides


def ear_surface(nu=12, nv=10, bowl=0.55, rim=0.9):
    """Folha da orelha em coordenadas (u ao longo da orelha, v de lado, h para fora), unitária.
    Devolve (P (n, 3), T (m, 3), shade (n,)): P[:, 0] = u em [-0,2, 1], P[:, 1] = largura, P[:, 2] = altura
    na direção de fora; shade = escurecimento da cor (concha e dobra da borda). Normal para fora."""
    us = np.r_[-0.2, np.linspace(0, 1, nu + 1)[:-1]]
    vs = np.linspace(-1, 1, nv + 1)
    rows_f, rows_b = [], []
    P, S = [], []

    def width(u):
        u = np.clip(u, 0, 1)
        return (1 - u) ** 0.85 * (0.62 + 0.38 * np.sin(np.pi * np.clip(u * 1.3, 0, 1)))

    for u in us:
        w = width(u)
        rf, rb = [], []
        for j, v in enumerate(vs):
            e = abs(v)
            y = v * w * 0.5
            # frente: borda levantada (mais no lado de trás da orelha, v > 0), concha no meio
            rim_h = rim * (0.6 + 0.4 * (v > 0)) * np.clip((e - 0.55) / 0.35, 0, 1) ** 1.5 * (1 - 0.6 * np.clip(u, 0, 1))
            bowl_h = bowl * (1 - e * e) * (1 - np.clip(u, 0, 1) ** 1.5)
            hf = 0.22 + rim_h - bowl_h * (1.0 if u > 0 else 0.3)
            hb = -0.22 - 0.25 * (1 - e ** 3) * (1 - np.clip(u, 0, 1))
            if j in (0, nv):     # a borda junta frente e costas
                hm = 0.5 * (hf + hb)
                P.append((u, y, hm)); S.append(0.92)
                rf.append(len(P) - 1); rb.append(len(P) - 1)
                continue
            P.append((u, y, hf)); S.append(1.0 - 0.28 * np.clip(bowl_h / bowl, 0, 1) - 0.12 * np.clip(rim_h / rim, 0, 1))
            rf.append(len(P) - 1)
            P.append((u, y, hb)); S.append(0.96)
            rb.append(len(P) - 1)
        rows_f.append(rf); rows_b.append(rb)
    P.append((1.0, 0.0, 0.0)); S.append(0.95)
    tip = len(P) - 1
    T = []
    for rows, flip in ((rows_f, False), (rows_b, True)):
        for i in range(len(us) - 1):
            a, b = rows[i], rows[i + 1]
            for j in range(nv):
                q = [(a[j], b[j], b[j + 1]), (a[j], b[j + 1], a[j + 1])]
                T += [t[::-1] if flip else t for t in q]
        last = rows[-1]
        for j in range(nv):
            t = (last[j], tip, last[j + 1])
            T.append(t[::-1] if flip else t)
    P, T = np.array(P, float), np.array(T, int)
    # normal para fora: frente (h > 0) deve apontar para +h
    n = np.cross(P[T[:, 1]] - P[T[:, 0]], P[T[:, 2]] - P[T[:, 0]])
    c = P[T].mean(1)
    if (np.sign(n[:, 2]) * np.sign(c[:, 2] + 1e-9)).mean() < 0:
        T = T[:, ::-1].copy()
    return P, T, np.array(S)


def attach_ears(rig, length_scale=1.08, width_scale=0.95, height=480.0, lift=220.0, sink=260.0, **kw):
    """Troca as orelhas do nó 13 pelas novas (corpo, grupo 0). Devolve um rig novo."""
    r = copy.copy(rig)
    local = rig.local.copy()
    sides = _ear_faces(rig)
    P, T, S = ear_surface(**kw)
    new_local, new_owner = [local], [rig.owner]
    fields = dict(tris=[rig.tris], fnode=[rig.fnode], nidx=[rig.nidx], base=[rig.base], over=[rig.over],
                  uv=[rig.uv], layer=[rig.layer], mode=[rig.mode], order=[rig.order], zpair=[rig.zpair])
    hand = [rig.hand] if getattr(rig, 'hand', None) is not None else None
    nv = len(local)
    head_c = local[rig.owner == HEAD].mean(0)
    for side, faces in enumerate(sides):
        if not len(faces):
            continue
        vs = np.unique(rig.tris[faces])
        others = np.setdiff1d(np.nonzero(rig.fnode == HEAD)[0], faces)
        shared = np.intersect1d(vs, np.unique(rig.tris[others]))
        own = np.setdiff1d(vs, shared)
        # a orelha velha é uma placa (a borda de trás é soldada ao cabelo quase até a ponta): plano pelo PCA,
        # normal para fora da cabeça; os vértices só dela vão para o plano (um pouco para dentro)
        C = local[vs].mean(0)
        _, _, Vt = np.linalg.svd(local[vs] - C)
        out = Vt[2] if Vt[2] @ (C - head_c) > 0 else -Vt[2]
        tip = own[np.argmax(np.linalg.norm(local[own] - C, axis=1))]
        d = local[tip] - C
        d -= out * (d @ out)
        d /= np.linalg.norm(d)
        side_ax = np.cross(out, d)
        if side_ax[2] > 0:       # v > 0 = borda de trás (para -z, a nuca), dos dois lados
            side_ax = -side_ax
        frame_flip = np.linalg.det(np.stack([d, side_ax, out])) < 0
        q = local[vs] - C
        a_d, a_s = q @ d, q @ side_ax
        B = C + d * a_d.min() + side_ax * 0.5 * (a_s.min() + a_s.max())   # pé da placa, no meio
        L = (a_d.max() - a_d.min()) * length_scale
        width = (a_s.max() - a_s.min()) * width_scale
        local[own] = local[own] - out[None] * ((local[own] - C) @ out + 60.0)[:, None]
        # pontos: u ao longo de d, largura em side_ax, altura em out; a ponta afasta da cabeça (lift)
        u = P[:, 0]
        pos = (B[None] - out[None] * (sink * (u < 0))[:, None] + d[None] * (u * L)[:, None] + side_ax[None] * (P[:, 1] * width)[:, None]
               + out[None] * (P[:, 2] * height + lift * np.clip(u, 0, 1) ** 1.4)[:, None])
        TT = T[:, ::-1] if frame_flip else T     # base (d, lado, fora) canhota inverte a orientação
        # enrolamento do jogo: normal para dentro
        TT = TT[:, ::-1] + nv
        nv += len(pos)
        new_local.append(pos); new_owner.append(np.full(len(pos), HEAD))
        F = len(TT)
        col = np.asarray(SKIN, np.float32)[None, None] * S[TT - (nv - len(pos))][..., None]
        fields['tris'].append(TT); fields['fnode'].append(np.full(F, HEAD)); fields['nidx'].append(np.full((F, 3), -1))
        fields['base'].append(col.astype(np.float32)); fields['over'].append(np.full((F, 3, 3), 128, np.float32))
        fields['uv'].append(np.zeros((F, 3, 2), np.float32)); fields['layer'].append(np.full(F, -1))
        fields['mode'].append(np.zeros(F, int))
        fields['order'].append(np.stack([np.full(F, HEAD), np.full(F, 40), np.arange(F) + 1000 * side], 1))
        fields['zpair'].append(TT[:, :2])
        if hand is not None:
            hand.append(np.zeros(F, int))
    for k, v in fields.items():
        setattr(r, k, np.concatenate(v))
    r.local = np.concatenate(new_local)
    r.owner = np.concatenate(new_owner)
    if hand is not None:
        r.hand = np.concatenate(hand)
    return r
