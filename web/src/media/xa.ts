/**
 * CD-XA ADPCM audio decoder (PlayStation / CD-ROM XA "Form 2" audio sectors).
 *
 * Input is a byte buffer of raw CD sectors, either 2352 bytes each
 * (sync + header + subheader + data + EDC, as produced by a MODE2/2352 .bin
 * rip) or 2336 bytes each (subheader + data, no sync/header). The format is
 * auto-detected from the first bytes.
 *
 * Sector layout (2352-byte form):
 *   0x000  12  sync (00 FF*10 00)
 *   0x00C   4  minute/second/frame/mode (BCD)
 *   0x010   8  XA subheader: file, channel, submode, codinginfo, then a copy
 *   0x018 2304 18 sound groups x 128 bytes
 *   0x918  20  zero padding
 *   0x92C   4  EDC
 *
 * Submode bits: 0 EOR, 1 video, 2 audio, 3 data, 4 trigger, 5 form2, 6 realtime, 7 EOF.
 * Codinginfo bits: 0-1 stereo, 2-3 rate (0=37800, 1=18900), 4-5 bits (0=4, 1=8), 6 emphasis.
 *
 * Each 128-byte sound group has a 16-byte header of sound parameters followed
 * by 28 little-endian 32-bit "data words"; the 4-bit (or 8-bit) samples of the
 * 8 (or 4) blocks of the group are interleaved inside those words. In stereo,
 * even blocks are left and odd blocks are right. Every block carries its own
 * shift/filter parameter (header byte 4+blockIndex, with copies at 0..3 and
 * 12..15 for error recovery).
 *
 * Decoding (psx-spx "CDROM XA Audio ADPCM Compression"):
 *   shift  = param & 0x0F   (13..15 behave like 9)
 *   filter = (param >> 4) & 3
 *   s = (nibble << 12) >> shift  +  (old*K0[filter] + older*K1[filter] + 32) >> 6
 *   clamp to 16 bit; older = old; old = s
 * The filter history runs continuously across blocks and sectors of a channel.
 */

export interface XaChannelInfo {
  file: number;
  channel: number;
  stereo: boolean;
  /** Sample rate in Hz: 37800 or 18900. */
  rate: number;
  /** Bits per sample: 4 or 8. */
  bits: number;
  /** Number of audio sectors seen for this file/channel (up to and including EOF). */
  sectorCount: number;
  /** Index (in the input) of the first sector of this channel. */
  firstSector: number;
  /** True when a sector with the EOF submode bit was seen for this channel. */
  sawEof: boolean;
}

export interface XaPcm {
  rate: number;
  left: Float32Array;
  right: Float32Array;
  /** Number of sectors that were decoded. */
  sectorCount: number;
}

/** XA subheader as stored in bytes 16..19 of a 2352-byte sector. */
export interface XaSubheader {
  file: number;
  channel: number;
  submode: number;
  coding: number;
}

export const SECTOR_RAW = 2352;
export const SECTOR_NOSYNC = 2336;

export const SUBMODE_EOR = 0x01;
export const SUBMODE_VIDEO = 0x02;
export const SUBMODE_AUDIO = 0x04;
export const SUBMODE_DATA = 0x08;
export const SUBMODE_TRIGGER = 0x10;
export const SUBMODE_FORM2 = 0x20;
export const SUBMODE_REALTIME = 0x40;
export const SUBMODE_EOF = 0x80;

/** ADPCM prediction filter coefficients (XA uses only filters 0..3). */
const K0 = [0, 60, 115, 98];
const K1 = [0, 0, -52, -55];

const GROUPS_PER_SECTOR = 18;
const SAMPLES_PER_BLOCK = 28;

/**
 * Returns the sector stride for a buffer: 2352 when it starts with the CD sync
 * pattern, 2336 otherwise (sub-header-only sectors).
 */
export function detectSectorSize(buf: Uint8Array): number {
  if (buf.length >= 12 && buf[0] === 0 && buf[11] === 0) {
    let ok = true;
    for (let i = 1; i < 11; i++) if (buf[i] !== 0xff) { ok = false; break; }
    if (ok) return SECTOR_RAW;
  }
  return SECTOR_NOSYNC;
}

/** Offset of the 8-byte XA subheader inside one sector of the given size. */
export function subheaderOffset(sectorSize: number): number {
  return sectorSize === SECTOR_RAW ? 16 : 0;
}

export function readSubheader(buf: Uint8Array, sectorStart: number, sectorSize: number): XaSubheader {
  const o = sectorStart + subheaderOffset(sectorSize);
  return { file: buf[o], channel: buf[o + 1], submode: buf[o + 2], coding: buf[o + 3] };
}

/** A sector carries XA-ADPCM audio when the Audio submode bit is set. */
export function isAudioSector(sh: XaSubheader): boolean {
  return (sh.submode & SUBMODE_AUDIO) !== 0;
}

