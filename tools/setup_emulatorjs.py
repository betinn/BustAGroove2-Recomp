#!/usr/bin/env python3
"""Unpack an EmulatorJS release archive (.7z from GitHub releases) into web/public/emulatorjs/.
Only the data/ folder is needed (loader.js + cores). Usage: setup_emulatorjs.py <release.7z>
"""
import os, sys, py7zr
src = sys.argv[1]
dst = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'web', 'public', 'emulatorjs')
os.makedirs(dst, exist_ok=True)
with py7zr.SevenZipFile(src, 'r') as z:
    names = z.getnames()
    wanted = [n for n in names if n.startswith('data/')]
    print(f'{len(names)} entries, extracting {len(wanted)} under data/')
    z.extract(path=dst, targets=wanted)
print('done ->', os.path.abspath(dst))
