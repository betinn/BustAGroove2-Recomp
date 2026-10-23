/**
 * PlayStation STR (streaming video) demuxer and MDEC/BS frame decoder.
 *
 * A .STR file is a sequence of raw CD sectors. Video sectors are MODE2/Form1
 * data sectors whose 2048-byte payload starts with a 32-byte STR header
 * followed by 2016 bytes of frame data; XA-ADPCM audio sectors are interleaved
 * between them (decode those with ./xa.ts).
 *
 * STR sector header (little endian, at byte 24 of a 2352-byte sector):
 *   0x00 u16 status      0x0160
 *   0x02 u16 type        0x8001 = MDEC video
 *   0x04 u16 chunk       sector number inside the frame (0 = first)
 *   0x06 u16 chunks      sectors per frame
 *   0x08 u32 frameNo     1-based
 *   0x0C u32 frameSize   bytes of BS data in the frame (headers/padding excluded)
 *   0x10 u16 width
 *   0x12 u16 height
 *   0x14 u16 mdecCodes   copy of the BS header word 0 (MDEC code count / 4... see below)
 *   0x16 u16 0x3800      copy of the BS header word 1
 *   0x18 u16 quantScale  copy of the BS header word 2
 *   0x1A u16 version     copy of the BS header word 3 (1, 2 or 3)
 *   0x1C u32 0
 *
 * BS frame (the concatenated chunk payloads):
 *   0x00 u16 MDEC size / 4 (size of the run-length data after Huffman expansion)
 *   0x02 u16 0x3800
 *   0x04 u16 quantization scale (0..63)
 *   0x06 u16 version
 *   0x08 ... Huffman bitstream, read as little-endian 16-bit halfwords, MSB first
 *
 * Bitstream: macroblocks are stored column by column (all macroblocks of the
 * leftmost 16-pixel column top to bottom, then the next column...). Each
 * macroblock holds six 8x8 blocks in the order Cr, Cb, Y1, Y2, Y3, Y4
 * (Y1 top-left, Y2 top-right, Y3 bottom-left, Y4 bottom-right). Each block is
 * a DC value followed by run/level AC codes and an end-of-block code.
 *   v1/v2: DC is a raw signed 10-bit value; DC == +0x1FF in a Cr slot = end of frame.
 *   v3:    DC is a Huffman-coded difference (times 4) from the previous DC of the
 *          same kind (Cr, Cb or Y); the 10-bit all-ones code = end of frame.
 * AC codes are the MPEG-1 intra VLC set (see AC_TABLE below), escape code
 * 000001 + 6-bit run + 10-bit signed level, EOB = "10".
 *
 * The run-length coefficients are dequantized like the MDEC hardware does
 * (DC = dc*qt[0]; AC = (level*qt[k]*scale + 4) / 8, clamped to -1024..1023),
 * then an 8x8 IDCT gives signed YCbCr samples which are converted to RGB with
 * R = Y + 1.402 Cr, G = Y - 0.3437 Cb - 0.7143 Cr, B = Y + 1.772 Cb (+128).
 */

import {
  SUBMODE_AUDIO, codingRate, detectSectorSize, readSubheader, samplesPerSector, subheaderOffset,
} from './xa.ts';

export interface StrFrame {
  /** 1-based frame number from the STR header. */
  frameNo: number;
  width: number;
  height: number;
  quantScale: number;
  version: number;
  /** BS header word 0: MDEC run-length data size in 32-bit words. */
  mdecSize: number;
  /** BS data including its 8-byte header. */
  data: Uint8Array;
  /** Index of the first sector of the frame inside the input buffer (for timing). */
  sectorIndex: number;
  /** Number of video sectors the frame was split into (as declared). */
  chunkCount: number;
  /** Number of video sectors actually found. */
  chunksFound: number;
}

export interface StrDemux {
  frames: StrFrame[];
  /** Concatenated raw audio sectors (same sector size as the input), ready for xa.ts. */
  audio: Uint8Array;
  /** Total sectors in the input. */
  sectorCount: number;
  /** Detected sector size (2352 or 2336). */
  sectorSize: number;
  /** Estimated disc read speed in sectors per second (75 or 150), derived from the audio interleave. */
  sectorsPerSecond: number;
  /** Estimated video frame rate. */
  fps: number;
}

