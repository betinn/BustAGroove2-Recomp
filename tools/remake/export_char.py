#!/usr/bin/env python3
"""Exporta um personagem para o plugin do jogo (src/bag2_heat.c + núcleo src/remake/), fase 4 do remake.

Genérico por personagem (o exportador da fase 1–2 era só do HEAT, export_heat.py, que agora chama este):
malha soldada (tools/remake/rig.py), atributos por canto, oclusão ambiente por vértice, camadas HD (Real-ESRGAN
4x) em RGBA8 com o bit STP do PS1 no alfa (143) para a semitransparência, mãos alternativas do jogo como grupos
de faces (o jogo troca o mesh inteiro da mão religando o irmão do antebraço), páginas de textura por nó (o plugin
acha a página de cada TIM na VRAM pelos pacotes do próprio personagem: vale para o jogador 1 e o 2).
Só o HEAT ganha a geometria nova da fase 2 (mãos com dedos, orelhas, nariz: tools/remake/hands.py e ears.py,
feitas para ele); os outros ficam com a malha original suavizada + texturas HD + mapas de normal + luz do palco.

Uso: python tools/remake/export_char.py NOME[*] [saída ...]
     (padrão: build-debug/mods/remake/<nome>.bin e build/...; HEAT continua heat.bin). Com * no fim do nome, o
     figurino alternativo (modelo 195 + 2k, texturas 159 + 2k: o do jogador 2 numa partida espelhada), em
     <nome>_alt.bin; vários personagens têm o mesmo modelo nos dois figurinos e só a textura muda.
O arquivo é derivado do disco do usuário: fica fora do git (pastas build*/ são ignoradas).

Formato v6 (little-endian; lido por src/remake/rmk_asset.c, que lê também o v4/v5):
    char[4] "HTR6"; u32 versão (6), n_nodes, n_verts, n_tris, n_layers, tex_size, n_body, n_groups, n_pages,
                índice do personagem (bag2model.CHARS); char nome[16]
    por nó:     u16 nverts (0 = sem mesh), u8 páginas usadas pelas faces originais do nó (bit p = TIM p), u8 1 = nó
                desenhado (posição na árvore; 0 = mão alternativa ou sem mesh), u32 base_index (MeshHeader+0x98)
    por grupo (1..n_groups): u8 posição na árvore, u8 pad, u16 nverts e u32 base_index do mesh do jogo desse
                grupo, u32 primeiro vértice, u32 nº de vértices exclusivos
    por página: s16 x, y na VRAM (-1 = achar no jogo), u16 largura, altura (texels), u8 bits por texel, u8 pad[3],
                u32 FNV-1a dos 256 primeiros bytes da imagem do TIM (o plugin reconhece o LoadImage dele e carrega o
                asset durante o carregamento da partida)
    por camada: u8 página
    por vértice, por face, por camada: como no v5 (RmkVert, RmkTri, tex_size² RGBA8). Modo da face: bits 0–1
                material (0 cor, 1 cor + textura, 2 só textura), bit 2 sem mapa de normal, bit 4 semitransparente
                (o jogo usa 0,5 B + 0,5 F nos texels com STP; os outros texels da face são opacos; no material 1 o
                decalque se mistura com a cor base da face). Alfa da camada:
                0 transparente, 143 texel com STP, 255 opaco.
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
from rig import GAME_LIST_ORDER, build_rig, pose_positions  # noqa: E402
from heat_remake import DISC, OUT, hd_layers, ps1_layers  # noqa: E402
import detail  # noqa: E402

BAG2 = os.path.join(HERE, '..', 'recomp', 'bag2')
FACE_FIELDS = ('tris', 'fnode', 'nidx', 'base', 'over', 'uv', 'layer', 'mode', 'order', 'zpair', 'hand')
SEMI = 16                      # bit do modo da face: semitransparente (texels com STP)
STP_ALPHA = 143                # alfa do texel com STP (o mesmo valor do pacote HD do framework)

# Mãos alternativas que não se soldam ao antebraço (sem "uses"): posição na árvore observada no jogo
# (tools/remake/char_tree.py, HEAT x COMET no palco do HEAT: 21 e 26 na mão esquerda, 23 na direita) e, para as que
# não apareceram, o lado pelo sinal de z do centro da malha (esquerda +z, como os nós 17 e 20).
# Nó do arquivo -> posição (= nó padrão) que ele substitui.
ALT_POS: dict[str, dict[int, int]] = {'COMET': {21: 17, 22: 17, 26: 17, 23: 20, 24: 20, 25: 20}}


def slug(name: str) -> str:
    alt = name.endswith('*')
    base = name.rstrip('*')
    return ('heat' if base == 'HEAT' else ''.join(c for c in base.lower() if c.isalnum())) + ('_alt' if alt else '')


def default_outs(name: str) -> list[str]:
    return [os.path.join(BAG2, d, 'mods', 'remake', f'{slug(name)}.bin') for d in ('build-debug', 'build')]


def drawn_and_alts(name, nodes):
    """Nós desenhados (com mesh e ligados ao nó 0 pela cadeia de pais: posição na árvore = índice no arquivo) e
    as mãos alternativas (mesh fora da árvore) com a posição que cada uma ocupa quando o jogo a liga: a do nó
    desenhado sem filhos que se solda ao mesmo osso (mesmo conjunto de donos dos slots em "uses")."""
    def reaches_root(i):
        while i >= 0:
            if i == 0:
                return True
            i = nodes[i].parent
        return False
    drawn = [nd.index for nd in nodes if nd.mesh and reaches_root(nd.index)]
    assert drawn == list(range(len(drawn))), f'{name}: nós desenhados fora de ordem: {drawn}'
    alts = [nd.index for nd in nodes if nd.mesh and nd.index not in drawn]
    slot_owner = {}
    for nd in nodes:
        if nd.mesh:
            for _, dst in nd.mesh.refs:
                slot_owner.setdefault(dst, nd.index)
    src = {i: frozenset(slot_owner.get(s, -1) for s, _ in nodes[i].mesh.uses) for i in drawn + alts}
    parents = {nodes[i].parent for i in drawn}
    leaves = [i for i in drawn if i not in parents]
    pos, unresolved = {}, []
    for j in alts:
        if j in ALT_POS.get(name, {}):
            pos[j] = ALT_POS[name][j]
            continue
        c = [i for i in leaves if src[j] and src[i] == src[j]]
        if len(c) == 1:
            pos[j] = c[0]
        else:
            unresolved.append(j)
    return drawn, alts, pos, unresolved


def original_groups(rig, nodes, pos):
    """Grupos das mãos com as malhas originais: em cada posição com alternativas, o nó padrão e cada alternativa
    viram um grupo; os vértices das alternativas passam a ser do osso da posição. Devolve (rig, grupos)."""
    import copy
    r = copy.copy(rig)
    groups, gid = {}, {}
    for p in sorted(set(pos.values())):
        for n in [p] + sorted(j for j in pos if pos[j] == p):
            gid[n] = len(gid) + 1
            groups[gid[n]] = (p, n, len(nodes[n].mesh.verts), nodes[n].mesh.base_index)
    r.owner, r.gnowner = rig.owner.copy(), rig.gnowner.copy()
    for j, p in pos.items():
        r.owner[r.owner == j] = p
        r.gnowner[r.gnowner == j] = p
    r.hand = np.array([gid.get(int(n), 0) for n in rig.fnode])
    return r, groups


def stp_layers(base_rig, tims, layers):
    """Alfa 143 nos texels cuja cor da CLUT tem o bit STP (semitransparência do PS1 nas primitivas semi)."""
    out = []
    for (tex, clut), img in zip(base_rig.layers, layers):
        tim = tims[tex]
        v = tim.clut[min(clut, len(tim.clut) - 1)][tim.idx]
        stp = ((v >> 15) & 1).astype(bool) & (v != 0)
        o = img.copy()
        h, w = stp.shape
        o[:h, :w, 3][stp] = STP_ALPHA
        out.append(o)
    return out


def face_semi(rig, nodes):
    """Bit SEMI das faces originais de primitivas semitransparentes, de base ou de decalque (overlay). Conferido no
    quadro da GPU (gpu_frame_dump, COMET jogador 2): os 51 pacotes do nó 4 e os 18 decalques da cabeça com a linha 5
    da CLUT saem com o bit semi (0,5 B + 0,5 F). No decalque, o que fica atrás é a cor base da própria face."""
    semi = np.zeros(len(rig.tris), int)
    cache = {}
    for f, (n, lo, pi) in enumerate(rig.order):
        if lo >= len(GAME_LIST_ORDER) or nodes[n].mesh is None:
            continue
        key = (int(n), int(lo))
        if key not in cache:
            cache[key] = [p for p in nodes[n].mesh.prims if p.lst == GAME_LIST_ORDER[lo]]
        lst = cache[key]
        if pi < len(lst) and (lst[pi].overlay or lst[pi]).semi_bits:
            semi[f] = SEMI
    return semi


def tim_hash(tim) -> int:
    """FNV-1a 32 dos 256 primeiros bytes da imagem do TIM como está no arquivo (e na RAM no LoadImage)."""
    idx = tim.idx.astype(np.uint32)
    if tim.bpp == 4:
        raw = (idx[:, 0::2] | idx[:, 1::2] << 4).astype(np.uint8).ravel()
    elif tim.bpp == 8:
        raw = idx.astype(np.uint8).ravel()
    else:
        raw = idx.astype('<u2').view(np.uint8).ravel()
    h = 0x811C9DC5
    for b in raw[:256].tolist():
        h = ((h ^ b) * 0x01000193) & 0xFFFFFFFF
    return h


def canonical_corners(rig):
    """Gira os cantos de cada face (mantendo a orientação) para começar no menor índice de vértice."""
    k = np.argmin(rig.tris, axis=1)
    idx = (k[:, None] + np.arange(3)[None, :]) % 3
    rows = np.arange(len(k))[:, None]
    for name in ('tris', 'uv', 'base', 'over'):
        a = getattr(rig, name)
        setattr(rig, name, a[rows, idx])


def vertex_order(rig, n_groups):
    """Corpo primeiro (vértices de faces do corpo ou de mais de um grupo), depois os exclusivos de cada
    grupo, contíguos. Vértices sem face saem. Devolve (perm, n_body, faixas)."""
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


def export(name: str, outs: list[str]):
    alt = name.endswith('*')
    name = name.rstrip('*')
    k = CHARS.index(name)
    nodes, anims, tims, _ = load_character(DISC, k, alt)
    label = name + ('*' if alt else '')
    drawn, alts, pos, unresolved = drawn_and_alts(label, nodes)
    # sem deduplicar: o modelo tem camadas coplanares (decalques) que só ficam certas na ordem do jogo
    base_rig = build_rig(nodes, set(), two_sided=True, dedup=False)
    layers = ps1_layers(base_rig, tims)
    cache = os.path.join(OUT, slug(name) if name == 'HEAT' and not alt else os.path.join('chars', slug(label)))
    hd = hd_layers(stp_layers(base_rig, tims, layers), os.path.join(cache, 'textures'))
    head = max(drawn, key=lambda i: len(nodes[i].mesh.verts))
    if name == 'HEAT' and not alt:   # mãos com dedos, orelhas e nariz da fase 2 (feitos para o HEAT normal)
        import ears as E
        import hands as H
        rig, groups = H.attach_hands(base_rig, nodes, hd, cache_dir=os.path.join(cache, 'maos'))
        rig = E.attach_nose(E.attach_ears(rig))
        combos = [[1 + i, 1 + len(H.LEFT_NODES) + i] for i in range(len(H.GESTURES))]
    else:
        rig, groups = original_groups(base_rig, nodes, pos)
        if unresolved:   # alternativa sem posição conhecida: fica de fora (o jogo cai no original nesse quadro)
            keep = ~np.isin(base_rig.fnode, unresolved)
            for f in FACE_FIELDS:
                setattr(rig, f, getattr(rig, f)[keep])
        by_pos = {}
        for g, (p, n, _, _) in sorted(groups.items()):
            by_pos.setdefault(p, []).append(g)
        combos = [[gs[min(i, len(gs) - 1)] for gs in by_pos.values()] for i in range(max(map(len, by_pos.values()), default=1))]
    detail.mark_flat(rig, head)
    semi = face_semi(rig, nodes)
    rig.mode = rig.mode | semi
    perm = np.lexsort(rig.order.T[::-1])
    for f in FACE_FIELDS:
        setattr(rig, f, getattr(rig, f)[perm])
    n_groups = max(groups, default=0)
    vperm, n_body, ranges = vertex_order(rig, n_groups)
    remap = -np.ones(len(rig.owner), int)
    remap[vperm] = np.arange(len(vperm))
    rig.owner, rig.local = rig.owner[vperm], rig.local[vperm]
    rig.tris, rig.zpair = remap[rig.tris], remap[rig.zpair]
    assert (rig.tris >= 0).all() and (rig.zpair >= 0).all()
    assert rig.owner.max() < len(drawn), 'vértice com dono fora da árvore desenhada'
    # oclusão ambiente na pose de descanso, por vértice (na GPU, como nos renders offline): o corpo com o primeiro
    # grupo de cada mão; cada combinação de grupos com o corpo
    from render import Renderer
    rd = Renderer(layers, hd)
    p, _ = pose_positions(rig, nodes)
    ao = np.ones(len(p))
    for i, combo in enumerate(combos or [[]]):
        m = np.isin(rig.hand, [0] + combo)
        a = rd.ambient_occlusion(p, rig.tris[m])
        if i == 0:
            vs = np.unique(rig.tris[m])
            ao[vs] = a[vs]
        for g in combo:
            v0, n = ranges[g]
            ao[v0:v0 + n] = a[v0:v0 + n]
    canonical_corners(rig)
    size = hd[0].shape[0]
    assert all(img.shape == (size, size, 4) for img in hd)
    node_pages = np.zeros(len(nodes), int)
    for f in range(len(rig.tris)):
        if rig.layer[f] >= 0 and rig.order[f][1] < len(GAME_LIST_ORDER):
            node_pages[int(rig.order[f][0])] |= 1 << base_rig.layers[int(rig.layer[f])][0]
    import faces
    buf = bytearray(b'HTR6')
    buf += struct.pack('<10I', 6, len(nodes), len(rig.owner), len(rig.tris), len(hd), size, n_body, n_groups, len(tims), k)
    buf += label.encode('ascii')[:15].ljust(16, b'\0')
    for nd in nodes:
        buf += struct.pack('<HBBI', len(nd.mesh.verts) if nd.mesh else 0, int(node_pages[nd.index]) & 0xFF,
                           int(nd.index in drawn), nd.mesh.base_index if nd.mesh else 0)
    for g in range(1, n_groups + 1):
        gp, node, nv, base = groups[g]
        v0, n = ranges[g]
        buf += struct.pack('<BBHIII', gp, 0, nv, base, v0, n)
    for t, tim in enumerate(tims):
        vx, vy = faces.PAGE_VRAM.get(t, (-1, -1)) if name == 'HEAT' and not alt else (-1, -1)
        buf += struct.pack('<hhHHB3xI', vx, vy, tim.w, tim.h, tim.bpp, tim_hash(tim))
    assert len(base_rig.layers) == len(hd)
    buf += bytes(tex for tex, _ in base_rig.layers)
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
    for img in hd:   # alfa do xBR: 0, 143 ou 255; o filtro linear da GPU suaviza a borda
        buf += np.ascontiguousarray(img, np.uint8).tobytes()
    for o in outs:
        os.makedirs(os.path.dirname(o), exist_ok=True)
        with open(o, 'wb') as fh:
            fh.write(buf)
    print(f'{label}: {len(buf) / 1e6:.1f} MB, {len(rig.owner)} vértices ({n_body} do corpo), {len(rig.tris)} faces '
          f'({int(((rig.mode & SEMI) != 0).sum())} semi), {len(hd)} camadas, nós desenhados {len(drawn)}, cabeça {head}, '
          f'grupos ' + ', '.join(f'{g}: pos {groups[g][0]} nó {groups[g][1]} {ranges[g][1]} v' for g in groups) +
          (f'; alternativas sem posição (fora): {unresolved}' if unresolved else '') +
          f'; páginas por nó {[int(x) for x in node_pages[:len(drawn)]]} -> {outs}')


def main():
    name = sys.argv[1].upper().replace('MCLOAD', 'McLOAD') if len(sys.argv) > 1 else 'HEAT'
    export(name, sys.argv[2:] or default_outs(name))


if __name__ == '__main__':
    main()
