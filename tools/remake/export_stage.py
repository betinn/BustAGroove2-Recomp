#!/usr/bin/env python3
"""Exporta um palco para o plugin do jogo (src/bag2_stage.c + núcleo src/remake/), fase 6 do remake.

O palco é um modelo no mesmo formato dos personagens (docs/model-format.md), no overlay do palco (containers 231..245
do BUST2.BIN: DBI_ do código, SEDT, TIMs, o modelo e as trilhas de câmera): uma lista de objetos rígidos (nós de tipo 1,
quase todos raízes, posição no mundo). O jogo desenha cada nó por FUN_80029588 (um pacote por primitiva no buffer do nó,
+0xA8, na ordem das listas de GAME_LIST_ORDER; as listas com decalque geram dois pacotes, base e textura); fundo, chão e
peças especiais de alguns palcos saem do código do overlay (não entram aqui). O plugin troca, na OT do quadro, cada
sequência de pacotes dos nós pelo desenho de host do remake, na mesma ordem de pintor (docs/remake-visual.md, "Fase 6").

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
                   das duas faces (lido do pacote vivo: cor, UV), u8 prefixo (1 = DR_TPAGE de 2 palavras antes do
                   pacote: listas PREFIX, semitransparentes sem textura, que ficam com o jogo), u8 pad
      por chave:   u16 tpage (bits 0..4 e 7..8 do pacote), u16 clut (palavra do pacote), u8 camada, u8 hash[3]
                   (a camada HD de cada textura e CLUT que o jogo usa, pela posição de cada TIM na VRAM; hash: 24 bits
                   baixos do FNV-1a da CLUT da camada)
      versão 2:    u32 n_var; por variante de CLUT animada por MoveImage: u16 chave, i16 x, i16 y da linha de origem
                   na VRAM, u8 camada, u8 pad (stage_clut_vars.py)
      versão 3:    u32 n_var; por variante: u16 chave, i16 x, i16 y da origem, u8 camada, u8 tipo (0 = camada própria,
                   1 = afim: a camada base da chave com rgb' = k·rgb + b no shader), i16 k[3] (8.8), i16 b[3] (fração
                   da escala cheia em 1/4096). Fase 6c: quase toda paleta animada é uma transformação afim da paleta
                   base (escurecer, somar vermelho): tipo 1 não custa camada nem memória; tipo 0 só quando o ajuste
                   passa de AFFINE_TOL (unidades de 5 bits; BAG2_AFFINE_TOL) e couber no limite de 64 camadas.
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

BAG2 = os.path.join(HERE, '..', '..', 'game')
STAGES = range(231, 246)
CREASE_DEG = 40.0                  # vinco: faces vizinhas com mais que isso entre as normais não dividem o vértice
TEXTURED = {2, 3, 4, 5, 10, 11, 26, 27}
OVER = {8, 9, 12, 13, 15, 28, 29}  # decalque: pacote base + pacote com textura
# vértice de cada slot do GP0 num quad (A, B, C, D = v2, v3, v1, v0: docs/model-format.md, QUAD_ATTR); slot do vértice
QUAD_SLOT = {0: 3, 1: 2, 2: 0, 3: 1}


def fnv(b: bytes) -> int:
    h = 0x811C9DC5
    for x in b:
        h = ((h ^ x) * 0x01000193) & 0xFFFFFFFF
    return h


# listas semitransparentes sem textura: o jogo (FUN_8001FF34) põe antes do pacote um DR_TPAGE de 2 palavras (tag + E1h,
# com o modo de mistura); sem contar essas 2 palavras o buffer do nó não bate com a tabela e o plugin ignora o nó
# (fase 6c: os 20 GT4 do nó 5 do 241 ficavam "sem par"; 236 e 238 também usam as listas 30/31, 21 e 23)
PREFIX = {18, 19, 20, 21, 22, 23, 30, 31}


def packet_words(li: int) -> tuple[int, int]:
    """(palavras com as tags, nº de pacotes) da primitiva da lista `li` no buffer do nó: F/G conforme GOURAUD, T se tem
    textura; decalque = base sem textura + o mesmo com textura (conferido nas RAMs do inventário, fase 6); as listas
    PREFIX levam o DR_TPAGE de 2 palavras na frente (um pacote só na tabela: o plugin deixa esses com o jogo)."""
    n = 4 if bm.L[li][1] else 3
    g = li in bm.GOURAUD

    def size(tex):
        return 1 + (n if g else 1) + n + (n if tex else 0)
    if li in OVER:
        return size(False) + size(True), 2
    return size(li in TEXTURED) + (2 if li in PREFIX else 0), 1


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
    pal_of = {}                      # chave -> paleta base (a CLUT com que a camada HD foi feita), para o ajuste afim
    for li_, (tex, clut) in enumerate(rig.layers):
        x, y, cx, cy, bpp = tim_vram(tim_raw[tex])
        tp = (x // 64) | ((y // 256) << 4) | ({4: 0, 8: 1, 16: 2}[bpp] << 7)
        cw = (cx // 16) | ((cy + clut) << 6) if cx >= 0 else 0
        n = 16 if bpp == 4 else 256
        pal = tims[tex].clut[clut][:n] if tims[tex].clut is not None and bpp != 16 else None
        if pal is not None:
            pal_of[len(keys)] = np.asarray(pal, np.int64)
        keys.append((tp, cw, li_, clut_hash(pal)))
    # e as texturas da geometria do código do overlay (chão, fundo) que o jogo usou e o modelo não
    # (tools/remake/stage_extra_keys.json, de stage_keys.py): página montada da VRAM com os TIMs do palco
    ex_keys, ex_imgs, ex_hash, ex_pal = extra_layers(c, tims, tim_raw, {(a & 0x19F, b) for a, b, _, _ in keys})
    room = 64 - len(hd)              # limite do asset; extra_layers vem das mais usadas para as menos
    if len(ex_imgs) > room:
        print(f'palco {c}: {len(ex_imgs) - room} camadas do código do overlay ficam fora (limite de 64)')
        ex_keys, ex_imgs, ex_hash, ex_pal = ex_keys[:room], ex_imgs[:room], ex_hash[:room], ex_pal[:room]
    if ex_imgs:
        tag = f'{fnv(repr(ex_keys).encode()):08x}'
        hd += hd_layers(ex_imgs, os.path.join(OUT, 'palcos', f'palco{c}', f'textures_extra_{tag}'))
        for i, pal in enumerate(ex_pal):
            pal_of[len(keys) + i] = pal
        keys += [(a, b, len(rig.layers) + i, ex_hash[i]) for i, (a, b) in enumerate(ex_keys)]
        layers = layers + ex_imgs
    # variantes de CLUT animada por MoveImage (stage_clut_vars.py): afim sobre a camada base quando a paleta é uma
    # transformação k·rgb + b da paleta base (tipo 1, sem camada nova); senão uma camada por paleta distinta, todas as
    # de uma CLUT ou nenhuma (se não couberem no limite de 64, a CLUT fica com o jogo, como antes)
    var_rows, var_imgs = clut_variants(c, tims, tim_raw, rig, keys, 64 - len(hd), pal_of)
    if var_imgs:
        tag = f'{fnv(repr([r[:4] for r in var_rows if r[4] == 0]).encode()):08x}'
        hd += hd_layers(var_imgs, os.path.join(OUT, 'palcos', f'palco{c}', f'textures_var_{tag}'))
        layers = layers + var_imgs
    base = len(layers) - len(var_imgs)   # camada local -1 - i das variantes -> base + i
    var_rows = [(k, sx, sy, base - 1 - li_ if li_ < 0 else li_, kind, kq, bq) for k, sx, sy, li_, kind, kq, bq in var_rows]
    keys = [(a, b, base - 1 - li_ if li_ < 0 else li_, h) for a, b, li_, h in keys]
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
                rows.append((w, npk, len(fs), fs[0] if fs else 0, [slots[f] for f in fs], 1 if li in PREFIX else 0))
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
    stg += struct.pack('<6I', 3 if var_rows else 1, c, len(mesh_nodes), len(hd), len(keys), len(model))
    stg += struct.pack('<2I', fnv(ov[:512]), fnv(model[:256]))
    for r in node_rows:
        stg += struct.pack('<4I', *r)
    for w, npk, nf, f0, sl, pfx in prims:
        cs = [list(s) for s in sl] + [[0, 0, 0]] * (2 - len(sl))
        stg += struct.pack('<HBBI', w, npk, nf, f0) + bytes(cs[0] + cs[1]) + bytes([pfx, 0])
    for tp, cw, li_, h in keys:
        stg += struct.pack('<HHB', tp, cw, li_) + (h & 0xFFFFFF).to_bytes(3, 'little')
    if var_rows:
        stg += struct.pack('<I', len(var_rows))
        for k, sx, sy, li_, kind, kq, bq in var_rows:
            assert 0 <= li_ < len(hd) and 0 <= k < len(keys)
            stg += struct.pack('<HhhBB6h', k, sx, sy, li_, kind, *kq, *bq)
    for d in outs_dir:
        os.makedirs(d, exist_ok=True)
        with open(os.path.join(d, f'palco{c}.bin'), 'wb') as fh:
            fh.write(buf)
        with open(os.path.join(d, f'palco{c}.stg'), 'wb') as fh:
            fh.write(stg)
    print(f'palco {c}: {len(buf) / 1e6:.1f} MB, {len(mesh_nodes)} nós, {len(owner)} vértices ({len(rig.owner)} antes dos '
          f'vincos), {len(rig.tris)} faces ({int(((rig.mode & SEMI) != 0).sum())} semi), {len(hd)} camadas '
          f'({len(ex_imgs)} do código do overlay, {len(var_imgs)} variantes de CLUT em camada, '
          f'{sum(1 for r in var_rows if r[4] == 1)} afins), '
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
        return [], [], [], []
    want = json.load(open(EXTRA_KEYS, encoding='utf-8')).get(str(c), [])
    vram_dir = os.path.join(OUT, 'palcos', 'vram')
    common = set()
    if os.path.exists(os.path.join(vram_dir, 'resumo.json')):
        common = {tuple(int(x, 16) for x in k.split('/'))
                  for k in json.load(open(os.path.join(vram_dir, 'resumo.json'), encoding='utf-8'))['comuns']}
    info = [(t,) + tim_vram(r) for t, r in zip(tims, tim_raw)]
    out_k, out_i, out_h, out_p = [], [], [], []
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
        out_p.append(np.asarray(pal[:16 if bpp == 4 else 256], np.int64))
    return out_k, out_i, out_h, out_p


def key_page(c, tims, tim_raw, rig, key, tp, bpp):
    """Índices 256x256 da página de uma chave e a cobertura: do TIM da camada do modelo, da página montada com os TIMs do
    palco (como extra_layers) ou da página lida da VRAM (stage_vram_keys.py). (None, None) se não houver."""
    if key is not None and 0 <= key[2] < len(rig.layers):
        tim = tims[rig.layers[key[2]][0]]
        idx = np.zeros((256, 256), np.int64)
        cov = np.zeros((256, 256), bool)
        idx[:tim.idx.shape[0], :tim.idx.shape[1]] = tim.idx
        cov[:tim.idx.shape[0], :tim.idx.shape[1]] = True
        return idx, cov
    tpx = 16 // bpp
    tx, ty, hw = (tp & 0xF) * 64, ((tp >> 4) & 1) * 256, 256 // tpx
    idx = np.zeros((256, 256), np.int64)
    cov = np.zeros((256, 256), bool)
    for tim, raw in zip(tims, tim_raw):
        x, y, _cx, _cy, tb = tim_vram(raw)
        if tb != bpp:
            continue
        x0, x1 = max(x, tx), min(x + tim.w // tpx, tx + hw)
        y0, y1 = max(y, ty), min(y + tim.h, ty + 256)
        if x0 < x1 and y0 < y1:
            idx[y0 - ty:y1 - ty, (x0 - tx) * tpx:(x1 - tx) * tpx] = tim.idx[y0 - y:y1 - y, (x0 - x) * tpx:(x1 - x) * tpx]
            cov[y0 - ty:y1 - ty, (x0 - tx) * tpx:(x1 - tx) * tpx] = True
    if cov.any():
        return idx, cov
    vd = os.path.join(OUT, 'palcos', 'vram', str(c))
    for fn in sorted(os.listdir(vd)) if os.path.isdir(vd) else []:
        if fn.startswith(f'{tp:03x}_') and fn.endswith('.npz') and len(fn) == 12:
            raw = np.load(os.path.join(vd, fn))['page'].astype(np.int64)
            idx = np.stack([(raw >> (bpp * i)) & ((1 << bpp) - 1) for i in range(tpx)], axis=-1).reshape(256, 256)
            return idx, np.ones((256, 256), bool)
    return None, None


def page_rgba(idx, cov, pal):
    v = pal[np.clip(idx, 0, len(pal) - 1)].astype(np.int64)
    img = bm.c16_to_rgba(v)
    img[~cov] = 0
    stp = ((v >> 15) & 1).astype(bool) & (v != 0) & cov
    img[..., 3][stp] = STP_ALPHA
    return img


AFFINE_TOL = float(os.environ.get('BAG2_AFFINE_TOL', '1.5'))   # resíduo máximo do ajuste afim, em unidades de 5 bits


def affine_fit(base, pal, used):
    """Ajuste rgb' = k·rgb + b por canal (mínimos quadrados) da paleta `base` para `pal`, nos índices `used` (os que a
    página usa e não são transparentes). Devolve (k[3], b[3], resíduo máximo em unidades de 5 bits)."""
    b5 = np.stack([(base >> sh) & 31 for sh in (0, 5, 10)], 1).astype(float)
    p5 = np.stack([(pal >> sh) & 31 for sh in (0, 5, 10)], 1).astype(float)
    ks, bs, worst = [], [], 0.0
    for ch in range(3):
        x, y = b5[used, ch], p5[used, ch]
        if len(x) >= 2 and x.max() > x.min():
            (k, b), *_ = np.linalg.lstsq(np.stack([x, np.ones_like(x)], 1), y, rcond=None)
        else:
            k, b = 1.0, float((y - x).mean()) if len(x) else 0.0
        k = max(k, 0.0)
        worst = max(worst, float(np.abs(k * x + b - y).max()) if len(x) else 0.0)
        ks.append(k); bs.append(b)
    return ks, bs, worst


def affine_from(anchor, lines, used):
    """Ajuste afim de `anchor` para cada paleta de `lines`, nos índices usados. A cobertura (cor 0 = transparente no PS1,
    e o alfa e o STP vêm da camada âncora) tem de ser a mesma: senão None. Devolve (resíduo máximo, [(k, b)])."""
    n = len(anchor)
    a0 = anchor[used] == 0
    fit = used & (anchor != 0)
    out, worst = [], 0.0
    for pal in lines:
        pal = pal[:n]
        if np.any((pal[used] == 0) != a0):
            return None
        ks, bs, r = affine_fit(anchor, pal, fit)
        worst = max(worst, r)
        out.append((ks, bs))
    return worst, out


def clut_variants(c, tims, tim_raw, rig, keys, room, pal_of):
    """CLUT animada por MoveImage (tools/remake/stage_clut_vars.py, vram/<c>/clut_var.json). Para cada CLUT animada:
    1. tipo 1 (afim): escolhe uma paleta âncora (a CLUT do TIM da chave, se tiver cor, ou uma linha do banco) tal que
       toda linha de origem seja k·rgb + b da âncora com a mesma cobertura e resíduo máximo <= AFFINE_TOL nos índices
       usados; a âncora do TIM já é a camada da chave, a âncora de linha vira uma camada (uma por página); as linhas
       não custam camada: só os coeficientes na tabela;
    2. senão tipo 0: uma camada por paleta distinta de cada página, todas as da CLUT ou nenhuma (limite `room`).
    Chaves animadas que ainda não estavam na tabela (página só na VRAM) entram em `keys` com a camada da primeira
    variante (camada local, -1 - i; export() resolve). Devolve (linhas (chave, x, y, camada (>= 0 absoluta, < 0 local
    -1 - i), tipo, k[3] em 8.8, b[3] em 1/4096 da escala cheia), imagens 256x256 das camadas novas)."""
    import json
    f = os.path.join(OUT, 'palcos', 'vram', str(c), 'clut_var.json')
    if not os.path.exists(f) or room <= 0:
        return [], []
    vd = os.path.dirname(f)
    rows, imgs = [], []
    same = {}   # (página (índices), paleta) -> camada local: a mesma paleta na mesma imagem dá a mesma camada
    kq = lambda ks: [int(round(k * 256)) for k in ks]
    bq = lambda bs: [int(round(b / 31.0 * 4096)) for b in bs]
    for cw_hex, e in json.load(open(f, encoding='utf-8')).items():
        cw, bpp = int(cw_hex, 16), 4 if e['w'] == 16 else 8
        n = 16 if bpp == 4 else 256
        lines = [(sx, sy, np.array(row, np.int64)[:n]) for sx, sy, row in e['src']]
        pages = [(i, keys[i][0]) for i, k in enumerate(keys) if k[1] == cw]
        if not pages:   # página só na VRAM: a chave entra agora
            pages = [(None, int(fn[:3], 16)) for fn in sorted(os.listdir(vd))
                     if fn.endswith(f'_{cw:04x}.npz') and len(fn) == 12 and ((int(fn[:3], 16) >> 7) & 3) == (bpp == 8)]
        # as páginas (índices e cobertura) de cada chave
        pg = []
        for ki, tp in pages:
            idx, cov = key_page(c, tims, tim_raw, rig, keys[ki] if ki is not None else None, tp, bpp)
            if idx is not None:
                pg.append((ki, tp, idx, cov))
        if not pg:
            continue
        # 1. afim: a âncora que dá o menor resíduo máximo sobre todas as páginas e linhas
        best = None   # (resíduo, tipo da âncora, paleta âncora, [(ki, tp, idx, cov, [(k, b)])])
        cands = [('linha', pal) for _, _, pal in lines]
        if all(ki is not None and keys[ki][2] >= 0 and pal_of.get(ki) is not None and (pal_of[ki] != 0).any() for ki, *_ in pg):
            cands = [('tim', None)] + cands
        for kind_a, apal in cands:
            tot, per = 0.0, []
            for ki, tp, idx, cov in pg:
                anchor = (pal_of[ki][:n] if kind_a == 'tim' else apal)
                used = np.zeros(n, bool)
                used[np.unique(idx[cov]) % n] = True
                r = affine_from(anchor, [pal for _, _, pal in lines], used)
                if r is None:
                    per = None
                    break
                tot = max(tot, r[0])
                per.append((ki, tp, idx, cov, anchor, r[1]))
            if per is not None and (best is None or tot < best[0]):
                best = (tot, kind_a, apal, per)
        if best is not None and best[0] <= AFFINE_TOL:
            worst, kind_a, apal, per = best
            need = 0 if kind_a == 'tim' else len({(idx.tobytes(), apal.tobytes()) for _, _, idx, _, _, _ in per
                                                    if (idx.tobytes(), apal.tobytes()) not in same})
            if len(imgs) + need <= room:
                for ki, tp, idx, cov, anchor, fits in per:
                    if kind_a == 'tim':
                        lay = keys[ki][2]
                    else:
                        k2 = (idx.tobytes(), anchor.tobytes())
                        if k2 not in same:
                            same[k2] = len(imgs)
                            imgs.append(page_rgba(idx, cov, anchor))
                        lay = -1 - same[k2]
                        if ki is None:
                            keys.append((tp, cw, lay, 0))
                            ki = len(keys) - 1
                    for (sx, sy, _pal), (ks, bs) in zip(lines, fits):
                        rows.append((ki, sx, sy, lay, 1, kq(ks), bq(bs)))
                print(f'palco {c}: CLUT {cw_hex} afim ({len(lines)} variantes sobre a âncora {kind_a}'
                      f'{"" if kind_a == "tim" else f" {np.argmax([np.array_equal(apal, l[2]) for l in lines])}"}, '
                      f'{need} camada(s), resíduo máximo {worst:.2f}/31)')
                continue
        # 2. camadas: uma por paleta distinta de cada página
        cand = []
        for ki, tp, idx, cov in pg:
            for sx, sy, pal in lines:
                k2 = (idx.tobytes(), pal.tobytes())
                cand.append((ki, tp, idx, cov, sx, sy, k2, pal))
        need = len({k2 for *_, k2, _ in cand if k2 not in same})
        why = f'resíduo afim {best[0]:.2f}/31 > {AFFINE_TOL}' if best is not None else 'cobertura das paletas difere'
        if len(imgs) + need > room:
            print(f'palco {c}: CLUT {cw_hex} ({need} camadas novas; {why}) fica com o jogo (limite de 64)')
            continue
        print(f'palco {c}: CLUT {cw_hex} em {need} camadas ({why})')
        new_key = {}
        for ki, tp, idx, cov, sx, sy, k2, pal in cand:
            if k2 not in same:
                same[k2] = len(imgs)
                imgs.append(page_rgba(idx, cov, pal))
            if ki is None:
                if tp not in new_key:
                    keys.append((tp, cw, -1 - same[k2], 0))
                    new_key[tp] = len(keys) - 1
                ki = new_key[tp]
            rows.append((ki, sx, sy, -1 - same[k2], 0, [256, 256, 256], [0, 0, 0]))
    return rows, imgs


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
