// Model viewer: disc -> BUST2.BIN -> character model + textures -> WebGPU.
import { Disc, Bust2Archive, fileSource, urlSource, magic } from '../bag2/disc';
import { parseModel, type Model, type Node, type Prim } from '../bag2/model';
import { parseTim, type Tim } from '../psx/tim';
import { Vram } from '../psx/vram';
import { Renderer } from './renderer';
import { multiply, perspective, psxRotMatrix, rotX, rotY, scaling, translation, transformDir, transformPoint, type Mat4 } from './math';

// Character table (18 playable + 1 hidden). Model containers 123+2c / 124+2c (identical pairs),
// texture containers 87+2c (P1 palette) / 88+2c (P2 palette). Names from the exe string table.
const CHARS = ['HEAT', 'KELLY', 'HIRO', 'KITTY-N', 'SHORTY', 'STRIKE', 'TSUTOMU', 'PANDER', 'HUSTLE KONG', 'CAPOEIRA', 'COLUMBO',
  'ROBO-Z GOLD', 'SUSHI BOY', 'CHICHI&SALLY', 'MICHAEL DOI', 'McLOAD', 'JAMES SUNEOKA', 'BI-O', 'ROBO-Z', 'COMET', 'GAS-O', 'PANDER ALT'];

const $ = <T extends HTMLElement>(id: string) => document.getElementById(id) as T;
const status = $<HTMLSpanElement>('status'), info = $<HTMLPreElement>('info');
const charSel = $<HTMLSelectElement>('char'), modelSel = $<HTMLSelectElement>('model'), texSel = $<HTMLSelectElement>('texc');
const lightIn = $<HTMLInputElement>('light'), wireIn = $<HTMLInputElement>('wire'), shadowIn = $<HTMLInputElement>('showShadow');
const canvas = $<HTMLCanvasElement>('gl'), vramCanvas = $<HTMLCanvasElement>('vram');
const log = (m: string) => { status.textContent = m; console.log('[viewer]', m); };

let archive: Bust2Archive | undefined;
let renderer: Renderer | undefined;
let model: Model | undefined;
let vram = new Vram();
let texEntries: Tim[] = [];
let modelBytes: Uint8Array | undefined;
let modelSizeMax = 1000;
let center: [number, number, number] = [0, 0, 0];

interface TexInfo { tpx: number; tpy: number; bpp: 4 | 8 | 16; clutX: number; clutY: number; }

function texInfo(tex: number, clutRow: number): TexInfo | undefined {
  const t = texEntries[tex];
  if (!t) return undefined;
  const c = t.clut ?? { x: 0, y: 0 };
  return { tpx: t.x, tpy: t.y, bpp: t.bpp === 24 ? 16 : t.bpp, clutX: c.x, clutY: c.y + clutRow };
}

/** Rest-pose world matrix per node: world = parent * T * R * S. */
function nodeMatrices(m: Model): Mat4[] {
  const out: Mat4[] = [];
  for (const n of m.nodes) {
    const local = multiply(translation(n.pos.x, n.pos.y, n.pos.z), multiply(psxRotMatrix(n.rot.x, n.rot.y, n.rot.z), scaling(n.scale.x / 4096, n.scale.y / 4096, n.scale.z / 4096)));
    out.push(n.parent >= 0 ? multiply(out[n.parent], local) : local);
  }
  return out;
}

