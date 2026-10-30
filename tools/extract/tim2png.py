#!/usr/bin/env python3
"""Convert PS1 TIM textures to PNG (no Pillow needed).
Usage: tim2png.py <file.tim | dir> <outdir>   (dir mode converts every *.tim recursively)
4/8-bit TIMs use CLUT 0; 16-bit uses direct ABGR1555. STP bit -> alpha (semi-transparent marker kept opaque).
"""
import os, struct, sys, zlib

def png_write(path, w, h, rgba):
    def chunk(t, d):
        return struct.pack('>I', len(d)) + t + d + struct.pack('>I', zlib.crc32(t + d) & 0xffffffff)
    raw = b''.join(b'\x00' + rgba[y*w*4:(y+1)*w*4] for y in range(h))
    with open(path, 'wb') as f:
        f.write(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 6, 0, 0, 0))
                + chunk(b'IDAT', zlib.compress(raw, 6)) + chunk(b'IEND', b''))

def c16(v):
    r = (v & 31) << 3; g = ((v >> 5) & 31) << 3; b = ((v >> 10) & 31) << 3; stp = v >> 15
    a = 0 if (v & 0x7fff) == 0 and not stp else 255
    return bytes((r | r >> 5, g | g >> 5, b | b >> 5, a))

def tim_decode(b, clut_index=0):
    assert b[:4] == b'\x10\x00\x00\x00', 'not a TIM'
    flags = struct.unpack_from('<I', b, 4)[0]; bpp = flags & 3; off = 8
    pal = None
    if flags & 8:
        cl, cx, cy, cw, ch = struct.unpack_from('<IHHHH', b, off)
        pal_words = struct.unpack_from(f'<{cw*ch}H', b, off + 12)
        row = pal_words[clut_index*cw:(clut_index+1)*cw] if clut_index < ch else pal_words[:cw]
        pal = [c16(v) for v in row]
        off += cl
    bl, x, y, w, h = struct.unpack_from('<IHHHH', b, off); off += 12
    px = b[off:off + w*h*2]
    out = bytearray()
    if bpp == 0:
        W = w * 4
        for v in px:
            out += pal[v & 15] + pal[v >> 4]
    elif bpp == 1:
        W = w * 2
        for v in px:
            out += pal[v]
    elif bpp == 2:
        W = w
        for v in struct.unpack(f'<{w*h}H', px):
            out += c16(v)
    else:
        W = (w * 2) // 3
        for i in range(0, len(px), 3):
            out += px[i:i+3] + b'\xff'
    return W, h, bytes(out), (x, y), (bpp, flags)

def main():
    src, dst = sys.argv[1], sys.argv[2]
    files = []
    if os.path.isdir(src):
        for r, _, fs in os.walk(src):
            files += [os.path.join(r, f) for f in fs if f.endswith('.tim')]
    else:
        files = [src]
    os.makedirs(dst, exist_ok=True)
    n = 0
    for f in files:
        try:
            w, h, rgba, vram, _ = tim_decode(open(f, 'rb').read())
            if w == 0 or h == 0: continue
            name = os.path.relpath(f, src if os.path.isdir(src) else os.path.dirname(src)).replace(os.sep, '_')
            png_write(os.path.join(dst, name[:-4] + f'_{vram[0]}x{vram[1]}.png'), w, h, rgba)
            n += 1
        except Exception as e:
            print('skip', f, e)
    print('converted', n, 'of', len(files))

if __name__ == '__main__':
    main()
