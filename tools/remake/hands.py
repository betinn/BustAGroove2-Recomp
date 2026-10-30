"""Mãos com dedos modeladas por código para o remake (docs/remake-plano.md, fase 2).

O jogo não tem ossos de dedo: cada mão é um mesh rígido preso ao osso da mão, e o jogo troca o mesh inteiro
em alguns gestos (o antebraço passa a apontar para outro nó filho: nó 15 -> 16, 20 ou 21 na esquerda e
nó 18 -> 19, 22 ou 23 na direita; ver docs/remake-visual.md). Aqui cada gesto vira uma mão nova:
1. esqueleto de mão paramétrico (palma, 4 dedos de 3 falanges, polegar) com ângulos por gesto, no espaço do
   osso da mão do jogo (dedos para -y, costas da mão para +x, polegar da mão esquerda para +z);
2. superfície implícita (SDF: cones arredondados unidos com mínimo suave), poligonizada por surface nets
   e decimada por QEM (Garland & Heckbert) até um orçamento de faces;
3. textura: a mesma página da luva original (camada do asset). O UV sai de uma projeção plana (y, z) -> (u, v)
   ajustada por mínimos quadrados nas costas da luva original, calculada na pose "aberta" de cada ponto
   (os dedos dobrados levam o UV do dedo esticado), então a linha branca de cada dedo fica no dedo; a palma
   e o polegar ficam com a cor vermelha da luva (sem textura);
4. o punho cinza (as 22 faces de cor perto do pulso, com os 6 vértices compartilhados do antebraço) continua o
   original de cada gesto.
A mão direita é o espelho em z da esquerda (como no jogo).
"""
from __future__ import annotations

import heapq
from dataclasses import dataclass, field

import numpy as np

LEFT_NODES = (16, 20, 21)    # mão esquerda: padrão (aberta), punho fechado, apontando
RIGHT_NODES = (19, 22, 23)   # espelhos em z
GESTURES = ('aberta', 'punho', 'aponta')


# ---------------------------------------------------------------- SDF
def sd_round_cone(p, a, b, r1, r2):
    """Cone arredondado de a (raio r1) a b (raio r2) (Inigo Quilez). p: (N,3)."""
    ba = b - a
    l2 = ba @ ba
    rr = r1 - r2
    a2 = l2 - rr * rr
    il2 = 1.0 / l2
    pa = p - a
    y = pa @ ba
    z = y - l2
    xv = pa * l2 - np.outer(y, ba)
    x2 = np.einsum('ij,ij->i', xv, xv)
    y2 = y * y * l2
    z2 = z * z * l2
    k = np.sign(rr) * rr * rr * x2
    out = (np.sqrt(x2 * a2 * il2) + y * rr) * il2 - r1
    m1 = np.sign(z) * a2 * z2 > k
    out = np.where(m1, np.sqrt(x2 + z2) * il2 - r2, out)
    m2 = np.sign(y) * a2 * y2 < k
    out = np.where(m2, np.sqrt(x2 + y2) * il2 - r1, out)
    return out


def sd_round_box(p, c, h, r, R=np.eye(3)):
    q = np.abs((p - c) @ R) - (h - r)
    return np.linalg.norm(np.maximum(q, 0), axis=1) + np.minimum(q.max(1), 0) - r


def smin(a, b, k):
    h = np.clip(0.5 + 0.5 * (b - a) / k, 0, 1)
    return b + (a - b) * h - k * h * (1 - h)


# ---------------------------------------------------------------- esqueleto da mão
def rot(axis, deg):
    a = np.radians(deg)
    x, y, z = np.asarray(axis, float) / np.linalg.norm(axis)
    c, s, C = np.cos(a), np.sin(a), 1 - np.cos(a)
    return np.array([[c + x * x * C, x * y * C - z * s, x * z * C + y * s],
                     [y * x * C + z * s, c + y * y * C, y * z * C - x * s],
                     [z * x * C - y * s, z * y * C + x * s, c + z * z * C]])


