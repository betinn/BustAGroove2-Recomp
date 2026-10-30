#!/usr/bin/env python3
"""Decimação da malha da IA (pymeshlab, colapso de arestas por quádricas preservando topologia e bordas), no venv
do experimento. Uso: python archive/ai/ai_decimate.py <malha .npz em logs/remake/ai/> <faces>
Saída: <malha>_<faces>.npz."""
import os
import sys

import numpy as np
import pymeshlab

AI = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', 'logs', 'remake', 'ai')
name, target = sys.argv[1], int(sys.argv[2])
z = np.load(os.path.join(AI, f'{name}.npz'))
ms = pymeshlab.MeshSet()
ms.add_mesh(pymeshlab.Mesh(z['V'].astype(np.float64), z['F'].astype(np.int32)))
ms.meshing_decimation_quadric_edge_collapse(targetfacenum=target, preservetopology=True, preserveboundary=True,
                                            qualitythr=0.4, optimalplacement=True, planarquadric=True)
m = ms.current_mesh()
out = os.path.join(AI, f'{name}_{target}.npz')
np.savez(out, V=m.vertex_matrix(), F=m.face_matrix())
print(out, m.vertex_number(), m.face_number())
