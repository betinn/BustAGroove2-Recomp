// Emulated PS1 VRAM: 1024x512 x 16-bit. Textures are uploaded like LoadImage() does, and
// sampled through (tpage, clut, u, v) exactly as the GPU would.
import { type Tim, psxColorToRgba } from './tim';

export const VRAM_W = 1024, VRAM_H = 512;

export class Vram {
  readonly words = new Uint16Array(VRAM_W * VRAM_H);

  loadImage(x: number, y: number, w: number, h: number, src: Uint16Array): void {
    for (let j = 0; j < h; j++) {
      const dy = (y + j) & (VRAM_H - 1);
      for (let i = 0; i < w; i++) this.words[dy * VRAM_W + ((x + i) & (VRAM_W - 1))] = src[j * w + i];
    }
  }

  loadTim(t: Tim): void {
    if (t.clut) this.loadImage(t.clut.x, t.clut.y, t.clut.w, t.clut.h, t.clut.data);
    this.loadImage(t.x, t.y, t.w, t.h, t.data);
  }

  /** Texel fetch like the GPU: tpage = {x: 0..15 (*64), y: 0..1 (*256), bpp}, clut = {x (*16), y}. */
  texel(tpx: number, tpy: number, bpp: 4 | 8 | 16, clutX: number, clutY: number, u: number, v: number): number {
    const vy = (tpy + v) & (VRAM_H - 1);
    if (bpp === 16) return this.words[vy * VRAM_W + ((tpx + u) & (VRAM_W - 1))];
    if (bpp === 8) {
      const w = this.words[vy * VRAM_W + ((tpx + (u >> 1)) & (VRAM_W - 1))];
      const idx = (u & 1) ? w >> 8 : w & 0xff;
      return this.words[clutY * VRAM_W + ((clutX + idx) & (VRAM_W - 1))];
    }
    const w = this.words[vy * VRAM_W + ((tpx + (u >> 2)) & (VRAM_W - 1))];
    const idx = (w >> ((u & 3) * 4)) & 15;
    return this.words[clutY * VRAM_W + ((clutX + idx) & (VRAM_W - 1))];
  }

  /** Whole VRAM as RGBA8 (debug view). */
  toRgba(): Uint8ClampedArray<ArrayBuffer> {
    const out = new Uint8ClampedArray(new ArrayBuffer(VRAM_W * VRAM_H * 4));
    for (let i = 0; i < this.words.length; i++) psxColorToRgba(this.words[i], out, i * 4);
    return out;
  }
}

/** Decode PsyQ tpage id (from GetTPage) -> {x, y, bpp, abr}. */
export function decodeTPage(id: number) {
  return { x: (id & 15) * 64, y: ((id >> 4) & 1) * 256, abr: (id >> 5) & 3, bpp: ([4, 8, 16, 16] as const)[(id >> 7) & 3] };
}
/** Decode PsyQ clut id (from GetClut) -> {x, y}. */
export function decodeClut(id: number) {
  return { x: (id & 63) * 16, y: (id >> 6) & 511 };
}
