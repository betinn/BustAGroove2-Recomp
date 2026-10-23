#!/usr/bin/env python3
"""Quick MIPS R3000 scan of a PS-X EXE: find lui/ori (or lui/addiu) pairs building
given 32-bit constants, list BIOS syscalls, and estimate function starts.
Usage: python tools/exe_scan.py SLUS_011.59 [CONST_HEX ...]
"""
import sys, struct, collections
from capstone import Cs, CS_ARCH_MIPS, CS_MODE_MIPS32, CS_MODE_LITTLE_ENDIAN

data = open(sys.argv[1], 'rb').read()
pc0, gp0, t_addr, t_size = struct.unpack_from('<IIII', data, 16)
text = data[2048:2048 + t_size]
consts = [int(x, 16) for x in sys.argv[2:]]
md = Cs(CS_ARCH_MIPS, CS_MODE_MIPS32 | CS_MODE_LITTLE_ENDIAN)
md.detail = False

words = struct.unpack_from(f'<{len(text)//4}I', text)
# constant search: lui rt, hi ; later ori/addiu rt, rt, lo
hits = collections.defaultdict(list)
last_lui = {}
for i, w in enumerate(words):
    op = w >> 26
    rs = (w >> 21) & 31; rt = (w >> 16) & 31; imm = w & 0xffff
    if op == 0x0f:  # lui
        last_lui[rt] = (i, imm)
    elif op in (0x0d, 0x09) and rs in last_lui:  # ori / addiu with lui'd reg
        li, hi = last_lui[rs]
        if i - li < 8:
            val = (hi << 16) | imm if op == 0x0d else ((hi << 16) + (imm - 0x10000 if imm & 0x8000 else imm)) & 0xffffffff
            for c in consts:
                if val == c:
                    hits[c].append(t_addr + i * 4)
for c in consts:
    print(f'const {c:#010x}: {len(hits[c])} refs ->', [hex(a) for a in hits[c][:20]])

# BIOS syscalls: jal/jr via 0xA0/0xB0/0xC0 + li t1,N  ->  pattern: addiu t2,zero,0xA0 ; jr t2 ; addiu t1,zero,N
sysc = collections.Counter()
for i in range(len(words) - 2):
    w = words[i]
    if (w >> 16) == 0x240a and (w & 0xffff) in (0xa0, 0xb0, 0xc0):  # addiu t2,zero,X
        if words[i+1] == 0x01400008:  # jr t2
            w2 = words[i+2]
            if (w2 >> 16) == 0x2409:  # addiu t1,zero,N
                sysc[(w & 0xff, w2 & 0xff)] += 1
print('BIOS call stubs:', sorted(sysc.items()))

# function starts heuristic: addiu sp,sp,-N preceded by jr ra / nop region
starts = [t_addr + i*4 for i, w in enumerate(words) if (w >> 16) == 0x27bd and (w & 0x8000)]
print('approx functions (addiu sp,sp,-N):', len(starts))
print('entry pc0 =', hex(pc0), 'text', hex(t_addr), '+', hex(t_size))
