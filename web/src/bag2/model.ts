// Bust A Groove 2 character/stage model format (reverse-engineered from SLUS_011.59:
// FUN_80022170 skeleton, FUN_80021E24 mesh header, FUN_8001FF34 build, FUN_8003xxxx fill).
// See notes/model-format.md.

export interface Vec3 { x: number; y: number; z: number; }
export interface NormalGroup { r: number; g: number; b: number; code: number; count: number; normals: Vec3[]; }
export type PrimKind = 'F3' | 'F4' | 'FT3' | 'FT4' | 'G3' | 'G4' | 'GT3' | 'GT4';
export interface Prim {
  kind: PrimKind;
  list: number;                 // header list index 0..31
  verts: number[];              // 3 or 4 vertex indices (quad order v0,v1,v2,v3 -> tris 012, 132)
  normals?: number[];           // per-vertex normal index into mesh.normalIdx (lit prims)
  colors?: number[];            // per-vertex 0xRRGGBB (static prims)
  uv?: [number, number][];      // per-vertex (u,v) relative to texture entry origin
  tex?: number;                 // texture entry index 0..31
  clutRow?: number;             // CLUT row offset (+y)
  semi?: boolean;               // semi-transparent (abr from tpage)
  overlay?: Prim;               // second layer (G+GT combos): textured layer drawn over base
}
export interface Mesh {
  verts: Vec3[];
  groups: NormalGroup[];        // lit material groups, in order; global normal index = position in concatenation
  normalIdx: { r: number; g: number; b: number; n: Vec3 }[]; // flattened normals with material color
  sharedRefs: { src: number; dst: number }[]; // copy local sxy[src] -> model-global[dst]
  sharedUse: { src: number; dst: number }[];  // copy model-global[src] -> local sxy[dst]
  prims: Prim[];
}
export interface Shadow { verts: Vec3[]; tris: number[][]; quads: number[][]; }
export interface Node {
  index: number; type: number;
  parent: number; prev: number; child: number; sibling: number;
  rot: Vec3;                    // 12-bit angles (4096 = 360deg)
  scale: Vec3;                  // 4096 = 1.0
  pos: Vec3;
  mesh?: Mesh; shadow?: Shadow; instanceOf?: number;
}
export interface Model { nodes: Node[]; }

// per-list layout: words, kind, lit?, uv byte offsets in record (per vertex), tex byte, clut byte, overlay kind
interface Layout { words: number; kind: PrimKind; lit: boolean; nAt?: number; uv?: number[]; tex?: number; clut?: number; colorAt?: number; over?: PrimKind; overNAt?: number; overUv?: number[]; }
// quad vertex order in w0: byte0 = v3, byte1 = v0, byte2 = v1, byte3 = v2. UV storage order for quads: uv0, uv2, uv3, uv1.
const L: Layout[] = [
  /*0 0xac*/ { words: 2, kind: 'F3', lit: true, nAt: 4 },
  /*1 0xae*/ { words: 2, kind: 'F4', lit: true, nAt: 4 },
  /*2 0xb0*/ { words: 4, kind: 'FT3', lit: true, nAt: 4, uv: [8, 10, 12], tex: 14, clut: 15 },
  /*3 0xb2*/ { words: 4, kind: 'FT4', lit: true, nAt: 4, uv: [6, 12, 8, 10], tex: 14, clut: 15 },
  /*4 0xb4*/ { words: 5, kind: 'GT3', lit: true, nAt: 4, uv: [12, 14, 16], tex: 18, clut: 19 },
  /*5 0xb6*/ { words: 6, kind: 'GT4', lit: true, nAt: 4, uv: [12, 18, 14, 16], tex: 20, clut: 21 },
  /*6 0xb8*/ { words: 3, kind: 'G3', lit: true, nAt: 4 },
  /*7 0xba*/ { words: 3, kind: 'G4', lit: true, nAt: 4 },
  /*8 0xbc*/ { words: 6, kind: 'G3', lit: true, nAt: 4, over: 'GT3', overNAt: 10, overUv: [16, 18, 20], tex: 22, clut: 23 },
  /*9 0xbe*/ { words: 8, kind: 'G4', lit: true, nAt: 4, over: 'GT4', overNAt: 12, overUv: [20, 26, 22, 24], tex: 28, clut: 29 },
  /*10 0xc0*/ { words: 4, kind: 'FT3', lit: false, colorAt: 4, uv: [8, 10, 12], tex: 14, clut: 15 },
  /*11 0xc2*/ { words: 5, kind: 'FT4', lit: false, colorAt: 4, uv: [8, 14, 10, 12], tex: 16, clut: 17 },
  /*12 0xc4*/ { words: 5, kind: 'G3', lit: true, nAt: 4, over: 'FT3', overUv: [12, 14, 16], tex: 18, clut: 19 },
  /*13 0xc6*/ { words: 6, kind: 'G4', lit: true, nAt: 4, over: 'FT4', overUv: [12, 18, 14, 16], tex: 20, clut: 21 },
  /*14 0xc8*/ { words: 4, kind: 'F3', lit: false, colorAt: 4 },
  /*15 0xca*/ { words: 5, kind: 'F4', lit: true, nAt: 4, over: 'FT4', overUv: [8, 14, 10, 12], tex: 16, clut: 17 },
  /*16 0xcc*/ { words: 2, kind: 'F3', lit: false, colorAt: 4 },
  /*17 0xce*/ { words: 2, kind: 'F4', lit: false, colorAt: 4 },
  /*18 0xd0*/ { words: 2, kind: 'F3', lit: false, colorAt: 4 },
  /*19 0xd2*/ { words: 2, kind: 'F4', lit: false, colorAt: 4 },
  /*20 0xd4*/ { words: 3, kind: 'G3', lit: true, nAt: 4 },
  /*21 0xd6*/ { words: 4, kind: 'G4', lit: true, nAt: 4 },
  /*22 0xd8*/ { words: 2, kind: 'F3', lit: false, colorAt: 4 },
  /*23 0xda*/ { words: 2, kind: 'F4', lit: false, colorAt: 4 },
  /*24 0xdc*/ { words: 4, kind: 'G3', lit: false, colorAt: 4 },
  /*25 0xde*/ { words: 5, kind: 'G4', lit: false, colorAt: 4 },
  /*26 0xe0*/ { words: 6, kind: 'GT3', lit: false, colorAt: 4, uv: [16, 18, 20], tex: 22, clut: 23 },
  /*27 0xe2*/ { words: 8, kind: 'GT4', lit: false, colorAt: 4, uv: [20, 26, 22, 24], tex: 28, clut: 29 },
  /*28 0xe4*/ { words: 6, kind: 'G3', lit: false, colorAt: 4, over: 'GT3', overUv: [16, 18, 20], tex: 22, clut: 23 },
  /*29 0xe6*/ { words: 8, kind: 'G4', lit: false, colorAt: 4, over: 'GT4', overUv: [20, 26, 22, 24], tex: 28, clut: 29 },
  /*30 0xe8*/ { words: 4, kind: 'G3', lit: false, colorAt: 4 },
  /*31 0xea*/ { words: 5, kind: 'G4', lit: false, colorAt: 4 },
];
export const LIST_LAYOUTS = L;

