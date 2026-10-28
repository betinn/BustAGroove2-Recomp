#!/usr/bin/env python3
"""Exporta o elenco para o remake (fase 4): cada personagem nos dois figurinos (normal e alternativo, "*"), um
processo por asset (export_char.py), e resume o que saiu (tamanho, grupos de mãos, alternativas sem posição, faces
semitransparentes, erros).
Uso: python tools/remake/export_all.py ["NOME|NOME*|..."]   (padrão: os 18 personagens nos dois figurinos)
"""
from __future__ import annotations

import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from bag2model import CHARS  # noqa: E402

PY = os.path.join(HERE, '..', 'recomp', 'logs', 'remake', 'venv', 'Scripts', 'python.exe')


def main():
    names = sys.argv[1].split('|') if len(sys.argv) > 1 else [n + s for s in ('', '*') for n in CHARS]
    for n in names:
        r = subprocess.run([PY, os.path.join(HERE, 'export_char.py'), n], capture_output=True, text=True,
                           errors='replace')
        last = (r.stdout.strip().splitlines() or [''])[-1]
        if r.returncode:
            err = (r.stderr.strip().splitlines() or ['?'])[-1]
            print(f'{n}: ERRO {err}', flush=True)
        else:
            print(last.split(' -> ')[0], flush=True)


if __name__ == '__main__':
    main()