export function codingIsStereo(coding: number): boolean { return (coding & 3) === 1; }
export function codingRate(coding: number): number { return ((coding >> 2) & 3) === 1 ? 18900 : 37800; }
export function codingBits(coding: number): number { return ((coding >> 4) & 3) === 1 ? 8 : 4; }

/** PCM samples per channel produced by one sector for a given coding byte. */
export function samplesPerSector(coding: number): number {
  const blocks = codingBits(coding) === 8 ? 4 : 8;
  const perGroup = blocks * SAMPLES_PER_BLOCK;
  return (GROUPS_PER_SECTOR * perGroup) / (codingIsStereo(coding) ? 2 : 1);
}

/** Duration in seconds of `sectorCount` sectors of the given channel. */
export function xaDurationSeconds(info: { rate: number; stereo: boolean; bits: number; sectorCount: number }): number {
  const coding = (info.stereo ? 1 : 0) | (info.rate === 18900 ? 4 : 0) | (info.bits === 8 ? 16 : 0);
  return (info.sectorCount * samplesPerSector(coding)) / info.rate;
}

/**
 * Decodes the 2304 data bytes of XA-ADPCM sectors one at a time, keeping the
 * ADPCM filter history between calls. One instance per (file, channel).
 */
export class XaAdpcmDecoder {
  readonly stereo: boolean;
  readonly bits: number;
  readonly rate: number;
  /** Samples per channel per sector. */
  readonly samplesPerSector: number;
  private oldL = 0; private olderL = 0;
  private oldR = 0; private olderR = 0;

  constructor(coding: number) {
    this.stereo = codingIsStereo(coding);
    this.bits = codingBits(coding);
    this.rate = codingRate(coding);
    this.samplesPerSector = samplesPerSector(coding);
  }

  reset(): void { this.oldL = this.olderL = this.oldR = this.olderR = 0; }

  /**
   * Decodes one sector whose sound-group data starts at `buf[dataOff]`
   * (i.e. 24 bytes into a 2352-byte sector) and writes `samplesPerSector`
   * floats per channel at `out*[outOff]`. For mono, right receives a copy.
   */
  decodeSector(buf: Uint8Array, dataOff: number, left: Float32Array, right: Float32Array, outOff: number): void {
    const blocks = this.bits === 8 ? 4 : 8;
    let posL = outOff, posR = outOff;
    let oldL = this.oldL, olderL = this.olderL, oldR = this.oldR, olderR = this.olderR;

    for (let g = 0; g < GROUPS_PER_SECTOR; g++) {
      const base = dataOff + g * 128;
      for (let blk = 0; blk < blocks; blk++) {
        const param = buf[base + 4 + blk];
        let shift = param & 0x0f;
        if (shift > 12) shift = 9;
        const filter = (param >> 4) & 3;
        const f0 = K0[filter], f1 = K1[filter];
        const isRight = this.stereo && (blk & 1) === 1;
        let old = isRight ? oldR : oldL;
        let older = isRight ? olderR : olderL;
        const out = isRight ? right : left;
        let pos = isRight ? posR : posL;

        if (blocks === 8) {
          // 4-bit: the nibble of block `blk` lives in byte 16 + j*4 + (blk>>1), low nibble for even blocks.
          const byteOff = base + 16 + (blk >> 1);
          const nib = (blk & 1) * 4;
          for (let j = 0; j < SAMPLES_PER_BLOCK; j++) {
            const t = (((buf[byteOff + j * 4] >> nib) & 0x0f) << 28) >> 28; // sign-extend 4 bits
            let s = ((t << 12) >> shift) + ((old * f0 + older * f1 + 32) >> 6);
            if (s > 32767) s = 32767; else if (s < -32768) s = -32768;
            out[pos++] = s / 32768;
            older = old; old = s;
          }
        } else {
          // 8-bit: the byte of block `blk` lives in byte 16 + j*4 + blk.
          const byteOff = base + 16 + blk;
          for (let j = 0; j < SAMPLES_PER_BLOCK; j++) {
            const t = (buf[byteOff + j * 4] << 24) >> 24; // sign-extend 8 bits
            let s = ((t << 8) >> shift) + ((old * f0 + older * f1 + 32) >> 6);
            if (s > 32767) s = 32767; else if (s < -32768) s = -32768;
            out[pos++] = s / 32768;
            older = old; old = s;
          }
        }

        if (isRight) { oldR = old; olderR = older; posR = pos; }
        else { oldL = old; olderL = older; posL = pos; }
      }
    }
    if (!this.stereo) right.set(left.subarray(outOff, posL), outOff);
    this.oldL = oldL; this.olderL = olderL; this.oldR = oldR; this.olderR = olderR;
  }
}

/**
 * Streaming decoder for one (file, channel) pair: feed raw sectors in any
 * chunk size, get PCM back per call; `done` flips once the EOF sector of the
 * channel has been decoded. Partial sectors at the end of a chunk are kept
 * and completed by the next push.
 */
