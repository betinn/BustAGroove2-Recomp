#!/usr/bin/env python3
"""Exporta o HEAT para o plugin do jogo: hoje é só um atalho para o exportador genérico da fase 4
(tools/remake/export_char.py HEAT), que grava o asset v6 (formato descrito lá). Até a fase 3 este arquivo era o
exportador (v5, só do HEAT); a parte específica do HEAT (mãos com dedos, orelhas, nariz) continua lá dentro.

Uso: python tools/remake/export_heat.py [saída ...]   (padrão: build-debug/mods/remake/heat.bin e build/...)
"""
from __future__ import annotations

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from export_char import default_outs, export  # noqa: E402

if __name__ == '__main__':
    export('HEAT', sys.argv[1:] or default_outs('HEAT'))
