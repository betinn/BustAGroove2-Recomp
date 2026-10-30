// WebGPU renderer for BAG2 models: emulated PS1 VRAM sampled in the fragment shader
// (4/8/16 bpp + CLUT), vertex colors / lambert from normals, PS1 fixed-point units.
import { Vram, VRAM_W, VRAM_H } from '../psx/vram';
import type { Mat4 } from './math';

/** One vertex: pos(3) normal(3) color(3) uv(2) tex(4: tpx, tpy|bpp<<16, clutX, clutY) flags(1: lit) = 16 floats */
export const VERTEX_FLOATS = 16;

const SHADER = /* wgsl */`
struct Uniforms { mvp: mat4x4f, model: mat4x4f, light: vec4f, ambient: vec4f }
@group(0) @binding(0) var<uniform> U: Uniforms;
@group(0) @binding(1) var vram: texture_2d<u32>;

struct VSOut {
  @builtin(position) pos: vec4f,
  @location(0) color: vec3f,
  @location(1) uv: vec2f,
  @location(2) @interpolate(flat) tex: vec4f,
  @location(3) @interpolate(flat) lit: f32,
  @location(4) normal: vec3f,
}

@vertex fn vs(@location(0) p: vec3f, @location(1) n: vec3f, @location(2) c: vec3f, @location(3) uv: vec2f,
              @location(4) t0: vec4f, @location(5) lit: f32) -> VSOut {
  var o: VSOut;
  o.pos = U.mvp * vec4f(p, 1.0);
  o.color = c;
  o.uv = uv;
  o.tex = t0;
  o.lit = lit;
  o.normal = (U.model * vec4f(n, 0.0)).xyz;
  return o;
}

fn vramWord(x: i32, y: i32) -> u32 {
  return textureLoad(vram, vec2i(x & 1023, y & 511), 0).r;
}
fn texel(tex: vec4f, uv: vec2f) -> u32 {
  let u = i32(floor(uv.x)) & 255;
  let v = i32(floor(uv.y)) & 255;
  let tpx = i32(tex.x);
  let tpy = i32(tex.y) & 0xffff;
  let bpp = (i32(tex.y) >> 16) & 0xff;
  let cx = i32(tex.z);
  let cy = i32(tex.w);
  if (bpp == 16) { return vramWord(tpx + u, tpy + v); }
  if (bpp == 8) {
    let w = vramWord(tpx + (u >> 1), tpy + v);
    let idx = select(w & 0xffu, w >> 8u, (u & 1) == 1);
    return vramWord(cx + i32(idx), cy);
  }
  let w = vramWord(tpx + (u >> 2), tpy + v);
  let idx = (w >> (u32(u & 3) * 4u)) & 15u;
  return vramWord(cx + i32(idx), cy);
}
fn rgb15(c: u32) -> vec3f {
  return vec3f(f32(c & 31u), f32((c >> 5u) & 31u), f32((c >> 10u) & 31u)) / 31.0;
}

@fragment fn fs(i: VSOut) -> @location(0) vec4f {
  var base = i.color;
  if (i.lit > 0.5) {
    let n = normalize(i.normal);
    let d = max(dot(n, -normalize(U.light.xyz)), 0.0);
    base = i.color * min(U.ambient.xyz + d * U.light.w, vec3f(1.0));  // color = material/128 (0x80 neutral)
  }
  if (i.tex.x < 0.0) {           // untextured: gouraud color as computed by the GTE
    return vec4f(min(base, vec3f(1.0)), 1.0);
  }
  let t = texel(i.tex, i.uv);
  if (t == 0u) { discard; }
  // PS1 modulation: texel * color / 128 (color 0x80 = neutral)
  return vec4f(min(rgb15(t) * base, vec3f(1.0)), 1.0);
}
`;

export class Renderer {
  device!: GPUDevice; ctx!: GPUCanvasContext; format!: GPUTextureFormat;
  pipeline!: GPURenderPipeline; vramTex!: GPUTexture; uniformBuf!: GPUBuffer; bind!: GPUBindGroup;
  depth!: GPUTexture;
  vbuf?: GPUBuffer; vcount = 0;

