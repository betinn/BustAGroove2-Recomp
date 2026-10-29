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
from export_char import SEMI, STP_ALPHA, canonical_corners, stp_layers  # noqa: E402

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
    # camada de cada (tpage, clut) que o jogo usa: posição de cada TIM na VRAM
    keys = []
    for li_, (tex, clut) in enumerate(rig.layers):
        x, y, cx, cy, bpp = tim_vram(tim_raw[tex])
        tp = (x // 64) | ((y // 256) << 4) | ({4: 0, 8: 1, 16: 2}[bpp] << 7)
        cw = (cx // 16) | ((cy + clut) << 6) if cx >= 0 else 0
        n = 16 if bpp == 4 else 256
        pal = tims[tex].clut[clut][:n] if tims[tex].clut is not None and bpp != 16 else None
        keys.append((tp, cw, li_, clut_hash(pal)))
    # e as texturas da geometria do código do overlay (chão, fundo) que o jogo usou e o modelo não
    # (tools/remake/stage_extra_keys.json, de stage_keys.py): página montada da VRAM com os TIMs do palco
    ex_keys, ex_imgs, ex_hash = extra_layers(c, tims, tim_raw, {(a & 0x19F, b) for a, b, _, _ in keys})
    room = 64 - len(hd)              # limite do asset; extra_layers vem das mais usadas para as menos
    if len(ex_imgs) > room:
        print(f'palco {c}: {len(ex_imgs) - room} camadas do código do overlay ficam fora (limite de 64)')
        ex_keys, ex_imgs, ex_hash = ex_keys[:room], ex_imgs[:room], ex_hash[:room]
    if ex_imgs:
        tag = f'{fnv(repr(ex_keys).encode()):08x}'
        hd += hd_layers(ex_imgs, os.path.join(OUT, 'palcos', f'palco{c}', f'textures_extra_{tag}'))
        keys += [(a, b, len(rig.layers) + i, ex_hash[i]) for i, (a, b) in enumerate(ex_keys)]
        layers = layers + ex_imgs
    assert len(hd) <= 64, f'{c}: {len(hd)} camadas'
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
    for img, ps1 in zip(hd, layers):   # alfa suave (borda com antisserrilhado no shader) + STP no bit baixo
        buf += np.ascontiguousarray(smooth_alpha(img, ps1), np.uint8).tobytes()
    # ---- tabelas
    stg = bytearray(b'STG1')
    stg += struct.pack('<6I', 1, c, len(mesh_nodes), len(hd), len(keys), len(model))
    stg += struct.pack('<2I', fnv(ov[:512]), fnv(model[:256]))
    for r in node_rows:
        stg += struct.pack('<4I', *r)
    for w, npk, nf, f0, sl in prims:
        cs = [list(s) for s in sl] + [[0, 0, 0]] * (2 - len(sl))
        stg += struct.pack('<HBBI', w, npk, nf, f0) + bytes(cs[0] + cs[1]) + b'\0\0'
    for tp, cw, li_, h in keys:
        stg += struct.pack('<HHB', tp, cw, li_) + (h & 0xFFFFFF).to_bytes(3, 'little')
    for d in outs_dir:
        os.makedirs(d, exist_ok=True)
        with open(os.path.join(d, f'palco{c}.bin'), 'wb') as fh:
            fh.write(buf)
        with open(os.path.join(d, f'palco{c}.stg'), 'wb') as fh:
            fh.write(stg)
    print(f'palco {c}: {len(buf) / 1e6:.1f} MB, {len(mesh_nodes)} nós, {len(owner)} vértices ({len(rig.owner)} antes dos '
          f'vincos), {len(rig.tris)} faces ({int(((rig.mode & SEMI) != 0).sum())} semi), {len(hd)} camadas '
          f'({len(ex_imgs)} do código do overlay), '
          f'{len(prims)} primitivas, chaves {[(hex(a), hex(b)) for a, b, _, _ in keys]}', flush=True)


EXTRA_KEYS = os.path.join(HERE, 'stage_extra_keys.json')


def clut_hash(pal):
    """24 bits baixos do FNV-1a da CLUT (uint16 little-endian), como o plugin confere um LoadImage na CLUT da chave
    (src/bag2_stage.c, dyn_rect); 0 = desconhecido."""
    if pal is None:
        return 0
    h = fnv(np.asarray(pal, '<u2').tobytes()) & 0xFFFFFF
    return h or 1


def smooth_alpha(hd, ps1):
    """Alfa do palco como campo suave: a máscara de cobertura da camada original (256) ampliada por bicúbica para o
    tamanho HD; a borda fica no meio (128) e o shader a desenha com antisserrilhado, em curva em vez da escada do xBR
    (vigas, grades, folhagem). O STP (semitransparência do PS1) vai no bit baixo do alfa; o carregador do núcleo lê
    assim os assets de palco (índice >= 100). O RGB do Real-ESRGAN já vem dilatado por baixo da borda."""
    from PIL import Image
    S = hd.shape[0]
    cov = Image.fromarray(((ps1[..., 3] > 0) * 255).astype(np.uint8)).resize((S, S), Image.BICUBIC)
    a = np.asarray(cov).astype(np.int32)
    stp = np.asarray(Image.fromarray(((ps1[..., 3] == STP_ALPHA) * 255).astype(np.uint8)).resize((S, S), Image.NEAREST)) > 0
    out = hd.copy()
    out[..., 3] = ((a & 0xFE) | stp).astype(np.uint8)
    return out


def extra_layers(c, tims, tim_raw, have):
    """Camadas das texturas que só a geometria do código do overlay usa: para cada (tpage, clut) de stage_extra_keys.json
    que o modelo não cobre, a página 256 texels de largura da VRAM montada com os TIMs do palco que caem nela e a CLUT
    achada no bloco de CLUT de algum TIM do palco. Quando a página ou a CLUT não estão nos TIMs (montadas pelo overlay em
    tempo de execução), a página e a CLUT lidas da VRAM na partida (tools/remake/stage_vram_keys.py, fase 6b), se não
    forem animadas. Texturas do jogo que aparecem em quase todos os palcos (efeitos) e HUD ficam de fora. Devolve
    (chaves, imagens RGBA 256x256 com o STP no alfa)."""
    import json
    if not os.path.exists(EXTRA_KEYS):
        return [], [], []
    want = json.load(open(EXTRA_KEYS, encoding='utf-8')).get(str(c), [])
    vram_dir = os.path.join(OUT, 'palcos', 'vram')
    common = set()
    if os.path.exists(os.path.join(vram_dir, 'resumo.json')):
        common = {tuple(int(x, 16) for x in k.split('/'))
                  for k in json.load(open(os.path.join(vram_dir, 'resumo.json'), encoding='utf-8'))['comuns']}
    info = [(t,) + tim_vram(r) for t, r in zip(tims, tim_raw)]
    out_k, out_i, out_h = [], [], []
    for tp, cw, _n in sorted(want, key=lambda k: -k[2]):
        tp &= 0x19F
        if (tp, cw) in have or (tp, cw) in out_k:
            continue
        bpp = {0: 4, 1: 8}.get((tp >> 7) & 3)
        if bpp is None:
            continue
        tpx = 16 // bpp
        tx, ty, hw = (tp & 0xF) * 64, ((tp >> 4) & 1) * 256, 256 // tpx
        idx = np.zeros((256, 256), np.int32)
        cov = np.zeros((256, 256), bool)
        for tim, x, y, _cx, _cy, tb in info:
            if tb != bpp:
                continue
            x0, x1 = max(x, tx), min(x + tim.w // tpx, tx + hw)
            y0, y1 = max(y, ty), min(y + tim.h, ty + 256)
            if x0 >= x1 or y0 >= y1:
                continue
            idx[y0 - ty:y1 - ty, (x0 - tx) * tpx:(x1 - tx) * tpx] = tim.idx[y0 - y:y1 - y, (x0 - x) * tpx:(x1 - x) * tpx]
            cov[y0 - ty:y1 - ty, (x0 - tx) * tpx:(x1 - tx) * tpx] = True
        cx, cy = (cw & 0x3F) * 16, (cw >> 6) & 0x1FF
        pal = None
        for tim, _x, _y, tcx, tcy, _tb in info:
            if tim.clut is None or tcx < 0:
                continue
            ch, cn = tim.clut.shape
            if tcy <= cy < tcy + ch and tcx <= cx < tcx + cn:
                n = 16 if bpp == 4 else 256
                pal = np.pad(tim.clut[cy - tcy][cx - tcx:cx - tcx + n], (0, n))[:n]
                break
        if pal is None or not cov.any():
            vf = os.path.join(vram_dir, str(c), f'{tp:03x}_{cw:04x}.npz')
            if (tp, cw) in common or not os.path.exists(vf):
                continue
            z = np.load(vf)
            if bool(z['animated']):
                continue
            raw = z['page'].astype(np.int64)            # halfwords da VRAM: 4 (4 bpp) ou 2 (8 bpp) texels cada
            idx = np.stack([(raw >> (bpp * i)) & ((1 << bpp) - 1) for i in range(tpx)], axis=-1).reshape(256, 256)
            pal = z['clut'].astype(np.int64)
            cov = np.ones((256, 256), bool)
        v = pal[np.clip(idx, 0, len(pal) - 1)].astype(np.int64)
        img = bm.c16_to_rgba(v)
        img[~cov] = 0
        stp = ((v >> 15) & 1).astype(bool) & (v != 0) & cov
        img[..., 3][stp] = STP_ALPHA
        out_k.append((tp, cw))
        out_i.append(img)
        out_h.append(clut_hash(pal[:16 if bpp == 4 else 256]))
    return out_k, out_i, out_h


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