export class XaChannelStream {
  readonly file: number;
  readonly channel: number;
  /** Filled from the first matching sector. */
  info: XaChannelInfo | null = null;
  done = false;
  private dec: XaAdpcmDecoder | null = null;
  private sectorsSeen = 0;
  private pending: Uint8Array | null = null;
  private sectorSize = 0;

  constructor(file: number, channel: number) { this.file = file; this.channel = channel; }

  get rate(): number { return this.dec?.rate ?? 37800; }

  /** Consumes `chunk` (any length) and returns PCM for the matching sectors it contained. */
  push(chunk: Uint8Array): { left: Float32Array; right: Float32Array } {
    let buf = chunk;
    if (this.pending) {
      const merged = new Uint8Array(this.pending.length + chunk.length);
      merged.set(this.pending, 0); merged.set(chunk, this.pending.length);
      buf = merged; this.pending = null;
    }
    if (this.sectorSize === 0) this.sectorSize = detectSectorSize(buf);
    const ss = this.sectorSize;
    const n = Math.floor(buf.length / ss);
    const rest = buf.length - n * ss;
    if (rest > 0) this.pending = buf.slice(n * ss);

    // First pass: find matching sectors so the output can be allocated once.
    const matches: number[] = [];
    for (let i = 0; i < n && !this.done; i++) {
      const sh = readSubheader(buf, i * ss, ss);
      if (!isAudioSector(sh) || sh.file !== this.file || sh.channel !== this.channel) continue;
      if (!this.dec) {
        this.dec = new XaAdpcmDecoder(sh.coding);
        this.info = { file: sh.file, channel: sh.channel, stereo: this.dec.stereo, rate: this.dec.rate, bits: this.dec.bits, sectorCount: 0, firstSector: this.sectorsSeen + i, sawEof: false };
      }
      matches.push(i);
      if (sh.submode & SUBMODE_EOF) { this.done = true; this.info!.sawEof = true; }
    }
    this.sectorsSeen += n;
    const dec = this.dec;
    if (!dec || matches.length === 0) return { left: new Float32Array(0), right: new Float32Array(0) };
    const per = dec.samplesPerSector;
    const left = new Float32Array(per * matches.length);
    const right = new Float32Array(per * matches.length);
    const dataOff = subheaderOffset(ss) + 8;
    for (let k = 0; k < matches.length; k++) dec.decodeSector(buf, matches[k] * ss + dataOff, left, right, k * per);
    this.info!.sectorCount += matches.length;
    return { left, right };
  }
}

/**
 * Lists every (file, channel) pair carrying XA audio in the buffer, with its
 * coding parameters and sector count (sectors after a channel's EOF are ignored).
 */
export function listXaChannels(sectors: Uint8Array): XaChannelInfo[] {
  const ss = detectSectorSize(sectors);
  const n = Math.floor(sectors.length / ss);
  const map = new Map<number, XaChannelInfo>();
  for (let i = 0; i < n; i++) {
    const sh = readSubheader(sectors, i * ss, ss);
    if (!isAudioSector(sh)) continue;
    const key = (sh.file << 8) | sh.channel;
    let info = map.get(key);
    if (!info) {
      info = { file: sh.file, channel: sh.channel, stereo: codingIsStereo(sh.coding), rate: codingRate(sh.coding), bits: codingBits(sh.coding), sectorCount: 0, firstSector: i, sawEof: false };
      map.set(key, info);
    }
    if (info.sawEof) continue;
    info.sectorCount++;
    if (sh.submode & SUBMODE_EOF) info.sawEof = true;
  }
  return [...map.values()].sort((a, b) => a.file - b.file || a.channel - b.channel);
}

/** Decodes one file/channel from a buffer of raw sectors to float PCM (stops at the channel's EOF sector). */
export function decodeXa(sectors: Uint8Array, file: number, channel: number): XaPcm {
  const ss = detectSectorSize(sectors);
  const n = Math.floor(sectors.length / ss);
  const dataOff = subheaderOffset(ss) + 8;
  let dec: XaAdpcmDecoder | null = null;
  const matches: number[] = [];
  for (let i = 0; i < n; i++) {
    const sh = readSubheader(sectors, i * ss, ss);
    if (!isAudioSector(sh) || sh.file !== file || sh.channel !== channel) continue;
    if (!dec) dec = new XaAdpcmDecoder(sh.coding);
    matches.push(i);
    if (sh.submode & SUBMODE_EOF) break;
  }
  if (!dec) return { rate: 37800, left: new Float32Array(0), right: new Float32Array(0), sectorCount: 0 };
  const per = dec.samplesPerSector;
  const left = new Float32Array(per * matches.length);
  const right = new Float32Array(per * matches.length);
  for (let k = 0; k < matches.length; k++) dec.decodeSector(sectors, matches[k] * ss + dataOff, left, right, k * per);
  return { rate: dec.rate, left, right, sectorCount: matches.length };
}
