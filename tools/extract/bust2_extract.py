#!/usr/bin/env python3
"""Extract files from a Bust A Groove 2 (USA) disc image (.bin, MODE2/2352).

Reads the ISO9660 tree directly from the raw image, dumps every top-level file,
then recursively unpacks the BUST2.BIN archive ("DF2_" root, nested "DFS_"
containers) and labels leaf blobs by their magic.

Usage: python tools/extract/bust2_extract.py "<path to .bin>" <out dir>
Game data stays on your disk; the output directory is git-ignored.
"""
import os, struct, sys, collections, json
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from bpe import bpe_decode

SEC = 2352
HDR = 24  # sync(12) + header(4) + XA subheader(8)


class Disc:
    def __init__(self, path):
        self.f = open(path, 'rb')

    def sector(self, lba):
        self.f.seek(lba * SEC + HDR)
        return self.f.read(2048)

    def read(self, lba, size):
        n = (size + 2047) // 2048
        return b''.join(self.sector(lba + i) for i in range(n))[:size]

    def raw_sectors(self, lba, count):
        """Full 2352-byte sectors (needed for XA/STR streams)."""
        self.f.seek(lba * SEC)
        return self.f.read(count * SEC)

    def listdir(self, lba, length, prefix=''):
        data = self.read(lba, length)
        off = 0
        out = []
        while off < len(data):
            l = data[off]
            if l == 0:
                off = (off // 2048 + 1) * 2048
                continue
            rec = data[off:off + l]
            elba = struct.unpack_from('<I', rec, 2)[0]
            esz = struct.unpack_from('<I', rec, 10)[0]
            flags = rec[25]
            nl = rec[32]
            name = rec[33:33 + nl]
            if name not in (b'\x00', b'\x01'):
                nm = name.decode().split(';')[0]
                full = prefix + nm
                if flags & 2:
                    out += self.listdir(elba, esz, full + '/')
                else:
                    out.append((full, elba, esz))
            off += l
        return out

    def root(self):
        pvd = self.sector(16)
        assert pvd[1:6] == b'CD001', 'not an ISO9660 image'
        lba = struct.unpack_from('<I', pvd, 156 + 2)[0]
        ln = struct.unpack_from('<I', pvd, 156 + 10)[0]
        return self.listdir(lba, ln)


def classify(b):
    if len(b) < 8:
        return 'bin'
    m = b[:4]
    if m == b'DF2_' or m == b'DFS_':
        return 'dfs'
    if m == b'DBI_':
        return 'dbi'
    if m == b'BPE_':
        return 'bpe'
    if m == b'SEDT':
        return 'sedt'
    if m == b'pBAV':
        return 'vab'
    if m == b'pQES':
        return 'seq'
    if m == b'VAGp':
        return 'vag'
    if m == b'\x10\x00\x00\x00' and b[4] in (0, 1, 2, 3, 8, 9, 10, 11):
        return 'tim'
    if m == b'\x41\x00\x00\x00':
        return 'tmd'
    if m == b'\x50\x00\x00\x00':
        return 'hmd'
    return 'bin'


def parse_table(b):
    """DF2_/DFS_: magic, count, then count x (offset, size) relative to container."""
    count = struct.unpack_from('<I', b, 4)[0]
    tab = [struct.unpack_from('<II', b, 8 + i * 8) for i in range(count)]
    return tab


stats = collections.Counter()


def unpack(blob, outdir, path, manifest, depth=0):
    kind = classify(blob)
    if kind == 'bpe':
        blob = bpe_decode(blob)
        kind = classify(blob)
        path += '.bpe'
    if kind == 'dfs' and depth < 8:
        tab = parse_table(blob)
        os.makedirs(outdir, exist_ok=True)
        for i, (off, size) in enumerate(tab):
            if off + size > len(blob):
                manifest.append({'path': f'{path}/{i:03d}', 'kind': 'BAD', 'off': off, 'size': size})
                stats['BAD'] += 1
                continue
            child = blob[off:off + size]
            ck = classify(child)
            if ck == 'bpe':
                child = bpe_decode(child)
                ck = classify(child)
                stats['bpe-decoded'] += 1
            if ck == 'dfs':
                unpack(child, os.path.join(outdir, f'{i:03d}'), f'{path}/{i:03d}', manifest, depth + 1)
            else:
                fn = f'{i:03d}.{ck}'
                with open(os.path.join(outdir, fn), 'wb') as o:
                    o.write(child)
                manifest.append({'path': f'{path}/{fn}', 'kind': ck, 'size': size})
                stats[ck] += 1
    else:
        os.makedirs(os.path.dirname(outdir) or '.', exist_ok=True)
        with open(outdir + '.' + kind, 'wb') as o:
            o.write(blob)
        manifest.append({'path': path + '.' + kind, 'kind': kind, 'size': len(blob)})
        stats[kind] += 1


def main():
    if len(sys.argv) != 3:
        print(__doc__)
        sys.exit(1)
    disc = Disc(sys.argv[1])
    out = sys.argv[2]
    os.makedirs(os.path.join(out, 'disc'), exist_ok=True)
    manifest = []
    for name, lba, size in disc.root():
        dst = os.path.join(out, 'disc', name)
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        if name.upper().endswith(('.XA', '.STR')):
            # streaming data: keep raw 2352-byte sectors (subheaders carry channel/coding info)
            data = disc.raw_sectors(lba, (size + 2047) // 2048)
            with open(dst + '.raw', 'wb') as o:
                o.write(data)
            print(f'{name:<24} lba={lba:<7} {size:>10}  (raw sectors)')
            continue
        data = disc.read(lba, size)
        with open(dst, 'wb') as o:
            o.write(data)
        print(f'{name:<24} lba={lba:<7} {size:>10}')
        if name.upper() == 'BUST2.BIN':
            unpack(data, os.path.join(out, 'bust2'), 'bust2', manifest)
    with open(os.path.join(out, 'manifest.json'), 'w') as o:
        json.dump(manifest, o, indent=1)
    print('leaf kinds:', dict(stats))


if __name__ == '__main__':
    main()
