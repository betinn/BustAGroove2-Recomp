// PS1 TIM texture parser. Mirrors tools/extract/tim2png.py.
export interface TimClut { x: number; y: number; w: number; h: number; data: Uint16Array; }
export interface Tim {
  bpp: 4 | 8 | 16 | 24;
  clut?: TimClut;
  /** VRAM placement of the pixel block (x in 16-bit units, w in 16-bit units). */
  x: number; y: number; w: number; h: number;
  /** Raw 16-bit VRAM words (w*h). */
  data: Uint16Array;
}

export function parseTim(buf: ArrayBuffer, offset = 0): Tim {
  const dv = new DataView(buf, offset);
  if (dv.getUint32(0, true) !== 0x10) throw new Error('not a TIM');
  const flags = dv.getUint32(4, true);
  const bpp = ([4, 8, 16, 24] as const)[flags & 3];
  let p = 8;
  let clut: TimClut | undefined;
  if (flags & 8) {
    const len = dv.getUint32(p, true);
    const x = dv.getUint16(p + 4, true), y = dv.getUint16(p + 6, true);
    const w = dv.getUint16(p + 8, true), h = dv.getUint16(p + 10, true);
    clut = { x, y, w, h, data: new Uint16Array(buf.slice(offset + p + 12, offset + p + 12 + w * h * 2)) };
    p += len;
  }
  const x = dv.getUint16(p + 4, true), y = dv.getUint16(p + 6, true);
  const w = dv.getUint16(p + 8, true), h = dv.getUint16(p + 10, true);
  const data = new Uint16Array(buf.slice(offset + p + 12, offset + p + 12 + w * h * 2));
  return { bpp, clut, x, y, w, h, data };
}

/** Expand a 15-bit PS1 color (plus STP bit) to RGBA8. Color 0x0000 is transparent. */
export function psxColorToRgba(v: number, out: Uint8ClampedArray, o: number): void {
  const r = (v & 31) << 3, g = ((v >> 5) & 31) << 3, b = ((v >> 10) & 31) << 3;
  out[o] = r | (r >> 5); out[o + 1] = g | (g >> 5); out[o + 2] = b | (b >> 5);
  out[o + 3] = v === 0 ? 0 : 255;
}