export interface DecodedFrame {
  width: number;
  height: number;
  rgba: Uint8ClampedArray;
}

const STR_MAGIC = 0x0160;
const STR_TYPE_VIDEO = 0x8001;
const STR_HEADER = 32;
const STR_PAYLOAD = 2016;

/** Splits a buffer of raw sectors into video frames and audio sectors. */
export function demuxStr(sectors: Uint8Array): StrDemux {
  const ss = detectSectorSize(sectors);
  const n = Math.floor(sectors.length / ss);
  const dataOff = subheaderOffset(ss) + 8;
  const dv = new DataView(sectors.buffer, sectors.byteOffset, sectors.byteLength);

  const frames: StrFrame[] = [];
  const audioIdx: number[] = [];
  let audioCoding = -1;

  // Frame being assembled.
  let cur: { frameNo: number; chunks: Uint8Array[]; first: number; count: number; size: number; w: number; h: number } | null = null;

  const flush = () => {
    if (!cur) return;
    const total = cur.chunks.reduce((a, c) => a + c.length, 0);
    let data = new Uint8Array(total);
    let p = 0;
    for (const c of cur.chunks) { data.set(c, p); p += c.length; }
    // frameSize counts the BS bytes; keep a 4-byte aligned prefix when it is sane.
    const want = (cur.size + 3) & ~3;
    if (want >= 8 && want < total) data = data.subarray(0, want);
    const hdv = new DataView(data.buffer, data.byteOffset, data.byteLength);
    frames.push({
      frameNo: cur.frameNo, width: cur.w, height: cur.h,
      mdecSize: data.length >= 2 ? hdv.getUint16(0, true) : 0,
      quantScale: data.length >= 6 ? hdv.getUint16(4, true) : 0,
      version: data.length >= 8 ? hdv.getUint16(6, true) : 0,
      data, sectorIndex: cur.first, chunkCount: cur.count, chunksFound: cur.chunks.length,
    });
    cur = null;
  };

  for (let i = 0; i < n; i++) {
    const base = i * ss;
    const sh = readSubheader(sectors, base, ss);
    if (sh.submode & SUBMODE_AUDIO) {
      audioIdx.push(i);
      if (audioCoding < 0) audioCoding = sh.coding;
      continue;
    }
    const h = base + dataOff;
    if (h + STR_HEADER + STR_PAYLOAD > sectors.length) break;
    if (dv.getUint16(h, true) !== STR_MAGIC || dv.getUint16(h + 2, true) !== STR_TYPE_VIDEO) continue;
    const chunk = dv.getUint16(h + 4, true);
    const chunks = dv.getUint16(h + 6, true);
    const frameNo = dv.getUint32(h + 8, true);
    const frameSize = dv.getUint32(h + 12, true);
    const w = dv.getUint16(h + 16, true);
    const hgt = dv.getUint16(h + 18, true);
    if (cur && (cur.frameNo !== frameNo || chunk === 0)) flush();
    if (!cur) cur = { frameNo, chunks: [], first: i, count: chunks, size: frameSize, w, h: hgt };
    cur.chunks.push(sectors.subarray(h + STR_HEADER, h + STR_HEADER + STR_PAYLOAD));
    if (chunk + 1 >= chunks) flush();
  }
  flush();

  const audio = new Uint8Array(audioIdx.length * ss);
  for (let k = 0; k < audioIdx.length; k++) audio.set(sectors.subarray(audioIdx[k] * ss, audioIdx[k] * ss + ss), k * ss);

  // Disc speed: one audio sector every `spacing` sectors must deliver samplesPerSector/rate seconds.
  let sectorsPerSecond = 150;
  if (audioIdx.length >= 2 && audioCoding >= 0) {
    const spacing = (audioIdx[audioIdx.length - 1] - audioIdx[0]) / (audioIdx.length - 1);
    const secPerAudioSector = samplesPerSector(audioCoding) / codingRate(audioCoding);
    const est = spacing / secPerAudioSector;
    sectorsPerSecond = Math.abs(est - 75) < Math.abs(est - 150) ? 75 : 150;
  }
  let fps = 15;
  if (frames.length >= 2) {
    const span = frames[frames.length - 1].sectorIndex - frames[0].sectorIndex;
    fps = sectorsPerSecond / (span / (frames.length - 1));
  }
  return { frames, audio, sectorCount: n, sectorSize: ss, sectorsPerSecond, fps };
}

