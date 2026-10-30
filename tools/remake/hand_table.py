#!/usr/bin/env python3
"""Mãos alternativas pela tabela do overlay de cada personagem (fase 5; offline, só o disco).

O script da dança religa o **irmão** do antebraço para trocar a mão (docs/remake-visual.md, "Mãos com dedos"): no
overlay do personagem (DBI_ no fim do container de texturas 87 + 2k, carregado em 0x801CC510 no 1P; o do figurino
alternativo em 159 + 2k) cada troca é um bloco
    lbu a0, F(..)   ; índice do antebraço (tabela no próprio overlay)
    ...  × 0xB0     ; nó = vetor de nós + índice × 0xB0
    lbu a0, H(..)   ; índice da mão
    ...
    sw  v0, 0x98(v1); antebraço.irmão = mão
Este script desmonta o overlay, acha cada `sw *, 0x98(*)` e os dois `lbu` de tabela antes dele, lê os bytes da
tabela e devolve, por antebraço, as mãos que o jogo liga ali. A posição de uma mão alternativa na árvore desenhada é a
do nó desenhado ligado ao mesmo antebraço (o padrão). Também lista trocas em que o índice vem de outro lugar (não
resolvidas aqui).
Uso: python tools/remake/hand_table.py [NOME[*] ...]   (padrão: os 18 nos dois figurinos)
"""
from __future__ import annotations

import os
import struct
import sys


HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from bag2model import CHARS, read_container  # noqa: E402

DISC = os.path.join(HERE, '..', '..', 'disc', 'Bust A Groove 2 (USA).bin')
LOAD_P1 = 0x801CC510


def overlay(k: int, alt: bool) -> bytes | None:
    for e in reversed(read_container(DISC, (159 if alt else 87) + 2 * k)):
        if e[:4] == b'DBI_':
            return e[12:12 + struct.unpack_from('<I', e, 4)[0]]
    return None


REGS = ['zero', 'at', 'v0', 'v1', 'a0', 'a1', 'a2', 'a3'] + [f't{i}' for i in range(8)] +        [f's{i}' for i in range(8)] + ['t8', 't9', 'k0', 'k1', 'gp', 'sp', 'fp', 'ra']


def relinks(code: bytes, base: int = LOAD_P1):
    """Trocas de mão do overlay: [(endereço do antebraço na tabela, endereço da mão, endereço do sw)], os blocos de
    inicialização da tabela ([{endereço: byte}], um por figurino: o overlay serve os dois e escolhe na carga) e os sw
    em +0x98 sem tabela reconhecida. Decodificação manual (o overlay mistura código e dados) com fluxo de
    registradores: o byte lido por `lbu` de uma tabela absoluta (lui + deslocamento) contamina o registrador e passa
    adiante pelas contas (sll, addu, subu, addiu); no `sw rt, 0x98(rs)` o antebraço é o byte de `rs` (o nó cujo campo é
    escrito) e a mão o de `rt` (o nó gravado)."""
    words = struct.unpack_from(f'<{len(code) // 4}I', code)
    hi, const, taint = {}, {}, {}  # registrador -> lui / constante (addiu/ori de zero) / endereço da tabela de origem
    inits = []                     # [(endereço da instrução, endereço escrito, byte)]
    sites, unknown = [], []
    for n, w in enumerate(words):
        a = base + 4 * n
        op, rs, rt, imm = w >> 26, (w >> 21) & 31, (w >> 16) & 31, w & 0xFFFF
        rd, fn = (w >> 11) & 31, w & 63
        simm = imm - 0x10000 if imm >= 0x8000 else imm
        if op == 0x0F:                                  # lui
            hi[rt] = imm << 16; const.pop(rt, None); taint.pop(rt, None)
        elif op == 0x09:                                # addiu
            if rs == 0:
                const[rt] = simm & 0xFF; hi.pop(rt, None); taint.pop(rt, None)
            else:
                t = taint.get(rs); hi.pop(rt, None); const.pop(rt, None)
                if t is None: taint.pop(rt, None)
                else: taint[rt] = t
        elif op == 0x0D and rs == 0:                    # ori rt, zero, imm
            const[rt] = imm & 0xFF; hi.pop(rt, None); taint.pop(rt, None)
        elif op == 0x24:                                # lbu rt, imm(rs)
            if rs in hi:
                taint[rt] = hi[rs] + simm
            else:
                taint.pop(rt, None)
            hi.pop(rt, None); const.pop(rt, None)
        elif op == 0x28 and rs in hi and rt in const:   # sb const, imm(rs)
            inits.append((a, hi[rs] + simm, const[rt]))
        elif op == 0x2B and imm == 0x98:                # sw rt, 0x98(rs)
            if rs in taint and rt in taint:
                sites.append((taint[rs], taint[rt], a))
            else:
                unknown.append(a)
        elif op == 0 and rd:                            # tipo R: sll/srl/sra (rt), addu/subu/or (rs, rt)
            if fn in (0, 2, 3):
                src = [rt]
            elif fn in (0x21, 0x23, 0x25):
                src = [rs, rt]
            else:
                src = []
            t = next((taint[r] for r in src if r in taint), None)
            hi.pop(rd, None); const.pop(rd, None)
            if t is None: taint.pop(rd, None)
            else: taint[rd] = t
        elif op in (0x23, 0x21, 0x25, 0x20):            # outras cargas: o registrador perde a origem
            hi.pop(rt, None); const.pop(rt, None); taint.pop(rt, None)
    blocks, cur, last = [], {}, None
    for a, addr, v in inits:   # sequências de sb de constantes (até 8 instruções entre elas) = um bloco
        if last is not None and a - last > 32 and cur:
            blocks.append(cur); cur = {}
        cur[addr] = v; last = a
    if cur:
        blocks.append(cur)
    return sites, blocks, unknown


