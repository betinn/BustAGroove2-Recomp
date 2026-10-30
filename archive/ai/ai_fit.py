#!/usr/bin/env python3
"""Encaixe da malha da IA (archive/ai/ai_mesh.py) no HEAT e comparação offline (fase 2, experimento de IA).

1. Escala e centro pela caixa, orientação (0 ou 180 graus em y) pela distância de Chamfer contra a superfície
   da pose em A do original, depois ICP de similaridade (Umeyama) com vizinho mais próximo.
2. Render lado a lado: ORIGINAL (texturas HD), MALHA DA IA (só forma, cinza) e REMAKE atual, em 4 ângulos.
Uso (venv do remake): python archive/ai/ai_fit.py [malha .npz em logs/remake/ai/] [ângulo da pose em A]
Saída: logs/remake/ai/<malha>_encaixe.npz (vértices no mundo da pose em A) e <malha>_compara.png.
"""
from __future__ import annotations

import os
import sys

import numpy as np
from scipy.spatial import cKDTree

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(HERE, '..'))

from heat_remake import OUT, Character, hd_layers, label, ps1_layers, side_by_side  # noqa: E402
from render import Renderer, pack  # noqa: E402
from rig import pose_positions, vertex_normals  # noqa: E402

AI = os.path.join(OUT, 'ai')


def a_pose(angle=450):
    return np.zeros(3), {14: (0, 0, angle), 17: (0, 4096, angle)}


def surface_points(p, tris, n=40000, seed=0):
    rng = np.random.default_rng(seed)
    a, b, c = p[tris[:, 0]], p[tris[:, 1]], p[tris[:, 2]]
    area = np.linalg.norm(np.cross(b - a, c - a), axis=1)
    f = rng.choice(len(tris), n, p=area / area.sum())
    u, v = rng.random(n), rng.random(n)
    m = u + v > 1
    u[m], v[m] = 1 - u[m], 1 - v[m]
    return a[f] + (b[f] - a[f]) * u[:, None] + (c[f] - a[f]) * v[:, None]


def umeyama(src, dst):
    """Similaridade (s, R, t) que leva src a dst por mínimos quadrados."""
    ms, md = src.mean(0), dst.mean(0)
    xs, xd = src - ms, dst - md
    U, S, Vt = np.linalg.svd(xd.T @ xs / len(src))
    D = np.eye(3)
    if np.linalg.det(U @ Vt) < 0:
        D[2, 2] = -1
    R = U @ D @ Vt
    s = np.trace(np.diag(S) @ D) / (xs ** 2).sum(1).mean()
    return s, R, md - s * R @ ms


def chamfer(a, b):
    return cKDTree(b).query(a)[0].mean() + cKDTree(a).query(b)[0].mean()


def fit(V, target, iters=30):
    lo, hi = target.min(0), target.max(0)
    best = None
    for yaw in (0.0, 180.0):
        c, s = np.cos(np.radians(yaw)), np.sin(np.radians(yaw))
        Ry = np.array([[c, 0, s], [0, 1, 0], [-s, 0, c]])
        W = V @ Ry.T
        sc = (hi[1] - lo[1]) / (W[:, 1].max() - W[:, 1].min())
        W = (W - (W.min(0) + W.max(0)) / 2) * sc + (lo + hi) / 2
        d = chamfer(W[::7], target)
        if best is None or d < best[0]:
            best = (d, W, yaw)
    d, W, yaw = best
    tree = cKDTree(target)
    sub = W[::5]
    for _ in range(iters):
        _, j = tree.query(sub)
        s, R, t = umeyama(sub, target[j])
        W = s * W @ R.T + t
        sub = s * sub @ R.T + t
    return W, yaw, d, chamfer(W[::7], target)


def main():
    name = sys.argv[1] if len(sys.argv) > 1 else 'malha_frente_256'
    angle = int(sys.argv[2]) if len(sys.argv) > 2 else 450
    z = np.load(os.path.join(AI, f'{name}.npz'))
    V, F = z['V'].astype(float), z['F']
    ch = Character('HEAT')
    pose = a_pose(angle)
    r0 = ch.rig_orig
    p0, _ = pose_positions(r0, ch.nodes, pose)
    m0 = ch.faces(r0)
    target = surface_points(p0, r0.tris[m0])
    W, yaw, d0, d1 = fit(V, target)
    h = target[:, 1].max() - target[:, 1].min()
    print(f'{name}: yaw {yaw:.0f}, Chamfer {d0 / h * 100:.2f} -> {d1 / h * 100:.2f} % da altura')
    np.savez(os.path.join(AI, f'{name}_encaixe.npz'), V=W, F=F)
    layers = ps1_layers(ch.rig, ch.tims)
    hd = hd_layers(layers, os.path.join(OUT, 'heat', 'textures'))
    rd = Renderer(layers, hd)
    n = vertex_normals(W, F)
    grey = np.full((len(F), 3, 3), 200, np.float32)
    d_ai = pack(W[F], n[F], grey, grey * 0 + 128, np.zeros((len(F), 3, 2), np.float32), np.full(len(F), -1),
                np.zeros(len(F), int))
    pts = target
    imgs = []
    for yaw_v in (25, 90, 205, 300):
        size = (360, 520)
        c, dist = Renderer.fit(pts, yaw_v, 8, size, 0.9)
        vp, eye = Renderer.camera(c, dist, yaw_v, 8, size)
        R = np.linalg.norm(pts.max(0) - pts.min(0)) / 2
        from heat_remake import render_set
        o = render_set(ch, rd, pose, yaw_v, size, pts, styles=('hd', 'modern'), floor_y=pts[:, 1].min())
        ai = rd.render_modern(d_ai, vp, eye, size, c, R, floor_y=pts[:, 1].min())
        imgs += [label(o['hd'], 'ORIGINAL'), label(ai, 'IA (forma)'), label(o['modern'], 'REMAKE')]
    out = side_by_side(imgs)
    fn = os.path.join(AI, f'{name}_compara.png')
    out.save(fn)
    print(fn)


if __name__ == '__main__':
    main()