// ---------------------------------------------------------------------------
// MDEC / BS decoding
// ---------------------------------------------------------------------------

/** zigzag index -> raster index. */
const ZAGZIG = new Uint8Array([
  0, 1, 8, 16, 9, 2, 3, 10,
  17, 24, 32, 25, 18, 11, 4, 5,
  12, 19, 26, 33, 40, 48, 41, 34,
  27, 20, 13, 6, 7, 14, 21, 28,
  35, 42, 49, 56, 57, 50, 43, 36,
  29, 22, 15, 23, 30, 37, 44, 51,
  58, 59, 52, 45, 38, 31, 39, 46,
  53, 60, 61, 54, 47, 55, 62, 63,
]);

/**
 * Default MDEC quantization table in zigzag order (the PsyQ/MPEG-1 intra matrix
 * with DC weight 2). The same table is used for luma and chroma.
 */
const DEFAULT_QT = new Uint8Array([
  2, 16, 16, 19, 16, 19, 22, 22,
  22, 22, 22, 22, 26, 24, 26, 27,
  27, 27, 26, 26, 26, 26, 27, 27,
  27, 29, 29, 29, 34, 34, 34, 29,
  29, 29, 27, 27, 29, 29, 32, 32,
  34, 34, 37, 38, 37, 35, 35, 34,
  35, 38, 38, 40, 40, 40, 48, 48,
  46, 46, 56, 56, 58, 69, 69, 83,
]);

/**
 * AC Huffman codes for BS v1/v2/v3. Each entry: bit-string prefix (with the
 * "x" index bits and trailing sign bit removed), followed by the list of MDEC
 * halfwords (run << 10 | level) selected by the index bits, in order.
 * Taken from the psx-spx table "Huffman codes for AC values BS v1/v2/v3/iki".
 */
const AC_TABLE: [string, number[]][] = [
  ['11', [0x0001]],
  ['011', [0x0401]],
  ['010', [0x0002, 0x0801]],
  ['0011', [0x1001, 0x0c01]],
  ['00101', [0x0003]],
  ['00100', [0x3401, 0x0006, 0x3001, 0x2c01, 0x0c02, 0x0403, 0x0005, 0x2801]],
  ['0001', [0x1c01, 0x1801, 0x0402, 0x1401]],
  ['00001', [0x0802, 0x2401, 0x0004, 0x2001]],
  ['0000001', [0x4001, 0x1402, 0x0007, 0x0803, 0x0404, 0x3c01, 0x3801, 0x1002]],
  ['00000001', [0x000b, 0x2002, 0x1003, 0x000a, 0x0804, 0x1c02, 0x5401, 0x5001,
    0x0009, 0x4c01, 0x4801, 0x0405, 0x0c03, 0x0008, 0x1802, 0x4401]],
  ['000000001', [0x2802, 0x2402, 0x1403, 0x0c04, 0x0805, 0x0407, 0x0406, 0x000f,
    0x000e, 0x000d, 0x000c, 0x6801, 0x6401, 0x6001, 0x5c01, 0x5801]],
  ['0000000001', [0x001f, 0x001e, 0x001d, 0x001c, 0x001b, 0x001a, 0x0019, 0x0018,
    0x0017, 0x0016, 0x0015, 0x0014, 0x0013, 0x0012, 0x0011, 0x0010]],
  ['00000000001', [0x0028, 0x0027, 0x0026, 0x0025, 0x0024, 0x0023, 0x0022, 0x0021,
    0x0020, 0x040e, 0x040d, 0x040c, 0x040b, 0x040a, 0x0409, 0x0408]],
  ['000000000001', [0x0412, 0x0411, 0x0410, 0x040f, 0x1803, 0x4002, 0x3c02, 0x3802,
    0x3402, 0x3002, 0x2c02, 0x7c01, 0x7801, 0x7401, 0x7001, 0x6c01]],
];

