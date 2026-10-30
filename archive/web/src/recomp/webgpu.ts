// WebGPU presentation of the recompiled game's framebuffer.
// The runtime (software renderer) exposes its ARGB8888 present buffer through
// psx_web_frame_ptr/w/h/serial (archive/web-recomp/patches/psxrecomp-web-frame-export.patch). Every
// animation frame with a new serial, the pixels are copied from the WASM heap into a bgra8unorm
// texture and drawn with a "sharp bilinear" filter (integer-scale crisp pixels, smooth edges).

const SHADER = /* wgsl */`
struct U { texSize: vec2f, outSize: vec2f, scanlines: f32, pad: f32 }
@group(0) @binding(0) var<uniform> u: U;
@group(0) @binding(1) var tex: texture_2d<f32>;
@group(0) @binding(2) var samp: sampler;

struct VSOut { @builtin(position) pos: vec4f, @location(0) uv: vec2f }

@vertex fn vs(@builtin(vertex_index) i: u32) -> VSOut {
  // full-screen triangle
  let p = array<vec2f, 3>(vec2f(-1.0, -1.0), vec2f(3.0, -1.0), vec2f(-1.0, 3.0));
  var o: VSOut;
  o.pos = vec4f(p[i], 0.0, 1.0);
  o.uv = vec2f((p[i].x + 1.0) * 0.5, 1.0 - (p[i].y + 1.0) * 0.5);
  return o;
}

// Sharp bilinear: sample at texel centres snapped to the integer scale, bilinear only in the
// fractional transition region, so pixels stay crisp without shimmering.
@fragment fn fs(i: VSOut) -> @location(0) vec4f {
  let scale = max(floor(u.outSize.y / u.texSize.y), 1.0);
  let texel = i.uv * u.texSize;
  let f = fract(texel);
  let s = clamp((f - 0.5) * scale + 0.5, vec2f(0.0), vec2f(1.0));
  let uv = (floor(texel) + s) / u.texSize;
  var c = textureSample(tex, samp, uv).rgb;
  if (u.scanlines > 0.0) {
    let line = fract(texel.y);
    c = c * (1.0 - u.scanlines * smoothstep(0.3, 0.5, abs(line - 0.5)));
  }
  return vec4f(c, 1.0);
}
`;

export class WebGpuPresenter {
  private device!: GPUDevice; private ctx!: GPUCanvasContext; private pipeline!: GPURenderPipeline;
  private tex?: GPUTexture; private bind?: GPUBindGroup; private uni!: GPUBuffer; private sampler!: GPUSampler;
  private texW = 0; private texH = 0;
  scanlines = 0;
  onError?: (msg: string) => void;

  static async create(canvas: HTMLCanvasElement): Promise<WebGpuPresenter | undefined> {
    const adapter = await navigator.gpu?.requestAdapter();
    if (!adapter) return undefined;
    const p = new WebGpuPresenter();
    p.device = await adapter.requestDevice();
    p.device.addEventListener('uncapturederror', (ev) => p.onError?.(String((ev as GPUUncapturedErrorEvent).error.message)));
    p.ctx = canvas.getContext('webgpu') as unknown as GPUCanvasContext;
    const format = navigator.gpu.getPreferredCanvasFormat();
    p.ctx.configure({ device: p.device, format, alphaMode: 'opaque' });
    const mod = p.device.createShaderModule({ code: SHADER });
    p.pipeline = p.device.createRenderPipeline({
      layout: 'auto',
      vertex: { module: mod, entryPoint: 'vs' },
      fragment: { module: mod, entryPoint: 'fs', targets: [{ format }] },
      primitive: { topology: 'triangle-list' },
    });
    p.uni = p.device.createBuffer({ size: 32, usage: GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST });
    p.sampler = p.device.createSampler({ magFilter: 'linear', minFilter: 'linear' });
    return p;
  }

  /** Upload one frame (ARGB8888 little-endian = BGRA bytes) and draw it. */
  present(pixels: Uint8Array, w: number, h: number, canvas: HTMLCanvasElement) {
    if (w !== this.texW || h !== this.texH || !this.tex) {
      this.tex?.destroy();
      this.tex = this.device.createTexture({ size: [w, h], format: 'bgra8unorm', usage: GPUTextureUsage.TEXTURE_BINDING | GPUTextureUsage.COPY_DST });
      this.bind = this.device.createBindGroup({ layout: this.pipeline.getBindGroupLayout(0), entries: [
        { binding: 0, resource: { buffer: this.uni } }, { binding: 1, resource: this.tex.createView() }, { binding: 2, resource: this.sampler } ] });
      this.texW = w; this.texH = h;
    }
    const cw = Math.max(1, Math.round(canvas.clientWidth * devicePixelRatio)), ch = Math.max(1, Math.round(canvas.clientHeight * devicePixelRatio));
    if (canvas.width !== cw || canvas.height !== ch) { canvas.width = cw; canvas.height = ch; }
    this.device.queue.writeTexture({ texture: this.tex }, pixels, { bytesPerRow: w * 4 }, [w, h]);
    this.device.queue.writeBuffer(this.uni, 0, new Float32Array([w, h, cw, ch, this.scanlines, 0, 0, 0]));
    const enc = this.device.createCommandEncoder();
    const pass = enc.beginRenderPass({ colorAttachments: [{ view: this.ctx.getCurrentTexture().createView(), loadOp: 'clear', storeOp: 'store', clearValue: { r: 0, g: 0, b: 0, a: 1 } }] });
    pass.setPipeline(this.pipeline); pass.setBindGroup(0, this.bind!); pass.draw(3); pass.end();
    this.device.queue.submit([enc.finish()]);
  }
}
