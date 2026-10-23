#!/usr/bin/env python3
"""Dump frames of a PlayStation .STR movie (raw sectors) to PNG. Pure Python reference
implementation mirroring web/src/media/str.ts (BS v2/v3 Huffman -> MDEC dequant -> IDCT -> RGB).

Usage: str2png.py <movie.STR.raw> <outdir> [--start K] [--count N] [--step S] [--info]
Frames are numbered from 1 as in the STR headers. Uses the zlib PNG writer from tim2png.py.
"""
import argparse, math, os, struct, sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from tim2png import png_write  # noqa: E402

# ----------------------------------------------------------------------------- demux

def sector_size(buf):
    return 2352 if buf[:12] == b'\x00' + b'\xff' * 10 + b'\x00' else 2336

def demux(buf):
    ss = sector_size(buf); n = len(buf) // ss
    data_off = (16 if ss == 2352 else 0) + 8
    sub_off = 16 if ss == 2352 else 0
    frames, audio = [], []
    cur = None

    def flush():
        nonlocal cur
        if cur is None: return
        data = b''.join(cur['chunks'])
        want = (cur['size'] + 3) & ~3
        if 8 <= want < len(data): data = data[:want]
        mdec_size, _, q, ver = struct.unpack_from('<HHHH', data, 0) if len(data) >= 8 else (0, 0, 0, 0)
        frames.append(dict(frame_no=cur['no'], width=cur['w'], height=cur['h'], data=data, sector=cur['first'],
                           chunks=cur['count'], found=len(cur['chunks']), quant=q, version=ver, mdec_size=mdec_size))
        cur = None

    for i in range(n):
        base = i * ss
        sm = buf[base + sub_off + 2]
        if sm & 4:
            audio.append(i); continue
        h = base + data_off
        magic, typ, chunk, chunks, fno, fsize, w, hh = struct.unpack_from('<HHHHIIHH', buf, h)
        if magic != 0x160 or typ != 0x8001: continue
        if cur and (cur['no'] != fno or chunk == 0): flush()
        if cur is None: cur = dict(no=fno, chunks=[], first=i, count=chunks, size=fsize, w=w, h=hh)
        cur['chunks'].append(buf[h + 32:h + 32 + 2016])
        if chunk + 1 >= chunks: flush()
    flush()
    return frames, audio, n

# ----------------------------------------------------------------------------- bitstream

ZAGZIG = [0, 1, 8, 16, 9, 2, 3, 10, 17, 24, 32, 25, 18, 11, 4, 5, 12, 19, 26, 33, 40, 48, 41, 34,
          27, 20, 13, 6, 7, 14, 21, 28, 35, 42, 49, 56, 57, 50, 43, 36, 29, 22, 15, 23, 30, 37, 44, 51,
          58, 59, 52, 45, 38, 31, 39, 46, 53, 60, 61, 54, 47, 55, 62, 63]

QT = [2, 16, 16, 19, 16, 19, 22, 22, 22, 22, 22, 22, 26, 24, 26, 27, 27, 27, 26, 26, 26, 26, 27, 27,
      27, 29, 29, 29, 34, 34, 34, 29, 29, 29, 27, 27, 29, 29, 32, 32, 34, 34, 37, 38, 37, 35, 35, 34,
      35, 38, 38, 40, 40, 40, 48, 48, 46, 46, 56, 56, 58, 69, 69, 83]

AC_TABLE = [
    ('11', [0x0001]), ('011', [0x0401]), ('010', [0x0002, 0x0801]), ('0011', [0x1001, 0x0c01]), ('00101', [0x0003]),
    ('00100', [0x3401, 0x0006, 0x3001, 0x2c01, 0x0c02, 0x0403, 0x0005, 0x2801]),
    ('0001', [0x1c01, 0x1801, 0x0402, 0x1401]), ('00001', [0x0802, 0x2401, 0x0004, 0x2001]),
    ('0000001', [0x4001, 0x1402, 0x0007, 0x0803, 0x0404, 0x3c01, 0x3801, 0x1002]),
    ('00000001', [0x000b, 0x2002, 0x1003, 0x000a, 0x0804, 0x1c02, 0x5401, 0x5001,
                  0x0009, 0x4c01, 0x4801, 0x0405, 0x0c03, 0x0008, 0x1802, 0x4401]),
    ('000000001', [0x2802, 0x2402, 0x1403, 0x0c04, 0x0805, 0x0407, 0x0406, 0x000f,
                   0x000e, 0x000d, 0x000c, 0x6801, 0x6401, 0x6001, 0x5c01, 0x5801]),
    ('0000000001', [0x001f, 0x001e, 0x001d, 0x001c, 0x001b, 0x001a, 0x0019, 0x0018,
                    0x0017, 0x0016, 0x0015, 0x0014, 0x0013, 0x0012, 0x0011, 0x0010]),
    ('00000000001', [0x0028, 0x0027, 0x0026, 0x0025, 0x0024, 0x0023, 0x0022, 0x0021,
                     0x0020, 0x040e, 0x040d, 0x040c, 0x040b, 0x040a, 0x0409, 0x0408]),
    ('000000000001', [0x0412, 0x0411, 0x0410, 0x040f, 0x1803, 0x4002, 0x3c02, 0x3802,
                      0x3402, 0x3002, 0x2c02, 0x7c01, 0x7801, 0x7401, 0x7001, 0x6c01]),
]
LOOKUP_BITS = 17
EOB, ESC = 'eob', 'esc'