@dataclass
class Finger:
    base: tuple                 # junta da base (nó do dedo) em espaço da mão
    lengths: tuple              # falanges proximal, média, distal
    radius: float               # raio na base (afina até ~80 % na ponta)
    splay: float = 0.0          # abertura (graus, em torno de x: para +z)


# Mão esquerda, espaço do osso da mão do jogo (unidades do modelo). Medidas tiradas do mesh original do nó 16:
# punho cinza até y ~ -560, nós dos dedos em y ~ -2900, ponta do médio em y = -6366, largura z -1611..1488
# (polegar até z 2229), espessura x -1085..1200 (costas em +x).
FINGERS = {
    'indicador': Finger((340, -2820, 1060), (1250, 830, 700), 390, 4),
    'medio': Finger((360, -2960, 330), (1330, 900, 760), 405, 1),
    'anelar': Finger((350, -2900, -400), (1240, 850, 720), 385, -2),
    'minimo': Finger((300, -2720, -1110), (1000, 680, 600), 335, -6),
}
THUMB = Finger((-150, -1150, 1150), (1000, 820, 660), 450, 0)
PALM = dict(c=(230, -1760, -30), h=(640, 1300, 1480), r=520)
GLOVE_TOP = -380               # a luva começa dentro do punho cinza


@dataclass
class Gesture:
    curl: dict                  # dedo -> (graus nas 3 juntas, flexão para a palma = -x)
    thumb: tuple                # (direção do metacarpo (x, y, z), flexão das 2 juntas, torção)
    spread: float = 1.0         # fator na abertura


GESTURE = {
    'aberta': Gesture({'indicador': (6, 6, 4), 'medio': (5, 6, 4), 'anelar': (6, 7, 5), 'minimo': (8, 8, 6)},
                      ((-0.34, -0.80, 0.50), (10, 12), 0), 1.0),
    'punho': Gesture({'indicador': (88, 98, 62), 'medio': (92, 100, 62), 'anelar': (92, 100, 62),
                      'minimo': (88, 95, 60)}, ((-0.45, -0.82, 0.35), (48, 40), 0), 0.3),
    'aponta': Gesture({'indicador': (2, 3, 2), 'medio': (90, 100, 62), 'anelar': (92, 100, 62),
                       'minimo': (88, 95, 60)}, ((-0.26, -0.62, 0.74), (8, 8), 0), 0.6),
}


@dataclass
class Bone:
    a: np.ndarray               # posição (pose do gesto)
    b: np.ndarray
    ra: float
    rb: float
    R: np.ndarray               # rotação pose -> repouso (para o UV)
    a0: np.ndarray              # posição da junta a na pose aberta
    kind: str                   # 'dedo', 'polegar'
    root: bool                  # nasce na palma


def chain(base, lengths, r0, d0, axis, angles, kind, rest=None):
    """Falanges encadeadas. d0 = direção da 1ª falange; a junta i dobra angles[i] graus em torno de `axis`.
    rest = (base, d0, axis, angles) da pose aberta, para levar cada ponto de volta (UV)."""
    rb, rd, rax, rang = rest if rest is not None else (base, d0, axis, angles)
    out = []
    p, d = np.asarray(base, float), np.asarray(d0, float) / np.linalg.norm(d0)
    p0, d_r = np.asarray(rb, float), np.asarray(rd, float) / np.linalg.norm(rd)
    r = r0
    Racc, Racc_r = np.eye(3), np.eye(3)
    for i, L in enumerate(lengths):
        Racc = rot(axis, angles[i]) @ Racc
        Racc_r = rot(rax, rang[i]) @ Racc_r
        dd, dd_r = Racc @ d, Racc_r @ d_r
        r2 = r * (0.93 if i < len(lengths) - 1 else 0.86)
        q = p + dd * L
        # rotação que leva a falange posada para a de repouso (pontos colados ao osso andam junto)
        R = (Racc_r @ _frame(d_r)) @ np.linalg.inv(Racc @ _frame(d))
        out.append(Bone(p, q, r, r2, R, p0, kind, i == 0))
        p, p0, r = q, p0 + dd_r * L, r2
    return out