  async init(canvas: HTMLCanvasElement) {
    const adapter = await navigator.gpu?.requestAdapter();
    if (!adapter) throw new Error('WebGPU indisponivel neste navegador');
    this.device = await adapter.requestDevice();
    this.ctx = canvas.getContext('webgpu') as unknown as GPUCanvasContext;
    this.format = navigator.gpu.getPreferredCanvasFormat();
    this.ctx.configure({ device: this.device, format: this.format, alphaMode: 'opaque' });
    this.vramTex = this.device.createTexture({ size: [VRAM_W, VRAM_H], format: 'r16uint', usage: GPUTextureUsage.TEXTURE_BINDING | GPUTextureUsage.COPY_DST });
    this.uniformBuf = this.device.createBuffer({ size: 4 * 16 * 2 + 32, usage: GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST });
    const mod = this.device.createShaderModule({ code: SHADER });
    const stride = VERTEX_FLOATS * 4;
    this.pipeline = this.device.createRenderPipeline({
      layout: 'auto',
      vertex: { module: mod, entryPoint: 'vs', buffers: [{ arrayStride: stride, attributes: [
        { shaderLocation: 0, offset: 0, format: 'float32x3' }, { shaderLocation: 1, offset: 12, format: 'float32x3' },
        { shaderLocation: 2, offset: 24, format: 'float32x3' }, { shaderLocation: 3, offset: 36, format: 'float32x2' },
        { shaderLocation: 4, offset: 44, format: 'float32x4' }, { shaderLocation: 5, offset: 60, format: 'float32' } ] }] },
      fragment: { module: mod, entryPoint: 'fs', targets: [{ format: this.format }] },
      primitive: { topology: 'triangle-list', cullMode: 'none' },
      depthStencil: { format: 'depth24plus', depthWriteEnabled: true, depthCompare: 'less' },
    });
    this.bind = this.device.createBindGroup({ layout: this.pipeline.getBindGroupLayout(0), entries: [
      { binding: 0, resource: { buffer: this.uniformBuf } }, { binding: 1, resource: this.vramTex.createView() } ] });
    this.resize(canvas);
  }

  resize(canvas: HTMLCanvasElement) {
    const w = Math.max(1, canvas.clientWidth * devicePixelRatio | 0), h = Math.max(1, canvas.clientHeight * devicePixelRatio | 0);
    if (canvas.width !== w || canvas.height !== h) { canvas.width = w; canvas.height = h; }
    if (!this.depth || this.depth.width !== w || this.depth.height !== h) {
      this.depth?.destroy();
      this.depth = this.device.createTexture({ size: [w, h], format: 'depth24plus', usage: GPUTextureUsage.RENDER_ATTACHMENT });
    }
  }

  uploadVram(v: Vram) {
    this.device.queue.writeTexture({ texture: this.vramTex }, v.words, { bytesPerRow: VRAM_W * 2 }, [VRAM_W, VRAM_H]);
  }

  setGeometry(data: Float32Array) {
    this.vbuf?.destroy();
    this.vcount = data.length / VERTEX_FLOATS;
    if (!this.vcount) return;
    this.vbuf = this.device.createBuffer({ size: data.byteLength, usage: GPUBufferUsage.VERTEX | GPUBufferUsage.COPY_DST });
    this.device.queue.writeBuffer(this.vbuf, 0, data);
  }

  frame(mvp: Mat4, model: Mat4, light: [number, number, number, number], ambient: number) {
    const u = new Float32Array(16 * 2 + 8);
    u.set(mvp, 0); u.set(model, 16); u.set(light, 32); u.set([ambient, ambient, ambient, 0], 36);
    this.device.queue.writeBuffer(this.uniformBuf, 0, u);
    const enc = this.device.createCommandEncoder();
    const pass = enc.beginRenderPass({
      colorAttachments: [{ view: this.ctx.getCurrentTexture().createView(), clearValue: { r: 0.08, g: 0.08, b: 0.1, a: 1 }, loadOp: 'clear', storeOp: 'store' }],
      depthStencilAttachment: { view: this.depth.createView(), depthClearValue: 1, depthLoadOp: 'clear', depthStoreOp: 'store' },
    });
    if (this.vbuf && this.vcount) {
      pass.setPipeline(this.pipeline); pass.setBindGroup(0, this.bind); pass.setVertexBuffer(0, this.vbuf); pass.draw(this.vcount);
    }
    pass.end();
    this.device.queue.submit([enc.finish()]);
  }
}