function buildGeometry(m: Model): Float32Array {
  const mats = nodeMatrices(m);
  const v: number[] = [];
  let minY = Infinity, maxY = -Infinity, maxR = 1, minX = Infinity, maxX = -Infinity, minZ = Infinity, maxZ = -Infinity;
  const emit = (p: [number, number, number], n: [number, number, number], c: number[], uv: [number, number], t: TexInfo | undefined, lit: boolean) => {
    v.push(p[0], p[1], p[2], n[0], n[1], n[2], c[0], c[1], c[2], uv[0], uv[1]);
    if (t) v.push(t.tpx, t.tpy | (t.bpp << 16), t.clutX, t.clutY); else v.push(-1, 0, 0, 0);
    v.push(lit ? 1 : 0);
    minY = Math.min(minY, p[1]); maxY = Math.max(maxY, p[1]); minX = Math.min(minX, p[0]); maxX = Math.max(maxX, p[0]); minZ = Math.min(minZ, p[2]); maxZ = Math.max(maxZ, p[2]); maxR = Math.max(maxR, Math.hypot(p[0], p[2]));
  };
  const drawPrim = (node: Node, prim: Prim, mat: Mat4) => {
    const mesh = node.mesh!;
    const order = prim.verts.length === 4 ? [0, 1, 2, 1, 3, 2] : [0, 1, 2];
    const lit = !!prim.normals;
    const t = prim.tex !== undefined ? texInfo(prim.tex, prim.clutRow ?? 0) : undefined;
    for (const k of order) {
      const vi = prim.verts[k];
      const vert = mesh.verts[vi] ?? { x: 0, y: 0, z: 0 };
      const p = transformPoint(mat, vert.x, vert.y, vert.z);
      let n: [number, number, number] = [0, -1, 0];
      let c = [0.5, 0.5, 0.5];
      if (lit) {
        const ni = prim.normals![prim.normals!.length === 1 ? 0 : k];
        const e = mesh.normalIdx[ni];
        if (e) { n = transformDir(mat, e.n.x / 4096, e.n.y / 4096, e.n.z / 4096); c = [e.r / 128, e.g / 128, e.b / 128]; }
      } else if (prim.colors) {
        const col = prim.colors[prim.colors.length === 1 ? 0 : k];
        c = [((col >> 16) & 255) / 128, ((col >> 8) & 255) / 128, (col & 255) / 128];
      }
      let uv: [number, number] = [0, 0];
      if (prim.uv && t) { const u = prim.uv[k]; uv = [u[0] + 0.5, u[1] + 0.5]; }
      emit(p, n, c, uv, prim.uv ? t : undefined, lit);
    }
  };
  for (const node of m.nodes) {
    let meshNode = node;
    if (node.type === 2 && node.instanceOf !== undefined) meshNode = m.nodes[node.instanceOf];
    if (meshNode.mesh) {
      for (const prim of meshNode.mesh.prims) {
        drawPrim(meshNode, prim, mats[node.index]);
        if (prim.overlay && !wireIn.checked) drawPrim(meshNode, prim.overlay, mats[node.index]);
      }
    }
    if (node.shadow && shadowIn.checked) {
      const s = node.shadow;
      const faces = [...s.tris, ...s.quads.map(q => [q[0], q[1], q[2], q[1], q[3], q[2]])];
      for (const f of faces) for (const vi of f) { const vert = s.verts[vi]; emit(transformPoint(mats[node.index], vert.x, vert.y, vert.z), [0, -1, 0], [0.2, 0.2, 0.2], [0, 0], undefined, false); }
    }
  }
  if (!isFinite(minY)) { minY = maxY = 0; }
  center = [(minX + maxX) / 2, (minY + maxY) / 2, (minZ + maxZ) / 2];
  modelSizeMax = Math.max(maxY - minY, maxX - minX, maxZ - minZ, 100);
  info.textContent += `
bounds y ${minY.toFixed(0)}..${maxY.toFixed(0)} r ${maxR.toFixed(0)} verts ${v.length / 16}`;
  console.log('nodes', m.nodes.map(n => `${n.index} t${n.type} p${n.parent} pos ${n.pos.x},${n.pos.y},${n.pos.z} rot ${n.rot.x},${n.rot.y},${n.rot.z} sc ${n.scale.x}`).join(' | '));
  return new Float32Array(v);
}

async function loadTextures(containerIdx: number) {
  if (!archive) return;
  const blob = await archive.entry(containerIdx);
  const kids = magic(blob) === 'DFS_' ? Bust2Archive.children(blob) : [blob];
  vram = new Vram();
  texEntries = [];
  const tims: Tim[] = [];
  for (const k of kids) {
    if (k.length > 8 && k[0] === 0x10 && k[1] === 0 && k[2] === 0 && k[3] === 0) {
      const copy = new Uint8Array(k);
      tims.push(parseTim(copy.buffer as ArrayBuffer));
    }
  }
  // The game relocates textures at load time (per-character overlay code); the TIM headers of body
  // textures all claim (0,0). For viewing, give every TIM its own VRAM slot: image at (512 + 64k, 0|256),
  // CLUT at (16k, 480). UVs are relative to each entry's own origin, so placement does not matter.
  let slot = 0;
  for (const t of tims) {
    if (!t.w || !t.h) continue;
    t.x = 512 + (slot % 8) * 64; t.y = slot < 8 ? 0 : 256;
    if (t.clut) { t.clut.x = slot * 16; t.clut.y = 480; }
    vram.loadTim(t);
    slot++;
  }
  // Texture entry order: first TIM is the small portrait (not referenced by models); body textures follow.
  const portrait = tims.length && tims[0].h <= 64 ? 1 : 0;
  texEntries = tims.slice(portrait);
  renderer?.uploadVram(vram);
  const ctx = vramCanvas.getContext('2d')!;
  ctx.putImageData(new ImageData(vram.toRgba() as Uint8ClampedArray<ArrayBuffer>, 1024, 512), 0, 0);
}