def _frame(d):
    """Base ortonormal com o 1º eixo em d (o giro em torno de d não importa: falange redonda)."""
    d = d / np.linalg.norm(d)
    u = np.cross(d, [1.0, 0, 0]) if abs(d[0]) < 0.9 else np.cross(d, [0, 1.0, 0])
    u /= np.linalg.norm(u)
    return np.c_[d, u, np.cross(d, u)]


def hand_bones(gesture: Gesture):
    bones = []
    flex = np.array([0.0, 0.0, 1.0])    # girar em torno de +z leva -y para -x (palma) na mão esquerda
    rest_g = GESTURE['aberta']
    for name, f in FINGERS.items():
        d0 = rot((1, 0, 0), f.splay * gesture.spread) @ np.array([0.0, -1.0, 0.0])
        dr = rot((1, 0, 0), f.splay * rest_g.spread) @ np.array([0.0, -1.0, 0.0])
        bones += chain(f.base, f.lengths, f.radius, d0, flex, [-a for a in gesture.curl[name]], 'dedo',
                       (f.base, dr, flex, [-a for a in rest_g.curl[name]]))
    tb = THUMB
    td, tflex, _ = gesture.thumb
    rd, rflex, _ = rest_g.thumb
    # o polegar dobra em torno de um eixo perpendicular à sua direção, para o lado da palma
    ax = np.cross(np.asarray(td, float), [1.0, 0, 0]); rax = np.cross(np.asarray(rd, float), [1.0, 0, 0])
    meta = chain(tb.base, tb.lengths[:1], tb.radius, td, ax, [0], 'polegar', (tb.base, rd, rax, [0]))
    tip = chain(meta[-1].b, tb.lengths[1:], tb.radius * 0.92, td, ax, [-tflex[0], -tflex[1]], 'polegar',
                (np.asarray(tb.base, float) + np.asarray(rd) / np.linalg.norm(rd) * tb.lengths[0], rd, rax,
                 [-rflex[0], -rflex[1]]))
    for b in tip:
        b.root = False
    return bones + meta + tip


def hand_sdf(gesture: Gesture):
    bones = hand_bones(gesture)
    pc = PALM

    def f(p):
        d = sd_round_box(p, np.asarray(pc['c'], float), np.asarray(pc['h'], float), pc['r'])
        # corta a luva acima do punho (o punho cinza original cobre a junção)
        d = np.maximum(d, p[:, 1] - GLOVE_TOP)
        for b in bones:
            # falange que nasce na palma: mínimo suave largo; as outras só emendam na vizinha do mesmo dedo
            k = (330 if b.kind == 'polegar' else 260) if b.root else 90
            d = smin(d, sd_round_cone(p, b.a, b.b, b.ra, b.rb), k)
        return d
    return f, bones


