#!/usr/bin/env python3
"""Experimento de IA imagem->3D (fase 2): malha do HEAT pelo Hunyuan3D-2mini (tencent/Hunyuan3D-2mini, licença
Tencent Hunyuan Community, uso não comercial) a partir de archive/ai/ai_input.py.

Roda no venv próprio do experimento (PyTorch com CUDA), com o código oficial clonado e o modelo baixado em
logs/remake/ai/dl/ (fora do git):
    logs/remake/ai/venv/Scripts/python archive/ai/ai_mesh.py [imagem] [octree] [passos] [semente]
Saída: logs/remake/ai/malha_<imagem>_<octree>.glb (espaço normalizado do modelo, y para cima).
"""
from __future__ import annotations

import os
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
AI = os.path.join(HERE, '..', '..', 'logs', 'remake', 'ai')
sys.path.insert(0, os.path.join(AI, 'dl', 'Hunyuan3D-2'))
os.environ.setdefault('HY3DGEN_MODELS', os.path.join(AI, 'dl', 'models'))


def main():
    import torch
    from PIL import Image
    from hy3dgen.shapegen import Hunyuan3DDiTFlowMatchingPipeline
    name = sys.argv[1] if len(sys.argv) > 1 else 'frente'
    octree = int(sys.argv[2]) if len(sys.argv) > 2 else 256
    steps = int(sys.argv[3]) if len(sys.argv) > 3 else 50
    seed = int(sys.argv[4]) if len(sys.argv) > 4 else 0
    t0 = time.time()
    pipe = Hunyuan3DDiTFlowMatchingPipeline.from_pretrained('tencent/Hunyuan3D-2mini', subfolder='hunyuan3d-dit-v2-mini')
    t1 = time.time()
    img = Image.open(os.path.join(AI, 'entrada', f'heat_{name}.png')).convert('RGBA')
    mesh = pipe(image=img, num_inference_steps=steps, octree_resolution=octree, num_chunks=20000,
                generator=torch.manual_seed(seed), output_type='trimesh')[0]
    t2 = time.time()
    out = os.path.join(AI, f'malha_{name}_{octree}.glb')
    mesh.export(out)
    print(f'{out}: {len(mesh.vertices)} vértices, {len(mesh.faces)} faces; carga {t1 - t0:.0f} s, geração {t2 - t1:.0f} s, '
          f'VRAM máx. {torch.cuda.max_memory_allocated() / 2**30:.1f} GB')


if __name__ == '__main__':
    main()
