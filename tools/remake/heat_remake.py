#!/usr/bin/env python3
"""Piloto de modernização gráfica de um personagem do Bust A Groove 2 (padrão: HEAT).

Do disco até as imagens: lê modelo/animações/texturas (bag2model), solda a malha entre ossos (rig),
anima pelo esqueleto do jogo, suaviza cada frame com Phong Tessellation (ou Loop, --method loop),
calcula oclusão ambiente, amplia as texturas com Real-ESRGAN e renderiza ORIGINAL (imitação PS1) vs
MODERNIZADO (luz por pixel com sombras) e MODERNIZADO cel-shading. Notas: docs/remake-visual.md.

Uso (venv com tools/remake/requirements.txt; Real-ESRGAN em $BAG2_TOOLS/realesrgan/bin):
    python tools/remake/heat_remake.py                 # tudo, saída em logs/remake/heat/
    python tools/remake/heat_remake.py --anim 2 --frame 20 --only compare
    python tools/remake/heat_remake.py --list-anims
Dados do jogo e imagens ficam fora do git (logs/ é ignorado).
"""
from __future__ import annotations

import argparse
import os
import sys

import numpy as np
from PIL import Image, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(HERE, '..'))

from bag2model import CHARS, load_character, pose_at  # noqa: E402
from rig import (build_rig, orient_faces, original_normals, phong_tessellate, pose_positions, smooth_normals,
                 subdivide, vertex_normals)  # noqa: E402
from render import Renderer, pack  # noqa: E402

DISC = os.path.join(HERE, '..', '..', 'disc', 'Bust A Groove 2 (USA).bin')
OUT = os.path.join(HERE, '..', '..', 'logs', 'remake')
ESRGAN = os.path.join(os.environ.get('BAG2_TOOLS', ''), 'realesrgan', 'bin', 'realesrgan-ncnn-vulkan.exe')
# HEAT: nós 20..23 são mãos alternativas (sem pai, posicionadas pelo jogo ao trocar de gesto)
SKIP = {'HEAT': {20, 21, 22, 23}}


def ps1_layers(rig, tims, expr=None):
    """Uma camada 256x256 por (TIM, linha de CLUT). expr: expressão do rosto (faces.EXPRESSIONS), aplicada na
    página do rosto como o jogo faz na VRAM."""
    import dataclasses
    import faces
    out = []
    for tex, clut in rig.layers:
        tim = tims[tex]
        if expr and tex == faces.FACE_TEX:
            tim = dataclasses.replace(tim, idx=faces.apply_ps1(tim.idx, expr))
        img = tim.rgba(clut)
        pad = np.zeros((256, 256, 4), np.uint8)
        pad[:img.shape[0], :img.shape[1]] = img
        out.append(pad)
    return out


def hd_layers(layers, cache_dir, scale=4, model='realesrgan-x4plus-anime'):
    """Real-ESRGAN no RGB (pixels transparentes dilatados antes), alfa via xBR (hd_textures.py)."""
    import shutil
    import hd_textures as hd
    os.makedirs(cache_dir, exist_ok=True)
    paths = [os.path.join(cache_dir, f'layer{i:02d}_{model}.png') for i in range(len(layers))]
    if not all(os.path.exists(p) for p in paths):
        tin, tout = os.path.join(cache_dir, 'in'), os.path.join(cache_dir, 'out')
        for d in (tin, tout):
            shutil.rmtree(d, ignore_errors=True); os.makedirs(d)
        for i, l in enumerate(layers):
            Image.fromarray(hd.fill_transparent(l)).save(os.path.join(tin, f'{i:02d}.png'))
        hd.run_esrgan(os.path.abspath(tin), os.path.abspath(tout), scale, ESRGAN, model)
        for i, l in enumerate(layers):
            rgb = np.asarray(Image.open(os.path.join(tout, f'{i:02d}.png')).convert('RGB'))
            a = hd.alpha_xbr(l, scale)
            o = np.dstack([rgb, a]).astype(np.uint8)
            Image.fromarray(o).save(paths[i])
        shutil.rmtree(tin); shutil.rmtree(tout)
    return [np.asarray(Image.open(p).convert('RGBA')) for p in paths]