# ---------------------------------------------------------------- surface nets
def surface_nets(f, lo, hi, cell):
    lo, hi = np.asarray(lo, float), np.asarray(hi, float)
    n = np.ceil((hi - lo) / cell).astype(int) + 1
    g = np.stack(np.meshgrid(*[lo[i] + np.arange(n[i]) * cell for i in range(3)], indexing='ij'), -1)
    v = f(g.reshape(-1, 3)).reshape(n)
    inside = v < 0
    # células com troca de sinal
    c = np.zeros(n - 1, bool)
    corners = [(i, j, k) for i in (0, 1) for j in (0, 1) for k in (0, 1)]
    any_in = np.zeros(n - 1, bool); all_in = np.ones(n - 1, bool)
    for i, j, k in corners:
        s = inside[i:n[0] - 1 + i, j:n[1] - 1 + j, k:n[2] - 1 + k]
        any_in |= s; all_in &= s
    c = any_in & ~all_in
    idx = -np.ones(n - 1, int)
    cells = np.argwhere(c)
    idx[tuple(cells.T)] = np.arange(len(cells))
    # vértice = média dos cruzamentos nas 12 arestas da célula
    acc = np.zeros((len(cells), 3)); cnt = np.zeros(len(cells))
    edges = [((0, 0, 0), (1, 0, 0)), ((0, 1, 0), (1, 1, 0)), ((0, 0, 1), (1, 0, 1)), ((0, 1, 1), (1, 1, 1)),
             ((0, 0, 0), (0, 1, 0)), ((1, 0, 0), (1, 1, 0)), ((0, 0, 1), (0, 1, 1)), ((1, 0, 1), (1, 1, 1)),
             ((0, 0, 0), (0, 0, 1)), ((1, 0, 0), (1, 0, 1)), ((0, 1, 0), (0, 1, 1)), ((1, 1, 0), (1, 1, 1))]
    ci = cells
    for a, b in edges:
        va = v[ci[:, 0] + a[0], ci[:, 1] + a[1], ci[:, 2] + a[2]]
        vb = v[ci[:, 0] + b[0], ci[:, 1] + b[1], ci[:, 2] + b[2]]
        m = (va < 0) != (vb < 0)
        t = va[m] / (va[m] - vb[m])
        pa = lo + (ci[m] + np.array(a)) * cell
        pb = lo + (ci[m] + np.array(b)) * cell
        acc[m] += pa + (pb - pa) * t[:, None]
        cnt[m] += 1
    V = acc / cnt[:, None]
    # quads: uma por aresta da grade com troca de sinal, ligando as 4 células em volta
    F = []
    for ax in range(3):
        o1, o2 = [(1, 2), (0, 2), (0, 1)][ax]
        # arestas ao longo de ax entre os pontos p e p + e_ax; precisam de 4 células vizinhas
        s0 = [slice(1, n[i] - 1) for i in range(3)]
        s1 = list(s0)
        s0[ax] = slice(0, n[ax] - 1); s1[ax] = slice(1, n[ax])
        A = inside[tuple(s0)]; B = inside[tuple(s1)]
        cross = np.argwhere(A != B)
        base = cross.copy()
        for k in (o1, o2):
            base[:, k] += 1      # índice do ponto na grade completa
        q = []
        for d1, d2 in ((0, 0), (1, 0), (1, 1), (0, 1)):
            cc = base.copy()
            cc[:, o1] -= 1 - d1; cc[:, o2] -= 1 - d2
            q.append(idx[tuple(cc.T)])
        F.append(np.stack(q, 1))
    Q = np.concatenate(F)
    Q = Q[(Q >= 0).all(1)]
    T = np.concatenate([Q[:, [0, 1, 2]], Q[:, [0, 2, 3]]])
    return V, T


def project(f, V, steps=4, h=8.0):
    """Leva os vértices para a superfície (passos de Newton pelo gradiente numérico)."""
    for _ in range(steps):
        d = f(V)
        g = np.stack([(f(V + np.eye(3)[i] * h) - f(V - np.eye(3)[i] * h)) / (2 * h) for i in range(3)], 1)
        g /= np.maximum(np.linalg.norm(g, axis=1, keepdims=True), 1e-9)
        V = V - g * d[:, None]
    return V


def fix_orientation(f, V, T, h=8.0):
    """Normais para fora: cada face segue o gradiente da SDF no seu centro."""
    c = V[T].mean(1)
    g = np.stack([f(c + np.eye(3)[i] * h) - f(c - np.eye(3)[i] * h) for i in range(3)], 1)
    n = np.cross(V[T[:, 1]] - V[T[:, 0]], V[T[:, 2]] - V[T[:, 0]])
    flip = np.einsum('ij,ij->i', n, g) < 0
    T = T.copy()
    T[flip] = T[flip][:, ::-1]
    return T