def resolve(sites, blocks, code: bytes, base: int = LOAD_P1):
    """Por bloco de inicialização: {antebraço: {mãos}} com os bytes da tabela (do bloco, senão do arquivo)."""
    out = []
    for b in blocks + [{}]:
        def byte_at(addr):
            o = addr - base
            return b.get(addr, code[o] if 0 <= o < len(code) else None)
        t = {}
        for f, h, _ in sites:
            fv, hv = byte_at(f), byte_at(h)
            if fv is not None and hv is not None and (f in b or h in b or not b):
                t.setdefault(fv, set()).add(hv)
        if t and t not in out:
            out.append(t)
    return out


def hand_groups(name: str):
    """{antebraço: [mãos]} do figurino `name` (NOME ou NOME*): o bloco da tabela em que cada grupo tem exatamente um
    nó desenhado (a mão padrão) e só nós com mesh. None se o overlay não religa mãos por tabela."""
    from bag2model import load_character
    from export_char import drawn_and_alts
    alt = name.endswith('*')
    k = CHARS.index(name.rstrip('*'))
    code = overlay(k, alt)
    if code is None:
        return None
    sites, blocks, _ = relinks(code)
    nodes = load_character(DISC, k, alt)[0]
    drawn = set(drawn_and_alts(name, nodes)[0])
    best = None
    for t in resolve(sites, blocks, code):
        ok = all(h < len(nodes) and nodes[h].mesh for hs in t.values() for h in hs) and              all(len(hs & drawn) == 1 for hs in t.values())
        if ok and (best is None or sum(map(len, t.values())) > sum(map(len, best.values()))):
            best = t
    return {f: sorted(hs) for f, hs in best.items()} if best else None


def main():
    names = sys.argv[1:] or [n + s for s in ('', '*') for n in CHARS]
    for name in names:
        alt = name.endswith('*')
        k = CHARS.index(name.rstrip('*'))
        code = overlay(k, alt)
        if code is None:
            print(f'{name}: sem overlay DBI_')
            continue
        sites, blocks, unk = relinks(code)
        g = hand_groups(name)
        cands = resolve(sites, blocks, code)
        print(f'{name}: ' + ('; '.join(f'antebraço {f}: mãos {hs}' for f, hs in sorted(g.items())) if g else
                             f'sem tabela que bata com o modelo ({len(sites)} trocas, candidatas {cands})') +
              (f'  (sw +0x98 sem lbu de tabela: {", ".join(hex(a) for a in unk)})' if unk else ''), flush=True)


if __name__ == '__main__':
    main()