class Character:
    """hands=True (HEAT): mãos novas com dedos (tools/remake/hands.py) no MODERNIZADO; o ORIGINAL usa a mão
    original do mesmo gesto. Gesto atual: self.gesture = (esquerda, direita), índices em hands.GESTURES."""

    def __init__(self, name='HEAT', levels=2, crease_deg=80.0, method='phong', pin_open=True, hands=None,
                 ears=True, ai_body=None):
        self.method, self.levels = method, levels
        k = CHARS.index(name)
        self.nodes, self.anims, self.tims, _ = load_character(DISC, k)
        self.hands = (name == 'HEAT') if hands is None else hands
        self.gesture = (0, 0)
        if self.hands:
            import hands as H
            base = build_rig(self.nodes, set())
            flipped = orient_faces(base, self.nodes)
            # ORIGINAL: corpo + mãos originais de cada gesto (as alternativas no osso da mão, como no jogo)
            self.rig_orig, _ = H.attach_hands(base, self.nodes, None, only_original=True)
            layers = ps1_layers(base, self.tims)
            hd = hd_layers(layers, os.path.join(OUT, name.lower(), 'textures'))
            self.rig, self.hand_groups = H.attach_hands(base, self.nodes, hd,
                                                        cache_dir=os.path.join(OUT, name.lower(), 'maos'))
            if ears:
                import ears as E
                self.rig = E.attach_nose(E.attach_ears(self.rig))
            if ai_body:   # experimento de IA: roupa da malha gerada (archive/ai/ai_hybrid.py)
                sys.path.insert(0, os.path.join(HERE, '..', '..', 'archive', 'ai'))
                import ai_hybrid
                z = np.load(os.path.join(OUT, 'ai', f'{ai_body}_encaixe.npz'))
                self.rig, self.ai_info = ai_hybrid.attach_ai_body(
                    self.rig, self.nodes, z['V'], z['F'], (np.zeros(3), {14: (0, 0, 450), 17: (0, 4096, 450)}))
            import detail
            detail.mark_flat(self.rig)
        else:
            self.rig = build_rig(self.nodes, SKIP.get(name, set()))
            flipped = orient_faces(self.rig, self.nodes)
            self.rig_orig = self.rig
        r = self.rig
        self.used = np.unique(r.tris[self.faces(r)])   # vértices desenhados no gesto padrão
        print(f'{name}: {len(r.owner)} vértices, {len(r.tris)} triângulos ({flipped} reorientados), '
              f'{len(r.layers)} camadas de textura')
        if method == 'loop':
            p_rest, _ = pose_positions(r, self.nodes)
            self.sub = subdivide(r.tris, len(r.owner), levels, p_rest, crease_deg, pin_open)
            # atributos por canto da malha subdividida (interpolação linear dentro da face original)
            par, B = self.sub.parent, self.sub.bary
            self.s_base = np.einsum('fkj,fjd->fkd', B, r.base[par])
            self.s_over = np.einsum('fkj,fjd->fkd', B, r.over[par])
            self.s_uv = np.einsum('fkj,fjd->fkd', B, r.uv[par])
            self.s_layer, self.s_mode = r.layer[par], r.mode[par]

    def faces(self, r):
        """Faces desenhadas com o gesto atual (todas, sem mãos novas)."""
        if getattr(r, 'hand', None) is None:
            return np.ones(len(r.tris), bool)
        return np.isin(r.hand, (0, 1 + self.gesture[0], 4 + self.gesture[1]))

    def pose(self, anim, frame):
        if anim is None or anim < 0:
            return None
        return pose_at(self.anims[anim], frame)

    def original(self, pose):
        r = self.rig_orig
        p, mats = pose_positions(r, self.nodes, pose)
        m = self.faces(r)
        uv = r.uv[m] + 0.5
        return pack(p[r.tris[m]], original_normals(r, mats)[m], r.base[m], r.over[m], uv, r.layer[m], r.mode[m]), p

    def modern(self, pose, rd=None, flat_mesh=False, ao=True):
        """Malha suavizada (ou a original, com flat_mesh) com normais novas e oclusão ambiente."""
        r = self.rig
        p, _ = pose_positions(r, self.nodes, pose)
        m = self.faces(r)
        T, base, over, uv, layer, mode = r.tris[m], r.base[m], r.over[m], r.uv[m], r.layer[m], r.mode[m]
        if flat_mesh:   # malha original, só iluminação/textura novas (para isolar o efeito da suavização)
            q, tris = p, T
        elif self.method == 'phong':
            # Phong Tessellation: 4^levels triângulos por face, normais de sombreamento interpoladas
            cn = smooth_normals(p, T)
            pts, par, B = phong_tessellate(p, T, vertex_normals(p, T), 2 ** self.levels)
            n = np.einsum('fkj,fjd->fkd', B, cn[par])
            n /= np.maximum(np.linalg.norm(n, axis=-1, keepdims=True), 1e-9)
            tris = np.arange(len(pts) * 3).reshape(-1, 3)
            q = pts.reshape(-1, 3)
            lerp = lambda a: np.einsum('fkj,fjd->fkd', B, a[par])
            occ = rd.ambient_occlusion(q, tris, smooth=0)[tris] if ao and rd is not None else None
            return pack(pts, n, lerp(base), lerp(over), lerp(uv) + 0.5, layer[par], mode[par], occ)
        else:
            q, tris = self.sub.S @ p, self.sub.tris
            base, over, uv, layer, mode = self.s_base, self.s_over, self.s_uv, self.s_layer, self.s_mode
        n = smooth_normals(q, tris)
        occ = None
        if ao and rd is not None:
            occ = rd.ambient_occlusion(q, tris)[tris]
        return pack(q[tris], n, base, over, uv + 0.5, layer, mode, occ)