# ---------------------------------------------------------------- decimação (QEM)
def decimate(V, T, target):
    V = V.astype(float).copy()
    T = T.copy()
    nv = len(V)
    alive = np.ones(len(T), bool)
    def plane(t):
        a, b, c = V[T[t]]
        n = np.cross(b - a, c - a); l = np.linalg.norm(n)
        if l < 1e-12:
            return np.zeros((4, 4))
        n /= l
        p = np.r_[n, -n @ a]
        return np.outer(p, p) * l   # ponderado pela área
    Qv = np.zeros((nv, 4, 4))
    for t in range(len(T)):
        q = plane(t)
        for k in T[t]:
            Qv[k] += q
    vf = [set() for _ in range(nv)]
    for t, tri in enumerate(T):
        for k in tri:
            vf[k].add(t)
    parent = np.arange(nv)
    ver = np.zeros(nv, int)

    def cost(i, j):
        Q = Qv[i] + Qv[j]
        A = Q.copy(); A[3] = (0, 0, 0, 1)
        try:
            x = np.linalg.solve(A, [0, 0, 0, 1])
            if np.linalg.norm(x[:3] - (V[i] + V[j]) / 2) > np.linalg.norm(V[i] - V[j]) * 2:
                raise np.linalg.LinAlgError
        except np.linalg.LinAlgError:
            cands = [V[i], V[j], (V[i] + V[j]) / 2]
            x = min((np.r_[c, 1] for c in cands), key=lambda h: h @ Q @ h)
        return float(x @ Q @ x), x[:3]

    heap = []
    edges = set()
    for tri in T:
        for k in range(3):
            a, b = sorted((tri[k], tri[(k + 1) % 3]))
            edges.add((a, b))
    for a, b in edges:
        c, x = cost(a, b)
        heapq.heappush(heap, (c, a, b, ver[a], ver[b]))
    nalive = len(T)
    while nalive > target and heap:
        c, a, b, va, vb = heapq.heappop(heap)
        if parent[a] != a or parent[b] != b or ver[a] != va or ver[b] != vb:
            continue
        _, x = cost(a, b)
        shared = vf[a] & vf[b]
        # condição de enlace: vizinhos comuns = exatamente os 2 das faces da aresta
        na = {k for t in vf[a] for k in T[t]} - {a}
        nb = {k for t in vf[b] for k in T[t]} - {b}
        if len(na & nb) != len(shared) or len(shared) != 2:
            continue
        # nenhuma face vira (normal muda de sentido) nem degenera
        bad = False
        for t in (vf[a] | vf[b]) - shared:
            tri = [x if k in (a, b) else V[k] for k in T[t]]
            old = V[T[t]]
            n0 = np.cross(old[1] - old[0], old[2] - old[0])
            n1 = np.cross(tri[1] - tri[0], tri[2] - tri[0])
            if n0 @ n1 <= 0.2 * np.linalg.norm(n0) * np.linalg.norm(n1):
                bad = True; break
        if bad:
            continue
        V[a] = x
        Qv[a] += Qv[b]
        for t in shared:
            alive[t] = False; nalive -= 1
            for k in T[t]:
                vf[k].discard(t)
        for t in vf[b]:
            T[t] = [a if k == b else k for k in T[t]]
            vf[a].add(t)
        vf[b] = set()
        parent[b] = a
        ver[a] += 1
        for k in {k for t in vf[a] for k in T[t]} - {a}:
            c2, _ = cost(*sorted((a, k)))
            i, j = sorted((a, k))
            heapq.heappush(heap, (c2, i, j, ver[i], ver[j]))
    T = T[alive]
    used = np.unique(T)
    remap = -np.ones(nv, int); remap[used] = np.arange(len(used))
    return V[used], remap[T]


def smooth(V, T, f, iters=3, lam=0.5):
    """Laplaciano leve + reprojeção na superfície (regulariza os triângulos do surface nets)."""
    e = np.concatenate([T[:, [0, 1]], T[:, [1, 2]], T[:, [2, 0]]])
    e = np.concatenate([e, e[:, ::-1]])
    deg = np.bincount(e[:, 0], minlength=len(V)).astype(float)
    for _ in range(iters):
        nb = np.zeros_like(V)
        for k in range(3):
            nb[:, k] = np.bincount(e[:, 0], weights=V[e[:, 1], k], minlength=len(V))
        V = V + lam * (nb / np.maximum(deg, 1)[:, None] - V)
        V = project(f, V, 2)
    return V


# ---------------------------------------------------------------- material e UV
def fit_uv_map(p_local, uv, back_mask):
    """Projeção plana (y, z, 1) -> (u, v) por mínimos quadrados nos cantos das costas da luva original."""
    A = np.c_[p_local[back_mask][:, 1], p_local[back_mask][:, 2], np.ones(back_mask.sum())]
    M, *_ = np.linalg.lstsq(A, uv[back_mask], rcond=None)
    res = np.abs(A @ M - uv[back_mask]).mean(0)
    return M, res


