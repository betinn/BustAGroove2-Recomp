// Random-access reader over a raw MODE2/2352 disc image (File or URL with Range support),
// ISO9660 directory walk, and the BUST2.BIN "DF2_/DFS_" container format.
import { bpeDecode } from '../psx/bpe';

const SEC = 2352, HDR = 24;

export interface ByteSource { size: number; read(offset: number, length: number): Promise<Uint8Array>; }

export function fileSource(f: File): ByteSource {
  return { size: f.size, read: async (o, l) => new Uint8Array(await f.slice(o, o + l).arrayBuffer()) };
}
export async function urlSource(url: string): Promise<ByteSource> {
  const head = await fetch(url, { method: 'HEAD' });
  const size = +(head.headers.get('content-length') ?? 0);
  return {
    size,
    read: async (o, l) => {
      const r = await fetch(url, { headers: { Range: `bytes=${o}-${o + l - 1}` } });
      if (!r.ok) throw new Error(`range fetch failed ${r.status}`);
      return new Uint8Array(await r.arrayBuffer());
    },
  };
}

export interface IsoEntry { name: string; lba: number; size: number; }

export class Disc {
  constructor(readonly src: ByteSource) {}

  /** Read `size` bytes of user data starting at logical sector `lba` (+ byte offset within it). */
  async read(lba: number, size: number, skip = 0): Promise<Uint8Array> {
    lba += Math.floor(skip / 2048); skip %= 2048;
    const n = Math.ceil((skip + size) / 2048);
    const raw = await this.src.read(lba * SEC, n * SEC);
    const out = new Uint8Array(n * 2048);
    for (let i = 0; i < n; i++) out.set(raw.subarray(i * SEC + HDR, i * SEC + HDR + 2048), i * 2048);
    return out.subarray(skip, skip + size);
  }

  async rawSectors(lba: number, count: number): Promise<Uint8Array> { return this.src.read(lba * SEC, count * SEC); }

  async listRoot(): Promise<IsoEntry[]> {
    const pvd = await this.read(16, 2048);
    if (String.fromCharCode(...pvd.subarray(1, 6)) !== 'CD001') throw new Error('not ISO9660');
    const dv = new DataView(pvd.buffer, pvd.byteOffset);
    return this.listDir(dv.getUint32(156 + 2, true), dv.getUint32(156 + 10, true), '');
  }

  private async listDir(lba: number, len: number, prefix: string): Promise<IsoEntry[]> {
    const data = await this.read(lba, len);
    const dv = new DataView(data.buffer, data.byteOffset);
    const out: IsoEntry[] = [];
    let off = 0;
    while (off < data.length) {
      const l = data[off];
      if (l === 0) { off = (Math.floor(off / 2048) + 1) * 2048; continue; }
      const elba = dv.getUint32(off + 2, true), esz = dv.getUint32(off + 10, true), flags = data[off + 25];
      const nl = data[off + 32];
      const name = String.fromCharCode(...data.subarray(off + 33, off + 33 + nl)).split(';')[0];
      if (nl !== 1 || (data[off + 33] !== 0 && data[off + 33] !== 1)) {
        if (flags & 2) out.push(...await this.listDir(elba, esz, prefix + name + '/'));
        else out.push({ name: prefix + name, lba: elba, size: esz });
      }
      off += l;
    }
    return out;
  }
}

/** DF2_/DFS_ table: count + (offset,size)[]. */
export function dfsTable(b: Uint8Array): { off: number; size: number }[] {
  const dv = new DataView(b.buffer, b.byteOffset);
  const n = dv.getUint32(4, true);
  const t = [];
  for (let i = 0; i < n; i++) t.push({ off: dv.getUint32(8 + i * 8, true), size: dv.getUint32(12 + i * 8, true) });
  return t;
}
export const magic = (b: Uint8Array) => String.fromCharCode(b[0], b[1], b[2], b[3]);

/** BUST2.BIN: lazily reads top-level entries from disc; nested DFS_ are resolved in memory. */
export class Bust2Archive {
  private table?: { off: number; size: number }[];
  constructor(readonly disc: Disc, readonly lba: number) {}

  async entries() {
    if (!this.table) {
      const hdr = await this.disc.read(this.lba, 8);
      const n = new DataView(hdr.buffer, hdr.byteOffset).getUint32(4, true);
      this.table = dfsTable(await this.disc.read(this.lba, 8 + n * 8));
    }
    return this.table;
  }

  /** Top-level entry bytes (BPE_ decompressed transparently). */
  async entry(i: number): Promise<Uint8Array> {
    const t = (await this.entries())[i];
    let b = await this.disc.read(this.lba, t.size, t.off);
    if (magic(b) === 'BPE_') b = bpeDecode(b);
    return b;
  }

  /** Children of a DFS_ blob as sub-views. */
  static children(b: Uint8Array): Uint8Array[] {
    if (magic(b) !== 'DFS_' && magic(b) !== 'DF2_') return [];
    return dfsTable(b).map(t => {
      const c = b.subarray(t.off, t.off + t.size);
      return magic(c) === 'BPE_' ? bpeDecode(c) : c;
    });
  }
}
