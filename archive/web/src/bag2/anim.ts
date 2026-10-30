// Bust A Groove 2 skeletal animation format (reverse-engineered from SLUS_011.59:
// FUN_800124E4 init, FUN_800126B8 step, FUN_80012654 rewind, FUN_800123E8 chunk list).
//
// Animation list blob (model container entry 1): sequence of { u32 len; u8 data[len] } chunks,
// terminated by len == 0. Empty chunks (len 4, data = 0) are placeholders.
//
// Chunk layout:
//   u32 hdrSize            (0x1c)
//   u16 frames
//   u8  step               keyframe spacing in frames (1, 2 or 3)
//   u8  boneCount
//   u8  boneMap[boneCount] bone slot (model node index) for each animated bone, in stream order
//   ...pad to hdrSize
//   u32 footerOffset       offset (from hdrSize) of the event table
//   streams at hdrSize + 4: 3 streams (root translation x,y,z) then 3 per bone (rotation x,y,z).
//     stream = { u16 size (including this 4-byte header); u8 initLo; u8 initHi; u8 codes[size-4] }
//   translation codes: b != 0x80 -> delta = (int8)b ; b == 0x80 -> delta = (int16)(c1<<8 | c2) (big endian)
//   rotation codes:    (b & 0xf0) == 0xf0 -> delta = sign12((b<<8 | c1) & 0xfff) ; else delta = b - 0x78
//   values are accumulated per key; rotations are 12-bit PS1 angles, root translation is stored x2.

export interface AnimStream { init: number; codes: Uint8Array; }
export interface Anim {
  frames: number;
  step: number;
  boneMap: number[];
  /** streams[0..2] = root translation, streams[3 + 3*i .. ] = rotation of boneMap[i] */
  streams: AnimStream[];
  /** decoded per key: keys[k] = Int16Array(3 + 3*boneCount) */
  keys: Int16Array[];
  footer: Uint8Array;
}

export function splitAnimChunks(buf: Uint8Array): (Uint8Array | null)[] {
  const dv = new DataView(buf.buffer, buf.byteOffset, buf.byteLength);
  const out: (Uint8Array | null)[] = [];
  let p = 0;
  while (p + 4 <= buf.byteLength) {
    const n = dv.getUint32(p, true);
    if (n === 0) break;
    const data = buf.subarray(p + 4, p + 4 + n);
    out.push(n >= 0x20 && dv.getUint32(p + 4, true) === 0x1c ? data : null);
    p += 4 + n;
  }
  return out;
}

export function parseAnim(buf: Uint8Array): Anim {
  const dv = new DataView(buf.buffer, buf.byteOffset, buf.byteLength);
  const hdr = dv.getUint32(0, true);
  const frames = dv.getUint16(4, true), step = Math.max(1, dv.getUint8(6)), boneCount = dv.getUint8(7);
  const boneMap: number[] = [];
  for (let i = 0; i < boneCount; i++) boneMap.push(dv.getUint8(8 + i));
  const footerOff = dv.getUint32(hdr, true);
  const footer = buf.subarray(hdr + footerOff);
  const streams: AnimStream[] = [];
  let p = hdr + 4;
  const n = 3 + 3 * boneCount;
  for (let i = 0; i < n; i++) {
    const size = dv.getUint16(p, true);
    const init = dv.getUint8(p + 2) | (dv.getUint8(p + 3) << 8);
    streams.push({ init, codes: buf.subarray(p + 4, p + size) });
    p += size;
  }
  // Decode all keys up front (streams are delta coded and sequential).
  const keyCount = step > 1 ? Math.floor((frames - 2) / step) + 2 : frames;
  const keys: Int16Array[] = [];
  const cur = new Int32Array(n), pos = new Int32Array(n);
  for (let i = 0; i < n; i++) cur[i] = (streams[i].init << 16) >> 16;
  keys.push(Int16Array.from(cur));
  for (let k = 1; k < keyCount; k++) {
    for (let i = 0; i < n; i++) {
      const s = streams[i].codes; let q = pos[i];
      if (q >= s.length) continue;
      let d: number;
      const b = s[q++];
      if (i < 3) {
        if (b !== 0x80) d = (b << 24) >> 24;
        else { d = ((s[q] << 8) | s[q + 1]) << 16 >> 16; q += 2; }
      } else {
        if ((b & 0xf0) === 0xf0) { d = (((b << 8) | s[q]) << 20) >> 20; q += 1; }
        else d = b - 0x78;
      }
      cur[i] = (cur[i] + d) << 16 >> 16;
      pos[i] = q;
    }
    keys.push(Int16Array.from(cur));
  }
  return { frames, step, boneMap, streams, keys, footer };
}

/** Key index used by the game for a given frame (FUN_800126B8). */
export function keyForFrame(a: Anim, frame: number): number {
  let k = frame;
  if (a.step > 1) k = frame === a.frames - 1 ? Math.floor((a.frames - 2) / a.step) + 1 : Math.floor(frame / a.step);
  return Math.min(k, a.keys.length - 1);
}

export interface Pose { rootPos: [number, number, number]; rot: Map<number, [number, number, number]>; }

/** Pose at a frame, with linear interpolation between keys when step > 1. */
export function poseAt(a: Anim, frame: number, interpolate = true): Pose {
  const k0 = keyForFrame(a, frame);
  const k1 = Math.min(k0 + 1, a.keys.length - 1);
  let t = 0;
  if (interpolate && a.step > 1 && frame < a.frames - 1) t = (frame % a.step) / a.step;
  const A = a.keys[k0], B = a.keys[k1];
  const lerpAngle = (x: number, y: number) => { let d = ((y - x) & 4095); if (d > 2048) d -= 4096; return x + d * t; };
  const rootPos: [number, number, number] = [(A[0] + (B[0] - A[0]) * t) / 2, (A[1] + (B[1] - A[1]) * t) / 2, (A[2] + (B[2] - A[2]) * t) / 2];
  const rot = new Map<number, [number, number, number]>();
  for (let i = 0; i < a.boneMap.length; i++) {
    const o = 3 + i * 3;
    rot.set(a.boneMap[i], [lerpAngle(A[o], B[o]), lerpAngle(A[o + 1], B[o + 1]), lerpAngle(A[o + 2], B[o + 2])]);
  }
  return { rootPos, rot };
}