def rest_coords(V, bones):
    """Cada ponto vai para a pose aberta pelo osso mais próximo (rígido por falange)."""
    best = np.full(len(V), np.inf); out = V.copy(); kind = np.full(len(V), 'palma', object)
    for b in bones:
        ab = b.b - b.a
        t = np.clip(((V - b.a) @ ab) / (ab @ ab), 0, 1)
        d = np.linalg.norm(V - (b.a + np.outer(t, ab)), axis=1) - (b.ra + (b.rb - b.ra) * t)
        m = d < best
        best[m] = d[m]
        out[m] = (V[m] - b.a) @ b.R.T + b.a0
        kind[m] = b.kind
    # pontos da palma (longe de qualquer dedo) ficam onde estão
    palm = best > 150
    out[palm] = V[palm]
    kind[palm] = 'palma'
    return out, kind


def hand_geometry(gesture: str, cell=95.0, target=700, cache_dir=None):
    """Malha da mão esquerda num gesto: (V, T, R, kind). R = posição de cada vértice na pose aberta (UV),
    kind = 'dedo'/'polegar'/'palma'. Guardada em cache (npz) pela assinatura dos parâmetros."""
    import hashlib
    import os
    import pickle
    g = GESTURE[gesture]
    key = hashlib.sha1(pickle.dumps((g, FINGERS, THUMB, PALM, GLOVE_TOP, cell, target, GESTURE['aberta']))).hexdigest()[:12]
    path = os.path.join(cache_dir, f'mao_{gesture}_{key}.npz') if cache_dir else None
    if path and os.path.exists(path):
        z = np.load(path, allow_pickle=True)
        return z['V'], z['T'], z['R'], z['kind'], int(z['n_raw'])
    f, bones = hand_sdf(g)
    pts = np.concatenate([[b.a, b.b] for b in bones])
    lo = np.minimum(pts.min(0), [-1300, -6800, -2000]) - 700
    hi = np.maximum(pts.max(0), [1400, GLOVE_TOP, 1800]) + 700
    V, T = surface_nets(f, lo, hi, cell)
    V = project(f, V)
    T = fix_orientation(f, V, T)
    V = smooth(V, T, f, 2)
    n_raw = len(T)
    V, T = decimate(V, T, target)
    V = project(f, V, 3)
    T = fix_orientation(f, V, T)
    R, kind = rest_coords(V, bones)
    if path:
        os.makedirs(cache_dir, exist_ok=True)
        np.savez(path, V=V, T=T, R=R, kind=kind, n_raw=n_raw)
    return V, T, R, kind, n_raw


@dataclass
class HandMesh:
    node: int                   # nó do jogo com o mesh original desse gesto (16, 20, 21, 19, 22, 23)
    side: int                   # 0 esquerda (osso 16), 1 direita (osso 19)
    gesture: str
    V: np.ndarray               # (n, 3) espaço do osso da mão
    T: np.ndarray               # (m, 3)
    mode: np.ndarray            # (m,) 0 cor, 2 só textura
    uv: np.ndarray              # (m, 3, 2)
    base: np.ndarray            # (m, 3, 3)
    over: np.ndarray            # (m, 3, 3)
    n_raw: int = 0


def build_hand(gesture: str, side: int, uv_map, red, cache_dir=None, **kw) -> HandMesh:
    """Mão de um gesto e lado. uv_map: GloveUV do nó padrão do mesmo lado; red: cor da luva sem textura.
    A direita é o espelho em z da esquerda; o UV vem do mapeamento da própria luva direita original."""
    V, T, R, kind, n_raw = hand_geometry(gesture, cache_dir=cache_dir, **kw)
    if side:
        V = V * np.array([1, 1, -1]); R = R * np.array([1, 1, -1]); T = T[:, ::-1].copy()
    uv_v = uv_map(R[:, 1:3])
    # face nas costas (normal da pose aberta com x > ~0) e fora do polegar: textura; senão vermelho liso
    a, b, c = R[T[:, 0]], R[T[:, 1]], R[T[:, 2]]
    nrm = np.cross(b - a, c - a)
    nrm /= np.maximum(np.linalg.norm(nrm, axis=1, keepdims=True), 1e-9)
    thumb = (kind[T] == 'polegar').any(1)
    back = (nrm[:, 0] > -0.15) & ~thumb
    mode = np.where(back, 2, 0)
    base = np.broadcast_to(np.asarray(red, np.float32), (len(T), 3, 3)).copy()
    over = np.full((len(T), 3, 3), 128, np.float32)
    nodes = RIGHT_NODES if side else LEFT_NODES
    return HandMesh(nodes[GESTURES.index(gesture)], side, gesture, V, T, mode, uv_v[T], base, over, n_raw)


