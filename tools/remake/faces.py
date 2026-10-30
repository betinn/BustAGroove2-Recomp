"""Expressão do rosto do HEAT (dentes, olhos semicerrados/fechados, marca verde no olho).

Como o jogo faz (medido na build-debug em 2026-10-27, docs/remake-visual.md "Expressão do rosto"): o rosto não
troca UV, CLUT nem página. A página do rosto (TIM 1, na VRAM em 768,256) é um atlas: além das "casas" que as faces
da cabeça amostram (olho A, olho B, boca), tem as variantes de cada uma ao lado. O código do personagem chama
MoveImage (libgpu, 0x800482F4) e copia a variante da vez para a casa, na VRAM, durante a lógica do quadro (antes
de a OT do quadro ser desenhada). O remake não lê a VRAM: o plugin olha as chamadas de MoveImage e manda no
quadro, por casa, o deslocamento até a origem copiada; o shader soma esse deslocamento ao UV dentro da casa e
lê a variante na camada HD (que tem o atlas inteiro, ampliado).

Aqui ficam os dados medidos (origem de cada TIM na VRAM, casas e variantes vistas) e as contas usadas pelos
renders offline (heat_remake.py --only rosto) e pelo exportador (export_heat.py, asset v5).
"""
from __future__ import annotations

import numpy as np

# Origem na VRAM (x em halfwords, y) de cada TIM de corpo do HEAT jogador 1: tpage dos pacotes do jogo
# (0x1B = 704,256 para o TIM 0; 0x1C = 768,256 para o TIM 1). O TIM 2 não é usado pelos nós desenhados.
PAGE_VRAM = {0: (704, 256), 1: (768, 256)}
BPP = 4   # todos os TIMs de corpo são 4 bits: 4 texels por halfword da VRAM

# Casas da página do rosto (TIM 1) em texels da página: (x0, y0, largura, altura) e as origens que o jogo copia
# para cada uma (MoveImage visto no anel GP0 por 60 s de partida no Stage 1).
SLOTS = {
    'olho_a': ((0, 32, 48, 24), [(48, 0), (96, 0), (144, 0)]),
    'olho_b': ((0, 92, 48, 28), [(48, 24), (96, 24), (144, 24)]),
    'boca': ((8, 148, 40, 20), [(48, 52), (88, 52)]),
}
# Expressões para os renders offline: casa -> origem (None = conteúdo do disco)
EXPRESSIONS = {
    'disco': {},
    'olhos abertos': {'olho_a': (48, 0), 'olho_b': (48, 24)},
    'olhos semicerrados': {'olho_a': (96, 0), 'olho_b': (96, 24)},
    'olhos fechados': {'olho_a': (144, 0), 'olho_b': (144, 24)},
    'boca fechada': {'boca': (48, 52)},
    'dentes': {'boca': (88, 52)},
}
FACE_TEX = 1


def remaps(expr: dict, tex: int = FACE_TEX):
    """Lista de remapeamentos (tex, x0, y0, x1, y1, du, dv) em texels da página para uma expressão."""
    out = []
    for name, src in expr.items():
        (x, y, w, h), _ = SLOTS[name]
        if src is not None and (src[0] - x or src[1] - y):
            out.append((tex, x, y, x + w, y + h, src[0] - x, src[1] - y))
    return out


def apply_ps1(tim_idx: np.ndarray, expr: dict) -> np.ndarray:
    """O que o jogo faz na VRAM: copia a variante para a casa (índices 4 bits da página)."""
    img = tim_idx.copy()
    for name, src in expr.items():
        (x, y, w, h), _ = SLOTS[name]
        if src is not None:
            img[y:y + h, x:x + w] = tim_idx[src[1]:src[1] + h, src[0]:src[0] + w]
    return img
