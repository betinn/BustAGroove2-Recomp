#!/usr/bin/env python3
"""CD-XA ADPCM -> WAV (pure Python, reference implementation mirroring web/src/media/xa.ts).

Usage:
  xa2wav.py <file.XA.raw> --list
  xa2wav.py <file.XA.raw> <out.wav> --file 1 --channel 0 [--seconds N]

Input: raw 2352-byte (or 2336-byte) MODE2 sectors. Prints duration, RMS and peak of the result.
"""
import argparse, math, struct, sys

K0 = (0, 60, 115, 98)
K1 = (0, 0, -52, -55)

def sector_size(buf):
    return 2352 if buf[:12] == b'\x00' + b'\xff' * 10 + b'\x00' else 2336

def subheader(buf, off, ss):
    o = off + (16 if ss == 2352 else 0)
    return buf[o], buf[o + 1], buf[o + 2], buf[o + 3]  # file, channel, submode, coding

def coding_info(ci):
    stereo = (ci & 3) == 1
    rate = 18900 if ((ci >> 2) & 3) == 1 else 37800
    bits = 8 if ((ci >> 4) & 3) == 1 else 4
    return stereo, rate, bits

def samples_per_sector(ci):
    stereo, _, bits = coding_info(ci)
    return 18 * 28 * (4 if bits == 8 else 8) // (2 if stereo else 1)

def list_channels(buf):
    ss = sector_size(buf); n = len(buf) // ss
    chans = {}
    for i in range(n):
        f, c, sm, ci = subheader(buf, i * ss, ss)
        if not sm & 4:
            continue
        e = chans.setdefault((f, c), {'coding': ci, 'count': 0, 'eof': False, 'first': i})
        if e['eof']:
            continue
        e['count'] += 1
        if sm & 0x80:
            e['eof'] = True
    return chans

class Decoder:
    def __init__(self, ci):
        self.stereo, self.rate, self.bits = coding_info(ci)
        self.hist = [[0, 0], [0, 0]]  # [old, older] for L, R

    def decode_sector(self, buf, off, left, right):
        blocks = 4 if self.bits == 8 else 8
        for g in range(18):
            base = off + g * 128
            for blk in range(blocks):
                p = buf[base + 4 + blk]
                shift = p & 15
                if shift > 12: shift = 9
                f0, f1 = K0[(p >> 4) & 3], K1[(p >> 4) & 3]
                ch = 1 if (self.stereo and blk & 1) else 0
                old, older = self.hist[ch]
                out = right if ch else left
                if blocks == 8:
                    bo = base + 16 + (blk >> 1); nib = (blk & 1) * 4
                    for j in range(28):
                        t = (buf[bo + j * 4] >> nib) & 15
                        if t >= 8: t -= 16
                        s = ((t << 12) >> shift) + ((old * f0 + older * f1 + 32) >> 6)
                        if s > 32767: s = 32767
                        elif s < -32768: s = -32768
                        out.append(s); older, old = old, s
                else:
                    bo = base + 16 + blk
                    for j in range(28):
                        t = buf[bo + j * 4]
                        if t >= 128: t -= 256
                        s = ((t << 8) >> shift) + ((old * f0 + older * f1 + 32) >> 6)
                        if s > 32767: s = 32767
                        elif s < -32768: s = -32768
                        out.append(s); older, old = old, s
                self.hist[ch] = [old, older]

def decode(buf, file_no, chan, max_sectors=None):
    ss = sector_size(buf); n = len(buf) // ss
    data_off = (16 if ss == 2352 else 0) + 8
    left, right = [], []
    dec = None; count = 0
    for i in range(n):
        f, c, sm, ci = subheader(buf, i * ss, ss)
        if not sm & 4 or f != file_no or c != chan:
            continue
        if dec is None: dec = Decoder(ci)
        dec.decode_sector(buf, i * ss + data_off, left, right)
        count += 1
        if sm & 0x80 or (max_sectors and count >= max_sectors):
            break
    if dec is None:
        raise SystemExit(f'no audio sectors for file {file_no} channel {chan}')
    if not dec.stereo: right = left
    return dec.rate, left, right, count

def write_wav(path, rate, left, right):
    frames = bytearray()
    frames += struct.pack(f'<{len(left) * 2}h', *[v for pair in zip(left, right) for v in pair])
    with open(path, 'wb') as f:
        f.write(b'RIFF' + struct.pack('<I', 36 + len(frames)) + b'WAVE')
        f.write(b'fmt ' + struct.pack('<IHHIIHH', 16, 1, 2, rate, rate * 4, 4, 16))
        f.write(b'data' + struct.pack('<I', len(frames)) + frames)

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('src'); ap.add_argument('dst', nargs='?')
    ap.add_argument('--list', action='store_true')
    ap.add_argument('--file', type=int, default=1); ap.add_argument('--channel', type=int, default=0)
    ap.add_argument('--seconds', type=float, default=0, help='decode only the first N seconds')
    a = ap.parse_args()
    buf = open(a.src, 'rb').read()
    if a.list or not a.dst:
        for (f, c), e in sorted(list_channels(buf).items()):
            st, rate, bits = coding_info(e['coding'])
            dur = e['count'] * samples_per_sector(e['coding']) / rate
            print(f"file {f} channel {c}: {'stereo' if st else 'mono'} {rate} Hz {bits}-bit, "
                  f"{e['count']} sectors = {dur:.1f} s ({dur/60:.2f} min), eof={e['eof']}, first sector {e['first']}")
        return
    max_sectors = None
    if a.seconds:
        chans = list_channels(buf); ci = chans[(a.file, a.channel)]['coding']
        _, rate, _ = coding_info(ci)
        max_sectors = math.ceil(a.seconds * rate / samples_per_sector(ci))
    rate, left, right, count = decode(buf, a.file, a.channel, max_sectors)
    write_wav(a.dst, rate, left, right)
    n = len(left)
    rms = math.sqrt(sum(v * v for v in left) / n) / 32768 if n else 0
    peak = max(max(abs(v) for v in left), max(abs(v) for v in right)) / 32768 if n else 0
    print(f'{a.dst}: {count} sectors, {n} samples/ch @ {rate} Hz = {n / rate:.2f} s, RMS {rms:.4f} ({20*math.log10(max(rms,1e-9)):.1f} dBFS), peak {peak:.4f}')

if __name__ == '__main__':
    main()