def build_ac_lookup():
    t = [None] * (1 << LOOKUP_BITS)
    def fill(prefix, extra, value):
        ln = len(prefix) + extra
        for idx in range(1 << extra):
            code = ((int(prefix, 2) << extra) | idx) << (LOOKUP_BITS - ln)
            v = (ln,) + value(idx)
            for k in range(1 << (LOOKUP_BITS - ln)):
                t[code + k] = v
    fill('10', 0, lambda i: (EOB, 0))
    fill('000001', 0, lambda i: (ESC, 0))
    for prefix, codes in AC_TABLE:
        ib = int(math.log2(len(codes)))
        def val(idx, codes=codes):
            m = codes[idx >> 1]; lvl = m & 0x3ff
            return (m >> 10, -lvl if idx & 1 else lvl)
        fill(prefix, ib + 1, val)
    return t

AC_LOOKUP = build_ac_lookup()

class Bits:
    def __init__(self, data, off):
        self.d = data; self.p = off; self.acc = 0; self.n = 0
    def _fill(self):
        while self.n <= 16:
            hw = self.d[self.p] | (self.d[self.p + 1] << 8) if self.p + 1 < len(self.d) else 0
            self.p += 2; self.acc = (self.acc << 16) | hw; self.n += 16
    def peek(self, n):
        if self.n < n: self._fill()
        return (self.acc >> (self.n - n)) & ((1 << n) - 1)
    def skip(self, n):
        self.n -= n; self.acc &= (1 << self.n) - 1
    def read(self, n):
        v = self.peek(n); self.skip(n); return v
    def exhausted(self):
        return self.p - (self.n >> 3) >= len(self.d)

def sign10(v):
    return v - 1024 if v & 0x200 else v

def dc_diff(br, size):
    if size == 0: return 0
    b = br.read(size)
    return b if b & (1 << (size - 1)) else b - ((1 << size) - 1)

def read_dc_luma(br):
    p3 = br.peek(3)
    if p3 >> 1 == 0: br.skip(2); size = 1
    elif p3 >> 1 == 1: br.skip(2); size = 2
    elif p3 == 4: br.skip(3); size = 0
    elif p3 == 5: br.skip(3); size = 3
    elif p3 == 6: br.skip(3); size = 4
    else:
        n = 3; br.skip(3)
        while n < 8 and br.peek(1) == 1: br.skip(1); n += 1
        br.skip(1); size = min(n + 1, 8)
    return dc_diff(br, size)

def read_dc_chroma(br):
    p2 = br.peek(2)
    if p2 < 3: br.skip(2); size = p2
    else:
        n = 2; br.skip(2)
        while n < 8 and br.peek(1) == 1: br.skip(1); n += 1
        br.skip(1); size = min(n + 1, 8)
    return dc_diff(br, size)

COS = [[(math.sqrt(0.5) if u == 0 else 1.0) / 2 * math.cos((2 * x + 1) * u * math.pi / 16) for u in range(8)] for x in range(8)]

def idct(F):
    if not any(F[1:]):
        return [F[0] / 8] * 64
    tmp = [0.0] * 64
    for v in range(8):
        row = F[v * 8:v * 8 + 8]
        for x in range(8):
            c = COS[x]; tmp[v * 8 + x] = sum(c[u] * row[u] for u in range(8))
    out = [0.0] * 64
    for y in range(8):
        c = COS[y]
        for x in range(8):
            out[y * 8 + x] = sum(c[v] * tmp[v * 8 + x] for v in range(8))
    return out

def clamp11(v):
    return 1023 if v > 1023 else -1024 if v < -1024 else v