const AC_LOOKUP_BITS = 17; // longest non-escape code: 12 prefix + 4 index + 1 sign
const AC_EOB = 1;          // marker values stored in the lookup table
const AC_ESCAPE = 2;
/** lookup[peek17] = (len << 20) | (run << 10) | (level & 0x3ff) | kind<<18... packed as below. */
const AC_LOOKUP = buildAcLookup();

function buildAcLookup(): Int32Array {
  // Packing: bits 0..9 level (two's complement 10 bit), bits 10..15 run, bits 16..17 kind, bits 20..24 length.
  const t = new Int32Array(1 << AC_LOOKUP_BITS);
  const fill = (prefix: string, extraBits: number, valueFor: (idx: number) => number, kind: number) => {
    const len = prefix.length + extraBits;
    const prefixVal = parseInt(prefix, 2);
    const span = 1 << (AC_LOOKUP_BITS - len);
    for (let idx = 0; idx < 1 << extraBits; idx++) {
      const code = ((prefixVal << extraBits) | idx) << (AC_LOOKUP_BITS - len);
      const v = valueFor(idx);
      const packed = (len << 20) | (kind << 16) | (v & 0xffff);
      for (let k = 0; k < span; k++) t[code + k] = packed;
    }
  };
  fill('10', 0, () => 0, AC_EOB);
  fill('000001', 0, () => 0, AC_ESCAPE);
  for (const [prefix, codes] of AC_TABLE) {
    const idxBits = Math.log2(codes.length);
    fill(prefix, idxBits + 1, (idx) => {
      const sign = idx & 1;
      const mdec = codes[idx >> 1];
      const run = mdec >> 10;
      let level = mdec & 0x3ff;
      if (sign) level = -level;
      return (run << 10) | (level & 0x3ff);
    }, 0);
  }
  return t;
}

/** MSB-first bit reader over little-endian 16-bit halfwords. Past the end it yields zeros. */
class BitReader {
  private buf: Uint8Array;
  private pos: number;
  private acc = 0;   // up to 32 valid bits, right-aligned
  private nbits = 0;
  constructor(buf: Uint8Array, offset: number) { this.buf = buf; this.pos = offset; }

  private refill(): void {
    while (this.nbits <= 16) {
      let hw = 0;
      if (this.pos + 1 < this.buf.length) hw = this.buf[this.pos] | (this.buf[this.pos + 1] << 8);
      this.pos += 2;
      this.acc = ((this.acc << 16) | hw) >>> 0;
      this.nbits += 16;
    }
  }
  /** Peeks n <= 24 bits. */
  peek(n: number): number {
    if (this.nbits < n) this.refill();
    return (this.acc >>> (this.nbits - n)) & ((1 << n) - 1);
  }
  skip(n: number): void {
    this.nbits -= n;
    this.acc = this.acc & ((1 << this.nbits) - 1) >>> 0;
  }
  read(n: number): number { const v = this.peek(n); this.skip(n); return v; }
  /** True when every remaining bit comes from past the end of the data. */
  exhausted(): boolean { return this.pos - (this.nbits >> 3) >= this.buf.length; }
}

const COS = buildCosTable();
function buildCosTable(): Float32Array {
  // c[x*8+u] = C(u)/2 * cos((2x+1) u pi / 16), with C(0) = 1/sqrt(2)
  const t = new Float32Array(64);
  for (let x = 0; x < 8; x++) for (let u = 0; u < 8; u++) {
    t[x * 8 + u] = (u === 0 ? Math.SQRT1_2 : 1) / 2 * Math.cos(((2 * x + 1) * u * Math.PI) / 16);
  }
  return t;
}

/**
 * Reusable MDEC frame decoder (keeps its scratch buffers between frames).
 */
export class MdecDecoder {
  private coef = new Int32Array(64);
  private tmp = new Float32Array(64);
  private blocks = new Float32Array(6 * 64); // Cr, Cb, Y1..Y4 of the current macroblock
  private qt: Uint8Array = DEFAULT_QT;

  /** Replaces the quantization table (64 entries in zigzag order). */
  setQuantTable(qt: Uint8Array): void { this.qt = qt; }