def original_hand(rig, nodes, h):
    """Faces do nó h (mão original) com os cantos no espaço do osso da mão. Os vértices do pulso pertencem ao
    antebraço (nó 15/18): convertidos pela pose de descanso, como o jogo os vê com a mão nesse osso."""
    from rig import pose_positions
    hand_bone = 16 if h in LEFT_NODES else 19
    _, mats = pose_positions(rig, nodes)
    f = np.nonzero(rig.fnode == h)[0]
    loc = np.zeros((len(f), 3, 3))
    for k in range(3):
        v = rig.tris[f, k]
        own = rig.owner[v]
        mine = own == h
        loc[mine, k] = rig.local[v[mine]]
        for i in np.nonzero(~mine)[0]:
            M = np.linalg.inv(mats[hand_bone]) @ mats[own[i]]
            loc[i, k] = M[:3, :3] @ rig.local[v[i]] + M[:3, 3]
    return f, loc


class GloveUV:
    """UV das costas da luva original do nó h como função de (y, z): as faces texturizadas das costas
    (|normal x| > 0,3; dentro da projeção vale a de maior x) projetadas no plano y-z, interpoladas por baricêntricas (linear por partes, igual ao
    mapeamento do original). Fora da projeção vale o triângulo mais próximo, com baricêntricas presas."""

    def __init__(self, rig, nodes, h=16):
        f, loc = original_hand(rig, nodes, h)
        tex = rig.mode[f] == 2
        n = np.cross(loc[:, 1] - loc[:, 0], loc[:, 2] - loc[:, 0])
        n /= np.maximum(np.linalg.norm(n, axis=1, keepdims=True), 1e-9)
        back = tex & (np.abs(n[:, 0]) > 0.3)   # a ordem dos cantos do arquivo não diz o lado; a altura em x diz
        self.tri = loc[back][:, :, 1:3]          # (K, 3, 2) em (y, z)
        self.depth = loc[back][:, :, 0].mean(1)
        self.uv = rig.uv[f][back]
        self.layer = int(rig.layer[f][tex][0])

    def __call__(self, yz):
        a, b, c = self.tri[:, 0], self.tri[:, 1], self.tri[:, 2]
        v0, v1 = b - a, c - a
        d00 = (v0 * v0).sum(1); d01 = (v0 * v1).sum(1); d11 = (v1 * v1).sum(1)
        den = d00 * d11 - d01 * d01
        w = yz[:, None, :] - a[None]
        d20 = (w * v0[None]).sum(2); d21 = (w * v1[None]).sum(2)
        l1 = (d11 * d20 - d01 * d21) / den; l2 = (d00 * d21 - d01 * d20) / den
        l0 = 1 - l1 - l2
        L = np.stack([l0, l1, l2], 2)                       # (N, K, 3)
        inside = (L > -1e-3).all(2)
        Lc = np.clip(L, 0, 1); Lc /= Lc.sum(2, keepdims=True)
        q = np.einsum('nkj,kjd->nkd', Lc, self.tri)
        dist = np.linalg.norm(q - yz[:, None], axis=2)
        score = np.where(inside, -1e6 - self.depth[None], dist)   # dentro: a face mais de costas
        k = score.argmin(1)
        Lk = Lc[np.arange(len(yz)), k]
        return np.einsum('nj,njd->nd', Lk, self.uv[k])