function svec(dv: DataView, o: number): Vec3 { return { x: dv.getInt16(o, true), y: dv.getInt16(o + 2, true), z: dv.getInt16(o + 4, true) }; }

function parseMesh(dv: DataView, p: number): { mesh: Mesh; end: number } {
  const u32 = (o: number) => dv.getUint32(o, true), s16 = (o: number) => dv.getInt16(o, true);
  const nVerts = u32(p + 0x94);
  const verts: Vec3[] = [];
  let q = p + 0x120;
  for (let i = 0; i < nVerts; i++, q += 8) verts.push(svec(dv, q));
  const nGroupsA = u32(p + 0x9c), nGroupsB = u32(p + 0xa0);
  const groups: NormalGroup[] = [];
  const normalIdx: Mesh['normalIdx'] = [];
  for (let g = 0; g < nGroupsA + nGroupsB; g++) {
    const r = dv.getUint8(q), gg = dv.getUint8(q + 1), b = dv.getUint8(q + 2), code = dv.getUint8(q + 3);
    const count = u32(q + 4); q += 8;
    const normals: Vec3[] = [];
    for (let i = 0; i < count; i++, q += 8) { const n = svec(dv, q); normals.push(n); normalIdx.push({ r, g: gg, b, n }); }
    groups.push({ r, g: gg, b, code, count, normals });
  }
  const sharedRefs = [], sharedUse = [];
  for (let i = 0; i < u32(p + 0xa4); i++, q += 4) { const w = u32(q); sharedRefs.push({ src: w & 0xff, dst: w >>> 16 }); }
  for (let i = 0; i < u32(p + 0xa8); i++, q += 4) { const w = u32(q); sharedUse.push({ src: w & 0xffff, dst: w >>> 16 }); }
  const prims: Prim[] = [];
  for (let li = 0; li < 32; li++) {
    const count = s16(p + 0xac + li * 2);
    const lay = L[li];
    for (let k = 0; k < count; k++, q += lay.words * 4) {
      const w0 = u32(q);
      const isQuad = lay.kind.endsWith('4');
      const verts = isQuad ? [(w0 >>> 8) & 0xff, (w0 >>> 16) & 0xff, w0 >>> 24, w0 & 0xff] : [(w0 >>> 8) & 0xff, (w0 >>> 16) & 0xff, w0 >>> 24];
      const nv = verts.length;
      const readUv = (offs: number[]) => offs.map(o => [dv.getUint8(q + o), dv.getUint8(q + o + 1)] as [number, number]);
      const prim: Prim = { kind: lay.kind, list: li, verts };
      if (lay.lit) {
        const nn = lay.kind.startsWith('G') ? nv : 1;
        prim.normals = []; for (let i = 0; i < nn; i++) prim.normals.push(dv.getUint16(q + lay.nAt! + i * 2, true));
      } else {
        const nc = lay.kind.startsWith('G') ? nv : 1;
        prim.colors = []; for (let i = 0; i < nc; i++) { const o = q + lay.colorAt! + i * 4; prim.colors.push(dv.getUint8(o) << 16 | dv.getUint8(o + 1) << 8 | dv.getUint8(o + 2)); }
      }
      if (lay.uv) prim.uv = readUv(lay.uv);
      if (lay.tex !== undefined && !lay.over) { const t = dv.getUint8(q + lay.tex); prim.tex = t & 0x1f; prim.semi = (t >> 5) !== 0; prim.clutRow = dv.getUint8(q + lay.clut!); }
      if (lay.over) {
        const t = dv.getUint8(q + lay.tex!);
        const ov: Prim = { kind: lay.over, list: li, verts, uv: readUv(lay.overUv!), tex: t & 0x1f, semi: (t >> 5) !== 0, clutRow: dv.getUint8(q + lay.clut!) };
        if (lay.overNAt !== undefined) { ov.normals = []; for (let i = 0; i < nv; i++) ov.normals.push(dv.getUint16(q + lay.overNAt + i * 2, true)); }
        else ov.normals = prim.normals;
        prim.overlay = ov;
      }
      prims.push(prim);
    }
  }
  return { mesh: { verts, groups, normalIdx, sharedRefs, sharedUse, prims }, end: q };
}