  /**
   * Decodes a BS frame (with its 8-byte header) into RGBA. `width`/`height`
   * are the visible size; decoding happens on the 16-aligned size.
   */
  decode(data: Uint8Array, width: number, height: number, out?: Uint8ClampedArray): DecodedFrame {
    const dv = new DataView(data.buffer, data.byteOffset, data.byteLength);
    const qscale = data.length >= 6 ? dv.getUint16(4, true) : 1;
    const version = data.length >= 8 ? dv.getUint16(6, true) : 2;
    const mbw = (width + 15) >> 4, mbh = (height + 15) >> 4;
    const rgba = out && out.length >= width * height * 4 ? out : new Uint8ClampedArray(width * height * 4);
    const br = new BitReader(data, 8);
    const dcPred = [0, 0, 0]; // Cr, Cb, Y predictors for v3

    outer:
    for (let mbx = 0; mbx < mbw; mbx++) {
      for (let mby = 0; mby < mbh; mby++) {
        for (let b = 0; b < 6; b++) {
          if (br.exhausted()) break outer;
          const kind = b < 2 ? b : 2;
          // --- DC ---
          let dc: number;
          if (version >= 3) {
            if (b === 0 && br.peek(10) === 0x3ff) break outer; // end of frame
            const diff = kind === 2 ? readDcLuma(br) : readDcChroma(br);
            dc = dcPred[kind] + diff * 4;
            dc = ((dc & 0x3ff) << 22) >> 22; // wrap to signed 10 bit like newer v3 decoders
            dcPred[kind] = dc;
          } else {
            dc = br.read(10);
            if (b === 0 && dc === 0x1ff) break outer; // end of frame
            dc = (dc << 22) >> 22;
          }
          // --- AC + IDCT ---
          const ok = this.readBlockAc(br, dc, qscale);
          this.idct(this.blocks, b * 64);
          if (!ok) break outer;
        }
        this.emitMacroblock(rgba, width, height, mbx, mby);
      }
    }
    return { width, height, rgba };
  }

  /** Reads AC coefficients into this.coef, dequantizes. Returns false on a bad code. */
  private readBlockAc(br: BitReader, dc: number, qscale: number): boolean {
    const coef = this.coef;
    const qt = this.qt;
    coef.fill(0);
    let v = qscale === 0 ? dc * 2 : dc * qt[0];
    if (v > 1023) v = 1023; else if (v < -1024) v = -1024;
    coef[0] = v;
    let k = 0;
    for (;;) {
      const e = AC_LOOKUP[br.peek(AC_LOOKUP_BITS)];
      const len = e >>> 20;
      if (len === 0) return false;
      const kind = (e >> 16) & 3;
      br.skip(len);
      if (kind === AC_EOB) return true;
      let run: number, level: number;
      if (kind === AC_ESCAPE) {
        run = br.read(6);
        level = (br.read(10) << 22) >> 22;
      } else {
        run = (e >> 10) & 0x3f;
        level = ((e & 0x3ff) << 22) >> 22;
      }
      k += run + 1;
      if (k > 63) return true; // tolerate overlong blocks like the hardware (ignores extra)
      if (qscale === 0) {
        let val = level * 2;
        if (val > 1023) val = 1023; else if (val < -1024) val = -1024;
        coef[k] = val;
      } else {
        let val = ((level * qt[k] * qscale + 4) / 8) | 0;
        if (val > 1023) val = 1023; else if (val < -1024) val = -1024;
        coef[ZAGZIG[k]] = val;
      }
    }
  }

  /** 8x8 separable float IDCT of this.coef into dst[dstOff..dstOff+64), rounded. */
  private idct(dst: Float32Array, dstOff: number): void {
    const F = this.coef, tmp = this.tmp;
    // Fast path: DC only.
    let onlyDc = true;
    for (let i = 1; i < 64; i++) if (F[i] !== 0) { onlyDc = false; break; }
    if (onlyDc) {
      const v = F[0] / 8;
      for (let i = 0; i < 64; i++) dst[dstOff + i] = v;
      return;
    }
    // Rows: tmp[v][x] = sum_u c[x][u] * F[v][u]
    for (let v = 0; v < 8; v++) {
      const row = v * 8;
      for (let x = 0; x < 8; x++) {
        let s = 0;
        const cx = x * 8;
        for (let u = 0; u < 8; u++) s += COS[cx + u] * F[row + u];
        tmp[row + x] = s;
      }
    }
    // Columns: out[y][x] = sum_v c[y][v] * tmp[v][x]
    for (let y = 0; y < 8; y++) {
      const cy = y * 8;
      for (let x = 0; x < 8; x++) {
        let s = 0;
        for (let v = 0; v < 8; v++) s += COS[cy + v] * tmp[v * 8 + x];
        dst[dstOff + y * 8 + x] = s;
      }
    }
  }

