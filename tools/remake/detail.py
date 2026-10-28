"""Detalhe de superfície do remake (fase 2): mapas de normal gerados das texturas HD.

Mesma conta de src/remake/rmk_asset.c (rmk_asset_build_normals), para os renders offline baterem com o jogo:
a arte é anime com traço preto. Altura h por texel:
- estampa colorida (saturação alta: chamas, logotipo) em relevo, como tinta grossa ou bordado;
- traço preto (contornos, costuras) como sulco;
- sombreado pintado (as dobras do pano) como ondulação: luminância bem suavizada.
Normal = normalize(-k dh/du, -k dh/dv, 1) por diferenças centrais. Texel transparente: normal plana.
"""
from __future__ import annotations

import numpy as np

STRENGTH = 16.0
PRINT, INK, FOLD = 1.0, 0.8, 1.2
R_PRINT, R_INK, R_FOLD = 3, 2, 8      # raios das caixas (texels HD), cada uma aplicada 2 vezes


NO_NMAP = 4          # bit no modo da face: sem mapa de normal


def mark_flat(rig, head=13):
    """Rosto (faces de cor com desenho por cima no osso da cabeça: olhos, boca, sobrancelhas, marcas) fica
    sem mapa de normal: o estilo é pintado, e o traço entalhado parecia gravado. head: nó da cabeça (13 no HEAT)."""
    m = (rig.fnode == head) & ((rig.mode & 3) == 1)
    rig.mode = rig.mode.copy()
    rig.mode[m] |= NO_NMAP
    return int(m.sum())


def _blur3(h):
    p = np.pad(h, ((0, 0), (1, 1)), mode='edge')
    h = (p[:, :-2] + 2 * p[:, 1:-1] + p[:, 2:]) * 0.25
    p = np.pad(h, ((1, 1), (0, 0)), mode='edge')
    return (p[:-2] + 2 * p[1:-1] + p[2:]) * 0.25


def _box(h, r):
    """Caixa separável de raio r (2r + 1 texels), borda repetida; igual a box() em rmk_asset.c."""
    for axis in (1, 0):
        p = np.pad(h, [(r + 1, r)] * 1 + [(0, 0)] if axis == 0 else [(0, 0), (r + 1, r)], mode='edge')
        c = np.cumsum(p, axis=axis, dtype=np.float64)
        if axis == 1:
            h = (c[:, 2 * r + 1:] - c[:, :-2 * r - 1]) / (2 * r + 1)
        else:
            h = (c[2 * r + 1:] - c[:-2 * r - 1]) / (2 * r + 1)
    return h.astype(np.float32)


def _box2(h, r):
    return _box(_box(h, r), r)


def normal_map(rgba: np.ndarray, strength: float = STRENGTH) -> np.ndarray:
    """(S, S, 4) uint8 RGBA -> (S, S, 4) uint8: xyz * 0,5 + 0,5 no espaço tangente (u, v, normal), alfa igual."""
    c = rgba[..., :3].astype(np.float32)
    on = rgba[..., 3] > 0
    Y = (0.299 * c[..., 0] + 0.587 * c[..., 1] + 0.114 * c[..., 2]) / 255.0
    sat = (c.max(-1) - c.min(-1)) / 255.0
    ink = np.clip((0.30 - Y) / 0.22, 0, 1)
    paint = np.clip((sat - 0.25) / 0.2, 0, 1)
    z = lambda a: np.where(on, a, 0.0).astype(np.float32)
    h = PRINT * _box2(z(paint), R_PRINT) - INK * _box2(z(ink), R_INK) + FOLD * _box2(z(Y), R_FOLD)
    h = np.where(on, h, 0.0)
    p = np.pad(h, 1, mode='edge')
    du = p[1:-1, 2:] - p[1:-1, :-2]
    dv = p[2:, 1:-1] - p[:-2, 1:-1]
    n = np.stack([-strength * du, -strength * dv, np.ones_like(h)], -1)
    n /= np.linalg.norm(n, axis=-1, keepdims=True)
    out = np.empty(rgba.shape, np.uint8)
    out[..., :3] = (n * 127.5 + 127.5).astype(np.uint8)
    out[..., 3] = rgba[..., 3]
    return out
