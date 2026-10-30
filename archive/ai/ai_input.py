#!/usr/bin/env python3
"""Imagens de entrada para o experimento de IA imagem->3D (docs/remake-plano.md, fase 2): o HEAT com as texturas
HD, iluminação suave, fundo transparente (alfa pela diferença entre dois renders com fundo preto e branco).
Saída: logs/remake/ai/entrada/heat_<vista>.png (fora do git: render de assets do jogo).
Uso: python archive/ai/ai_input.py [pose: 'descanso', 'A<ângulo>' (pose em A, padrão A450) ou anim:quadro]
"""
from __future__ import annotations

import os
import sys

import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(HERE, '..'))

from heat_remake import OUT, Character, hd_layers, ps1_layers  # noqa: E402
from render import Renderer  # noqa: E402
from rig import pose_positions  # noqa: E402

VIEWS = {'frente': 0, 'esquerda': 90, 'costas': 180, 'direita': 270}


def main():
    arg = sys.argv[1] if len(sys.argv) > 1 else 'A450'
    ch = Character('HEAT', hands=True, ears=False)
    layers = ps1_layers(ch.rig, ch.tims)
    hd = hd_layers(layers, os.path.join(OUT, 'heat', 'textures'))
    rd = Renderer(layers, hd)
    if arg == 'descanso':
        pose = None
    elif arg.startswith('A'):   # pose em A: braços (nós 14 e 17) afastados do corpo, ângulo em unidades de 4096
        a = int(arg[1:] or 450)
        pose = (np.zeros(3), {14: (0, 0, a), 17: (0, 4096, a)})
    else:
        pose = ch.pose(*[float(x) for x in arg.split(':')])
    data, p = ch.original(pose)
    pts = p[np.unique(ch.rig_orig.tris[ch.faces(ch.rig_orig)])]
    out = os.path.join(OUT, 'ai', 'entrada')
    os.makedirs(out, exist_ok=True)
    size = (1024, 1024)
    for name, yaw in VIEWS.items():
        c, d = Renderer.fit(pts, yaw, 0, size, 0.85, fovy=12.0)
        vp, _ = Renderer.camera(c, d, yaw, 0, size, fovy=12.0)   # lente longa: quase ortográfica
        kw = dict(ldir=(0.2, -0.5, -0.85), amb=0.75)
        a = np.asarray(rd.render_ps1(data, vp, size, bg=(0, 0, 0), **kw), np.float32)
        b = np.asarray(rd.render_ps1(data, vp, size, bg=(1, 1, 1), **kw), np.float32)
        alpha = np.clip(1.0 - (b - a).mean(-1) / 255.0, 0, 1)
        rgb = np.where(alpha[..., None] > 0.01, a / np.maximum(alpha[..., None], 1e-3), 0)
        img = np.dstack([np.clip(rgb, 0, 255), alpha * 255]).astype(np.uint8)
        fn = os.path.join(out, f'heat_{name}.png')
        Image.fromarray(img, 'RGBA').save(fn)
        print(fn)


if __name__ == '__main__':
    main()
