#!/usr/bin/env python3
"""Byte Pair Encoding decoder, ported from SLUS_011.59 @ 0x80035DC4.
Stream: 'BPE_' + u32 big-endian total size (incl. 8-byte header), then blocks:
  pair table (Gage 1994 style): c=0; while c<256: n=byte; if n>127: c+=n-127; if c==256 break; n=0
      for i in range(n+1): left[c]=byte; if left[c]!=c: right[c]=byte; c+=1
  u16 big-endian count; count bytes of codes, expanded via a stack.
Usage: bpe.py in.bpe out.bin
"""
import struct, sys

def bpe_decode(src: bytes) -> bytes:
    assert src[:4] == b'BPE_', 'not BPE_'
    end = struct.unpack('>I', src[4:8])[0]
    p = 8
    out = bytearray()
    left = bytearray(256); right = bytearray(256)
    while p < end:
        for i in range(256):
            left[i] = i
        c = 0
        while c < 256:
            n = src[p]; p += 1
            if n > 127:
                c += n - 127
                if c == 256:
                    break
                n = 0
            for _ in range(n + 1):
                left[c] = src[p]; p += 1
                if left[c] != c:
                    right[c] = src[p]; p += 1
                c += 1
        count = (src[p] << 8) | src[p + 1]; p += 2
        stack = []
        while True:
            if stack:
                ch = stack.pop()
            else:
                if count == 0:
                    break
                count -= 1
                ch = src[p]; p += 1
            if left[ch] == ch:
                out.append(ch)
            else:
                stack.append(right[ch]); stack.append(left[ch])
    return bytes(out)

if __name__ == '__main__':
    data = open(sys.argv[1], 'rb').read()
    dec = bpe_decode(data)
    open(sys.argv[2], 'wb').write(dec)
    print(f'{len(data)} -> {len(dec)} bytes, head: {dec[:16].hex(" ")}')