def label(img, text):
    d = ImageDraw.Draw(img)
    try:
        f = ImageFont.truetype('C:/Windows/Fonts/segoeuib.ttf', max(16, img.width // 26))
    except OSError:
        f = ImageFont.load_default()
    d.text((14, 10), text, fill=(235, 235, 240), font=f, stroke_width=2, stroke_fill=(0, 0, 0))
    return img


def side_by_side(imgs):
    w = sum(i.width for i in imgs) + 6 * (len(imgs) - 1)
    out = Image.new('RGB', (w, imgs[0].height), (0, 0, 0))
    x = 0
    for i in imgs:
        out.paste(i, (x, 0)); x += i.width + 6
    return out


def render_set(ch, rd, pose, yaw, size, pts, styles=('ps1', 'hd', 'modern'), floor_y=0.0, pitch=8.0,
               ps1_scale=3, margin=0.9, stage=None, remap=()):
    """Um painel por estilo, mesma câmera, enquadrando os pontos `pts`. stage = luzes do palco (render.STAGE_LIGHT,
    no espaço da câmera do GTE): o ORIGINAL usa a soma do GTE por vértice e o MODERNIZADO a mesma por pixel.
    remap = expressão do rosto no MODERNIZADO (Renderer.render_modern); o ORIGINAL usa as camadas do Renderer."""
    data_o, _ = ch.original(pose)
    center, dist = Renderer.fit(pts, yaw, pitch, size, margin)
    radius = np.linalg.norm(pts.max(0) - pts.min(0)) / 2
    vp, eye = Renderer.camera(center, dist, yaw, pitch, size)
    from render import look_at
    view = look_at(eye, center)
    sk = dict(stage=stage, view=view)
    out = {}
    if 'ps1' in styles:
        small = (size[0] // ps1_scale, size[1] // ps1_scale)
        vps, _ = Renderer.camera(center, dist, yaw, pitch, small)
        out['ps1'] = rd.render_ps1(data_o, vps, small, **sk).resize(size, Image.NEAREST)
    if 'hd' in styles:
        out['hd'] = rd.render_ps1(data_o, vp, size, **sk)
    kw = dict(floor_y=floor_y, remap=remap, **sk)
    if 'modern' in styles or 'toon' in styles:
        data_m = ch.modern(pose, rd)
        if 'modern' in styles:
            out['modern'] = rd.render_modern(data_m, vp, eye, size, center, radius, **kw)
        if 'toon' in styles:
            out['toon'] = rd.render_modern(data_m, vp, eye, size, center, radius, toon=True,
                                           outline_width=radius * 0.006, **kw)
    if 'flat' in styles:
        out['flat'] = rd.render_modern(ch.modern(pose, rd, flat_mesh=True), vp, eye, size, center, radius, **kw)
    return out


TITLES = {'ps1': 'ORIGINAL (PS1, 320x240)', 'hd': 'ORIGINAL em alta resolução', 'flat': 'Só luz + textura HD',
          'modern': 'MODERNIZADO', 'toon': 'MODERNIZADO (cel-shading)'}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--char', default='HEAT')
    ap.add_argument('--anim', type=int, default=14)
    ap.add_argument('--frame', type=float, default=25)
    ap.add_argument('--yaw', type=float, default=25)
    ap.add_argument('--levels', type=int, default=2)
    ap.add_argument('--crease', type=float, default=80.0, help='ângulo diedro (graus) acima do qual a aresta fica viva')
    ap.add_argument('--method', choices=['phong', 'loop'], default='phong', help='suavização da malha')
    ap.add_argument('--only', choices=['compare', 'metodos', 'turntable', 'anim', 'maos', 'rosto', 'all'], default='all')
    ap.add_argument('--list-anims', action='store_true')
    ap.add_argument('--styles', default='ps1,hd,modern,toon')
    args = ap.parse_args()
    out = os.path.join(OUT, args.char.lower().replace(' ', '_'))
    os.makedirs(out, exist_ok=True)
    ch = Character(args.char, args.levels, args.crease, args.method)
    if args.list_anims:
        for i, a in enumerate(ch.anims):
            print(i, (a.frames, a.step) if a else '-')
        return
    layers = ps1_layers(ch.rig, ch.tims)
    hd = hd_layers(layers, os.path.join(out, 'textures'))
    rd = Renderer(layers, hd)
    styles = args.styles.split(',')
    size = (600, 800)
    # enquadramento fixo pela animação inteira (evita a câmera "respirar"); chão = pé mais baixo
    a = ch.anims[args.anim]
    allp = [pose_positions(ch.rig, ch.nodes, ch.pose(args.anim, f))[0] for f in range(a.frames)]
    anim_pts = np.concatenate([q[ch.used] for q in allp])[::7]
    floor_y = anim_pts[:, 1].min()
    pose = ch.pose(args.anim, args.frame)
    p_pose = pose_positions(ch.rig, ch.nodes, pose)[0]
    pose_pts = p_pose[ch.used]
    if args.only in ('compare', 'all'):
        for yaw in (args.yaw, args.yaw + 180):
            imgs = render_set(ch, rd, pose, yaw, size, pose_pts, styles=styles, floor_y=floor_y)
            fn = os.path.join(out, f'compare_a{args.anim}_f{int(args.frame)}_y{int(yaw)}.png')
            side_by_side([label(imgs[s], TITLES[s]) for s in styles]).save(fn)
            print(fn)
        # close no rosto
        head = p_pose[ch.rig.owner == 13]
        imgs = render_set(ch, rd, pose, args.yaw, (600, 600), head, styles=styles, floor_y=floor_y, pitch=0,
                          margin=0.85)
        fn = os.path.join(out, f'compare_rosto_a{args.anim}_f{int(args.frame)}.png')
        side_by_side([label(imgs[s], TITLES[s]) for s in styles]).save(fn)
        print(fn)
    if args.only in ('rosto', 'all'):
        # expressões do rosto (tools/remake/faces.py): ORIGINAL com a casa copiada na página (o que o jogo faz na
        # VRAM) x REMAKE com o UV deslocado dentro da casa (o que o núcleo faz)
        import faces
        head = p_pose[ch.rig.owner == 13]
        mask = sum(1 << i for i, (t, _) in enumerate(ch.rig.layers) if t == faces.FACE_TEX)
        panels = []
        for name, expr in faces.EXPRESSIONS.items():
            rd.set_ps1_layers(ps1_layers(ch.rig, ch.tims, expr))
            rms = [(mask, *r[1:]) for r in faces.remaps(expr)]
            imgs = render_set(ch, rd, pose, 10, (420, 420), head, styles=('hd', 'modern'), pitch=0, margin=0.8,
                              floor_y=floor_y, remap=rms)
            panels.append(side_by_side([label(imgs['hd'], f'ORIGINAL: {name}'), label(imgs['modern'], 'REMAKE')]))
        rd.set_ps1_layers(layers)
        w, h = panels[0].width, panels[0].height
        img = Image.new('RGB', (w * 2 + 12, (h + 6) * ((len(panels) + 1) // 2)))
        for i, pn in enumerate(panels):
            img.paste(pn, ((i % 2) * (w + 12), (i // 2) * (h + 6)))
        fn = os.path.join(out, 'expressoes.png')
        img.save(fn)
        print(fn)
    if args.only in ('maos', 'all') and ch.hands:
        # mãos com dedos (fase 2): cada gesto, as duas mãos, 4 ângulos, ORIGINAL x MODERNIZADO
        import hands as H
        for g, gname in enumerate(H.GESTURES):
            ch.gesture = (g, g)
            p_g = pose_positions(ch.rig, ch.nodes, pose)[0]
            drawn = np.unique(ch.rig.tris[ch.faces(ch.rig)])
            rows = []
            for bone in (16, 19):
                sel = np.intersect1d(np.nonzero(ch.rig.owner == bone)[0], drawn)
                imgs = []
                for yaw in (args.yaw, args.yaw + 90, args.yaw + 180, args.yaw + 270):
                    r = render_set(ch, rd, pose, yaw, (360, 360), p_g[sel], styles=('hd', 'modern'), pitch=10,
                                   margin=0.8, floor_y=floor_y)
                    imgs += [r['hd'], r['modern']]
                rows.append(side_by_side(imgs))
            img = Image.new('RGB', (rows[0].width, rows[0].height * 2 + 6))
            img.paste(rows[0], (0, 0)); img.paste(rows[1], (0, rows[0].height + 6))
            fn = os.path.join(out, f'maos_{gname}.png')
            d = ImageDraw.Draw(img)
            try:
                f = ImageFont.truetype('C:/Windows/Fonts/segoeuib.ttf', 22)
            except OSError:
                f = ImageFont.load_default()
            d.text((10, 8), f'mão {gname}: pares ORIGINAL | MODERNIZADO; esquerda em cima, direita embaixo',
                   fill=(235, 235, 240), font=f, stroke_width=2, stroke_fill=(0, 0, 0))
            img.save(fn)
            print(fn)
        ch.gesture = (0, 0)
    if args.only in ('metodos', 'all'):
        # mesma pose e luz, só muda a geometria: original, Loop (borda presa / borda curva), Phong
        variants = {'malha original': (ch, True),
                    'Loop, borda presa': (Character(args.char, args.levels, args.crease, 'loop', True), False),
                    'Loop, borda curva': (Character(args.char, args.levels, args.crease, 'loop', False), False),
                    'Phong Tessellation': (Character(args.char, args.levels, args.crease, 'phong'), False)}
        for nm, sel, sz, pitch in [('corpo', ch.used, (450, 600), 8),
                                   ('rosto', np.nonzero(ch.rig.owner == 13)[0], (450, 450), 0),
                                   ('pe', np.nonzero(np.isin(ch.rig.owner, (6, 7)))[0], (450, 450), 8)]:
            pts = p_pose[sel]
            c, d = Renderer.fit(pts, args.yaw, pitch, sz)
            vp, eye = Renderer.camera(c, d, args.yaw, pitch, sz)
            R = np.linalg.norm(pts.max(0) - pts.min(0)) / 2
            imgs = [label(rd.render_modern(cc.modern(pose, rd, flat_mesh=flat), vp, eye, sz, c, R, floor_y=floor_y), k)
                    for k, (cc, flat) in variants.items()]
            fn = os.path.join(out, f'metodos_{nm}.png')
            side_by_side(imgs).save(fn)
            print(fn)
    if args.only in ('turntable', 'all'):
        import imageio.v2 as iio
        frames = []
        for i in range(36):
            imgs = render_set(ch, rd, pose, args.yaw + i * 10, (360, 480), pose_pts,
                              styles=('ps1', 'modern'), floor_y=floor_y)
            frames.append(np.asarray(side_by_side([label(imgs[s], TITLES[s]) for s in ('ps1', 'modern')])))
        fn = os.path.join(out, 'turntable.gif')
        iio.mimsave(fn, frames, duration=80, loop=0)
        print(fn)
    if args.only in ('anim', 'all'):
        import imageio.v2 as iio
        frames = []
        for f in range(a.frames):
            imgs = render_set(ch, rd, ch.pose(args.anim, f), args.yaw, (360, 480), anim_pts,
                              styles=('ps1', 'modern', 'toon'), floor_y=floor_y)
            frames.append(np.asarray(side_by_side([label(imgs[s], TITLES[s]) for s in ('ps1', 'modern', 'toon')])))
        fn = os.path.join(out, f'danca_a{args.anim}.gif')
        iio.mimsave(fn, frames, duration=1000 / 30, loop=0)
        print(fn)


if __name__ == '__main__':
    main()
