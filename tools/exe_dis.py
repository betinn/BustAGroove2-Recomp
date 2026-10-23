#!/usr/bin/env python3
"""Disassemble a range of a PS-X EXE. Usage: exe_dis.py SLUS START_HEX END_HEX"""
import sys, struct
from capstone import Cs, CS_ARCH_MIPS, CS_MODE_MIPS32, CS_MODE_LITTLE_ENDIAN
data = open(sys.argv[1], 'rb').read()
t_addr, t_size = struct.unpack_from('<II', data, 24)
start, end = int(sys.argv[2], 16), int(sys.argv[3], 16)
code = data[2048 + (start - t_addr): 2048 + (end - t_addr)]
md = Cs(CS_ARCH_MIPS, CS_MODE_MIPS32 | CS_MODE_LITTLE_ENDIAN)
for ins in md.disasm(code, start):
    print(f'{ins.address:08x}  {ins.bytes.hex():8s}  {ins.mnemonic:8s} {ins.op_str}')