function parseShadow(dv: DataView, p: number): { shadow: Shadow; end: number } {
  const nv = dv.getUint32(p + 16, true), nt = dv.getInt16(p + 20, true), nq = dv.getInt16(p + 22, true);
  let q = p + 0x4c;
  const verts: Vec3[] = []; for (let i = 0; i < nv; i++, q += 8) verts.push(svec(dv, q));
  const tris: number[][] = [], quads: number[][] = [];
  for (let i = 0; i < nt; i++, q += 8) { const w = dv.getUint32(q, true); tris.push([(w >>> 8) & 0xff, (w >>> 16) & 0xff, w >>> 24]); }
  for (let i = 0; i < nq; i++, q += 8) { const w = dv.getUint32(q, true); quads.push([(w >>> 8) & 0xff, (w >>> 16) & 0xff, w >>> 24, w & 0xff]); }
  return { shadow: { verts, tris, quads }, end: q };
}

export function parseModel(buf: Uint8Array): Model {
  const dv = new DataView(buf.buffer, buf.byteOffset, buf.byteLength);
  const N = dv.getUint32(0, true);
  const nodes: Node[] = [];
  for (let i = 0; i < N; i++) {
    const o = 4 + i * 16;
    nodes.push({ index: i, type: 0, parent: dv.getInt32(o, true), prev: dv.getInt32(o + 4, true), child: dv.getInt32(o + 8, true), sibling: dv.getInt32(o + 12, true),
      rot: { x: 0, y: 0, z: 0 }, scale: { x: 4096, y: 4096, z: 4096 }, pos: { x: 0, y: 0, z: 0 } });
  }
  let p = 4 + N * 16;
  for (let i = 0; i < N; i++) {
    const n = nodes[i];
    const t = dv.getUint32(p, true); n.type = t;
    if (t === 0) p += 4;
    else if (t === 2) { n.instanceOf = dv.getInt32(p + 4, true); p += 8; }
    else if (t === 3) { p += 12 + dv.getUint32(p + 8, true) * 8; }
    else if (t === 4) { const r = parseShadow(dv, p); n.shadow = r.shadow; p = r.end; }
    else if (t === 1) { const r = parseMesh(dv, p); n.mesh = r.mesh; p = r.end; }
    else throw new Error('node ' + i + ': unknown type ' + t + ' at ' + p);
    n.rot = { x: dv.getInt16(p, true), y: dv.getInt16(p + 2, true), z: dv.getInt16(p + 4, true) };
    n.scale = { x: dv.getInt32(p + 8, true), y: dv.getInt32(p + 12, true), z: dv.getInt32(p + 16, true) };
    n.pos = { x: dv.getInt32(p + 24, true), y: dv.getInt32(p + 28, true), z: dv.getInt32(p + 32, true) };
    p += 40;
  }
  if (p !== buf.byteLength) console.warn('model: parsed ' + p + ' of ' + buf.byteLength + ' bytes');
  return { nodes };
}