async function loadModel(containerIdx: number, entryIdx: number) {
  if (!archive) return;
  const blob = await archive.entry(containerIdx);
  const kids = Bust2Archive.children(blob);
  modelBytes = kids[entryIdx];
  model = parseModel(modelBytes);
  const nMesh = model.nodes.filter(n => n.mesh).length;
  const nPrims = model.nodes.reduce((a, n) => a + (n.mesh?.prims.length ?? 0), 0);
  const lists = new Map<number, number>();
  for (const n of model.nodes) for (const p of n.mesh?.prims ?? []) lists.set(p.list, (lists.get(p.list) ?? 0) + 1);
  info.textContent = `container ${containerIdx} entrada ${entryIdx}: ${modelBytes.length} bytes\n${model.nodes.length} nós, ${nMesh} meshes, ${nPrims} prims\nlistas: ${[...lists.entries()].sort((a, b) => a[0] - b[0]).map(([k, v]) => k + ':' + v).join(' ')}\ntexturas: ${texEntries.length}`;
  rebuild();
}

function rebuild() {
  if (!model || !renderer) return;
  renderer.setGeometry(buildGeometry(model));
}

let angle = 0, pitch = 0.15, dist = 2.2, dragging = false, lastX = 0, lastY = 0;
canvas.addEventListener('pointerdown', e => { dragging = true; lastX = e.clientX; lastY = e.clientY; canvas.setPointerCapture(e.pointerId); });
canvas.addEventListener('pointerup', () => dragging = false);
canvas.addEventListener('pointermove', e => { if (!dragging) return; angle += (e.clientX - lastX) * 0.01; pitch += (e.clientY - lastY) * 0.01; lastX = e.clientX; lastY = e.clientY; });
canvas.addEventListener('wheel', e => { dist *= Math.exp(e.deltaY * 0.001); e.preventDefault(); }, { passive: false });

function frame() {
  if (renderer) {
    renderer.resize(canvas);
    const aspect = canvas.width / canvas.height;
    const r = modelSizeMax * dist;
    const proj = perspective(Math.PI / 4, aspect, r * 0.01, r * 10);
    const view = multiply(translation(0, 0, -r), multiply(rotX(pitch), rotY(angle)));
    const modelM = translation(-center[0], -center[1], -center[2]);
    const mvp = multiply(proj, multiply(view, modelM));
    const l = +lightIn.value;
    renderer.frame(mvp, modelM, [0.4, 0.8, -0.5, l], 0.35);
  }
  requestAnimationFrame(frame);
}

async function openDisc(disc: Disc) {
  log('Lendo ISO...');
  const files = await disc.listRoot();
  const bust2 = files.find(f => f.name.toUpperCase() === 'BUST2.BIN');
  if (!bust2) throw new Error('BUST2.BIN nao encontrado');
  archive = new Bust2Archive(disc, bust2.lba);
  const entries = await archive.entries();
  log(`BUST2.BIN: ${entries.length} entradas`);
  charSel.innerHTML = CHARS.map((n, i) => `<option value="${i}">${i}: ${n}</option>`).join('');
  texSel.innerHTML = entries.map((_, i) => `<option value="${i}">${i}</option>`).join('');
  await selectChar(2);
}

async function selectChar(c: number) {
  const mc = 123 + 2 * c, tc = 87 + 2 * c;
  charSel.value = String(c);
  const blob = await archive!.entry(mc);
  const kids = Bust2Archive.children(blob);
  modelSel.innerHTML = kids.map((k, i) => `<option value="${i}">${i} (${k.length} B)${k.length > 16 && k[4] === 0xff ? ' modelo' : ''}</option>`).join('');
  texSel.value = String(tc);
  await loadTextures(tc);
  await loadModel(mc, 0);
}

charSel.addEventListener('change', () => selectChar(+charSel.value));
modelSel.addEventListener('change', () => loadModel(123 + 2 * +charSel.value, +modelSel.value));
texSel.addEventListener('change', async () => { await loadTextures(+texSel.value); rebuild(); });
wireIn.addEventListener('change', rebuild); shadowIn.addEventListener('change', rebuild);
$<HTMLInputElement>('bin').addEventListener('change', async e => {
  const f = (e.target as HTMLInputElement).files?.[0]; if (f) await openDisc(new Disc(fileSource(f)));
});

(async () => {
  renderer = new Renderer();
  try { await renderer.init(canvas); } catch (e) { log(String(e)); return; }
  frame();
  if (new URLSearchParams(location.search).has('dev')) {
    try { await openDisc(new Disc(await urlSource('/dev-disc.bin'))); } catch (e) { log('dev disc: ' + e); }
  }
})();