  /** Converts the six decoded blocks of one macroblock to RGBA pixels. */
  private emitMacroblock(rgba: Uint8ClampedArray, width: number, height: number, mbx: number, mby: number): void {
    const B = this.blocks;
    const x0 = mbx * 16, y0 = mby * 16;
    for (let y = 0; y < 16; y++) {
      const py = y0 + y;
      if (py >= height) break;
      const yBlk = (y >= 8 ? 2 : 0);
      for (let x = 0; x < 16; x++) {
        const px = x0 + x;
        if (px >= width) break;
        const yb = (yBlk + (x >= 8 ? 1 : 0) + 2) * 64;
        const Y = B[yb + (y & 7) * 8 + (x & 7)];
        const ci = (y >> 1) * 8 + (x >> 1);
        const cr = B[ci];
        const cb = B[64 + ci];
        const o = (py * width + px) * 4;
        rgba[o] = Y + 1.402 * cr + 128.5;
        rgba[o + 1] = Y - 0.3437 * cb - 0.7143 * cr + 128.5;
        rgba[o + 2] = Y + 1.772 * cb + 128.5;
        rgba[o + 3] = 255;
      }
    }
  }
}

/** v3 DC size code for Y blocks: returns the differential value (not yet *4). */
function readDcLuma(br: BitReader): number {
  // 100 -> 0 ; 00 -> 1 ; 01 -> 2 ; 101 -> 3 ; 110 -> 4 ; 1110 -> 5 ; 11110 -> 6 ; 111110 -> 7 ; 1111110 -> 8
  let size: number;
  const p3 = br.peek(3);
  if (p3 >> 1 === 0) { size = 1; br.skip(2); }
  else if (p3 >> 1 === 1) { size = 2; br.skip(2); }
  else if (p3 === 4) { size = 0; br.skip(3); }
  else if (p3 === 5) { size = 3; br.skip(3); }
  else if (p3 === 6) { size = 4; br.skip(3); }
  else {
    // 1110.. : count ones from the 4th bit on
    let n = 3;
    br.skip(3);
    while (n < 8 && br.peek(1) === 1) { br.skip(1); n++; }
    br.skip(1); // the terminating 0 (absent only for the unused all-ones code)
    size = n + 1; // 1110 -> 5 ... 1111110 -> 8
    if (size > 8) size = 8;
  }
  return dcDifferential(br, size);
}

/** v3 DC size code for Cr/Cb blocks. */
function readDcChroma(br: BitReader): number {
  // 00 -> 0 ; 01 -> 1 ; 10 -> 2 ; 110 -> 3 ; 1110 -> 4 ; ... ; 11111110 -> 8
  const p2 = br.peek(2);
  let size: number;
  if (p2 < 3) { size = p2; br.skip(2); }
  else {
    let n = 2;
    br.skip(2);
    while (n < 8 && br.peek(1) === 1) { br.skip(1); n++; }
    br.skip(1);
    size = n + 1;
    if (size > 8) size = 8;
  }
  return dcDifferential(br, size);
}

/** Reads `size` magnitude bits of a DC difference: leading 0 means negative (MPEG-style). */
function dcDifferential(br: BitReader, size: number): number {
  if (size === 0) return 0;
  const bits = br.read(size);
  if (bits & (1 << (size - 1))) return bits;
  return bits - ((1 << size) - 1);
}

const sharedDecoder = new MdecDecoder();

/** Decodes one demuxed STR frame to RGBA (convenience wrapper around MdecDecoder). */
export function decodeFrame(frame: StrFrame, out?: Uint8ClampedArray): DecodedFrame {
  return sharedDecoder.decode(frame.data, frame.width, frame.height, out);
}