def read_block(br, dc, qscale):
    coef = [0] * 64
    coef[0] = clamp11(dc * 2 if qscale == 0 else dc * QT[0])
    k = 0
    while True:
        e = AC_LOOKUP[br.peek(LOOKUP_BITS)]
        if e is None: return coef, False
        ln, run, level = e
        br.skip(ln)
        if run == EOB: return coef, True
        if run == ESC:
            run = br.read(6); level = sign10(br.read(10))
        k += run + 1
        if k > 63: return coef, True
        if qscale == 0:
            coef[k] = clamp11(level * 2)
        else:
            v = level * QT[k] * qscale + 4
            v = int(v / 8)  # truncate toward zero like C
            coef[ZAGZIG[k]] = clamp11(v)

def decode_frame(fr):
    data, w, h = fr['data'], fr['width'], fr['height']
    qscale, version = struct.unpack_from('<HH', data, 4)
    mbw, mbh = (w + 15) >> 4, (h + 15) >> 4
    rgba = bytearray(w * h * 4)
    br = Bits(data, 8)
    pred = [0, 0, 0]
    blocks = [None] * 6
    done = False
    for mbx in range(mbw):
        if done: break
        for mby in range(mbh):
            if done: break
            for b in range(6):
                if br.exhausted(): done = True; break
                kind = b if b < 2 else 2
                if version >= 3:
                    if b == 0 and br.peek(10) == 0x3ff: done = True; break
                    diff = read_dc_luma(br) if kind == 2 else read_dc_chroma(br)
                    dc = sign10((pred[kind] + diff * 4) & 0x3ff); pred[kind] = dc
                else:
                    dc = br.read(10)
                    if b == 0 and dc == 0x1ff: done = True; break
                    dc = sign10(dc)
                coef, ok = read_block(br, dc, qscale)
                blocks[b] = idct(coef)
                if not ok: done = True; break
            else:
                emit(rgba, w, h, mbx, mby, blocks)
    return w, h, bytes(rgba)

def emit(rgba, w, h, mbx, mby, B):
    x0, y0 = mbx * 16, mby * 16
    for y in range(16):
        py = y0 + y
        if py >= h: break
        for x in range(16):
            px = x0 + x
            if px >= w: break
            yb = B[2 + (2 if y >= 8 else 0) + (1 if x >= 8 else 0)]
            Y = yb[(y & 7) * 8 + (x & 7)]
            ci = (y >> 1) * 8 + (x >> 1)
            cr, cb = B[0][ci], B[1][ci]
            o = (py * w + px) * 4
            r = int(Y + 1.402 * cr + 128.5); g = int(Y - 0.3437 * cb - 0.7143 * cr + 128.5); bl = int(Y + 1.772 * cb + 128.5)
            rgba[o] = 0 if r < 0 else 255 if r > 255 else r
            rgba[o + 1] = 0 if g < 0 else 255 if g > 255 else g
            rgba[o + 2] = 0 if bl < 0 else 255 if bl > 255 else bl
            rgba[o + 3] = 255

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('src'); ap.add_argument('dst', nargs='?')
    ap.add_argument('--start', type=int, default=1); ap.add_argument('--count', type=int, default=3)
    ap.add_argument('--step', type=int, default=1); ap.add_argument('--info', action='store_true')
    a = ap.parse_args()
    buf = open(a.src, 'rb').read()
    frames, audio, n = demux(buf)
    sizes = [len(f['data']) for f in frames]
    vers = sorted({f['version'] for f in frames}); dims = sorted({(f['width'], f['height']) for f in frames})
    print(f'{n} sectors, {len(audio)} audio sectors, {len(frames)} frames, versions {vers}, sizes {dims}, '
          f'frame data {min(sizes)}..{max(sizes)} bytes (avg {sum(sizes)/len(sizes):.0f})')
    if a.info or not a.dst: return
    os.makedirs(a.dst, exist_ok=True)
    byno = {f['frame_no']: f for f in frames}
    for k in range(a.count):
        no = a.start + k * a.step
        fr = byno.get(no)
        if fr is None: print('no frame', no); continue
        w, h, rgba = decode_frame(fr)
        # crude image statistics: mean and std of the gray level
        g = rgba[0::4]; m = sum(g) / len(g); sd = math.sqrt(sum((v - m) ** 2 for v in g) / len(g))
        path = os.path.join(a.dst, f'frame_{no:04d}.png')
        png_write(path, w, h, rgba)
        print(f'{path}: {w}x{h} v{fr["version"]} q={fr["quant"]} {len(fr["data"])} bytes, mean R {m:.1f} std {sd:.1f}')

if __name__ == '__main__':
    main()