def attach_hands(rig, nodes, hd_layers, cache_dir=None, only_original=False, **kw):
    """Troca as luvas originais (nós 16, 19 e as alternativas 20..23) pelas mãos novas, uma por gesto.
    Devolve (rig novo, grupos): rig.hand (F,) = 0 corpo, 1..3 esquerda (16, 20, 21), 4..6 direita (19, 22, 23);
    o punho cinza original de cada gesto fica no grupo do gesto. Os vértices das mãos alternativas passam a
    pertencer ao osso 16/19 (é a posição na árvore que o jogo desenha). grupos[g] = (posição na árvore, nó
    do jogo, nº de vértices e índice-base do MeshHeader original, para reconhecer o gesto na RAM).
    only_original=True: as luvas originais de todos os gestos ficam (para o render ORIGINAL), sem mãos novas."""
    import copy
    r = copy.copy(rig)
    groups = {}
    hand_nodes = LEFT_NODES + RIGHT_NODES
    owner = rig.owner.copy()
    for h in hand_nodes:
        owner[owner == h] = 16 if h in LEFT_NODES else 19
    gnowner = rig.gnowner.copy()
    for h in hand_nodes:
        gnowner[gnowner == h] = 16 if h in LEFT_NODES else 19
    group = np.zeros(len(rig.tris), int)
    for gi, h in enumerate(hand_nodes):
        group[rig.fnode == h] = gi + 1
        nd = nodes[h]
        groups[gi + 1] = (16 if h in LEFT_NODES else 19, h, len(nd.mesh.verts), nd.mesh.base_index)
    keep = (group == 0) | (rig.mode == 0) | only_original   # das mãos originais fica só o punho cinza
    sel = np.nonzero(keep)[0]
    fields = ('tris', 'fnode', 'nidx', 'base', 'over', 'uv', 'layer', 'mode', 'order', 'zpair')
    new = {k: [getattr(rig, k)[sel]] for k in fields}
    new_group = [group[sel]]
    local = [rig.local]
    own = [owner]
    nv = len(rig.owner)
    for side, hn in (() if only_original else ((0, LEFT_NODES), (1, RIGHT_NODES))):
        uvf = GloveUV(rig, nodes, hn[0])
        img = hd_layers[uvf.layer]
        m = (img[..., 0] > 150) & (img[..., 1] < 60) & (img[..., 3] > 0)
        red = np.median(img[m][:, :3], 0)
        for gest, h in zip(GESTURES, hn):
            hm = build_hand(gest, side, uvf, red, cache_dir, **kw)
            bone = 16 if side == 0 else 19
            # enrolamento do jogo: no arquivo, a normal pela regra da mão direita aponta para DENTRO (é o
            # NCLIP > 0 com o y da tela para baixo); a malha nova sai com a normal para fora
            T = hm.T[:, ::-1] + nv
            hm.uv, hm.base, hm.over = hm.uv[:, ::-1], hm.base[:, ::-1], hm.over[:, ::-1]
            nv += len(hm.V)
            local.append(hm.V); own.append(np.full(len(hm.V), bone))
            F = len(T)
            new['tris'].append(T); new['fnode'].append(np.full(F, bone)); new['nidx'].append(np.full((F, 3), -1))
            new['base'].append(hm.base); new['over'].append(hm.over); new['uv'].append(hm.uv.astype(np.float32))
            new['layer'].append(np.where(hm.mode == 2, uvf.layer, -1)); new['mode'].append(hm.mode)
            # na ordem do jogo, logo depois das faces do osso da mão (o punho)
            new['order'].append(np.stack([np.full(F, bone), np.full(F, 40), np.arange(F)], 1))
            new['zpair'].append(T[:, :2])
            new_group.append(np.full(F, hand_nodes.index(h) + 1))
    for k in fields:
        setattr(r, k, np.concatenate(new[k]))
    r.local = np.concatenate(local)
    r.owner = np.concatenate(own)
    r.gnowner = gnowner
    r.hand = np.concatenate(new_group)
    return r, groups


def faces_for(rig, left=1, right=4):
    """Máscara das faces desenhadas com os gestos dados (grupo da esquerda 1..3, da direita 4..6)."""
    return np.isin(rig.hand, (0, left, right))
