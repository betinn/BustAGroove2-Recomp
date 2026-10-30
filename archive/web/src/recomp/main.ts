// Página do jogo recompilado (PSXRecomp fork kem0x → Emscripten). Baseada no fluxo do
// web/shell.html do RepsiMan: o runtime (bag2.js/bag2.wasm em /recomp/, gerado por
// archive/web-recomp/build-web.ps1) roda main() numa pthread (PROXY_TO_PTHREAD); o SDL2 da port do
// Emscripten apresenta o framebuffer do renderer software no <canvas id="canvas"> e lê o teclado;
// o áudio da SPU sai pelo web_audio_bridge.c do fork (Module.psxWebAudio). Os arquivos do disco,
// o OpenBIOS, game.toml e keybinds.ini são escritos no MEMFS antes de Module.callMain().

import { WebGpuPresenter } from './webgpu';

type EmModule = {
  HEAPU8?: Uint8Array;
  noInitialRun: boolean;
  canvas: HTMLCanvasElement;
  psxWebAudioLocked: boolean;
  psxWebAudio?: { context: AudioContext; nextTime: number; sources: Set<unknown>; started: boolean; underruns: number };
  locateFile: (path: string) => string;
  print: (t: string) => void;
  printErr: (t: string) => void;
  onRuntimeInitialized: () => void;
  onAbort: (reason: unknown) => void;
  FS: {
    mkdirTree(path: string): void;
    writeFile(path: string, data: Uint8Array | string, opts?: { canOwn?: boolean }): void;
  };
  callMain(args: string[]): void;
  _psx_web_perf_vblanks?: () => number;
  _psx_web_set_paused?: (paused: number) => void;
  _psx_web_frame_ptr?: () => number;
  _psx_web_frame_w?: () => number;
  _psx_web_frame_h?: () => number;
  _psx_web_frame_serial?: () => number;
};
declare global { interface Window { Module: EmModule } }

const $ = <T extends HTMLElement>(id: string) => document.getElementById(id) as T;
const discInput = $<HTMLInputElement>('disc'), startBtn = $<HTMLButtonElement>('start'), pauseBtn = $<HTMLButtonElement>('pause');
const status = $<HTMLSpanElement>('status'), logEl = $<HTMLPreElement>('log'), perfEl = $<HTMLParagraphElement>('perf');
const canvas = $<HTMLCanvasElement>('canvas');

const RUNTIME_BASE = '/recomp/';
const DISC_DIR = '/game/disc';
const params = new URLSearchParams(location.search);
const devMode = params.has('dev');

let runtimeReady = false, started = false, paused = false;
const setStatus = (m: string) => { status.textContent = m; console.log('[recomp]', m); };
function log(line: string) {
  logEl.textContent += line + '\n';
  if (logEl.textContent.length > 200_000) logEl.textContent = logEl.textContent.slice(-100_000);
  logEl.scrollTop = logEl.scrollHeight;
}

function updateStart() {
  startBtn.disabled = !runtimeReady || started || !(devMode || selectedDisc());
}

// ---- seleção de arquivos ------------------------------------------------------------------
function selectedDisc(): { bin: File; cue?: File } | undefined {
  const files = Array.from(discInput.files ?? []);
  const bin = files.find(f => /\.bin$/i.test(f.name));
  const cue = files.find(f => /\.cue$/i.test(f.name));
  return bin ? { bin, cue } : undefined;
}
discInput.addEventListener('change', () => {
  const d = selectedDisc();
  setStatus(d ? `${d.bin.name} (${(d.bin.size / 1e6).toFixed(0)} MB)${d.cue ? ' + ' + d.cue.name : ' (cue gerado)'}` : 'Selecione o .bin (e o .cue, se tiver).');
  updateStart();
});

// Cue trivial para um .bin de faixa única MODE2/2352 (o do BAG2 é exatamente assim).
const trivialCue = (binName: string) => `FILE "${binName}" BINARY\n  TRACK 01 MODE2/2352\n    INDEX 01 00:00:00\n`;

// ---- FS virtual -----------------------------------------------------------------------------
async function installFile(file: File, path: string) {
  setStatus(`Carregando ${file.name}…`);
  // canOwn: o MEMFS fica dono do ArrayBuffer do navegador em vez de copiar 600 MB para o heap WASM.
  window.Module.FS.writeFile(path, new Uint8Array(await file.arrayBuffer()), { canOwn: true });
}
async function installUrl(url: string, path: string, label: string) {
  const res = await fetch(url, { cache: 'no-store' });
  if (!res.ok) throw new Error(`${url}: HTTP ${res.status}`);
  const expected = Number(res.headers.get('Content-Length')) || 0;
  let bytes: Uint8Array;
  if (res.body && expected > 0) {
    bytes = new Uint8Array(expected);
    const reader = res.body.getReader();
    let got = 0, lastShown = 0;
    for (;;) {
      const { done, value } = await reader.read();
      if (done) break;
      bytes.set(value, got); got += value.byteLength;
      if (got - lastShown >= 16 << 20 || got === expected) { setStatus(`Lendo ${label}: ${(got / 1048576).toFixed(0)} / ${(expected / 1048576).toFixed(0)} MiB`); lastShown = got; }
    }
    if (got !== expected) throw new Error(`${label}: incompleto (${got} de ${expected} bytes)`);
  } else {
    bytes = new Uint8Array(await res.arrayBuffer());
  }
  window.Module.FS.writeFile(path, bytes, { canOwn: true });
  return bytes.byteLength;
}
async function installOpenBios() {
  const res = await fetch(RUNTIME_BASE + 'bios/openbios.bin');
  if (!res.ok) throw new Error(`OpenBIOS não encontrado em ${RUNTIME_BASE}bios/openbios.bin (rode archive/web-recomp/build-web.ps1 -Step copy)`);
  const bytes = new Uint8Array(await res.arrayBuffer());
  if (bytes.byteLength !== 512 * 1024) throw new Error(`OpenBIOS com tamanho inválido: ${bytes.byteLength}`);
  window.Module.FS.mkdirTree('/bios');
  window.Module.FS.writeFile('/bios/openbios.bin', bytes, { canOwn: true });
  return '/bios/openbios.bin';
}
const tomlStr = (v: string) => v.replaceAll('\\', '\\\\').replaceAll('"', '\\"');
// Mesmo conteúdo de archive/web-recomp/game-web.toml, com caminhos do MEMFS.
const gameToml = (cueName: string) => `[game]
name = "Bust A Groove 2"
id = "SLUS-01159"
exe = "game/SLUS_011.59"
disc = "${DISC_DIR}/${tomlStr(cueName)}"
load_address = "0x80010000"
entry_pc = "0x80038270"
text_size = "0x0004F000"
stack_base = "0x801FFFF0"

[recompiler]
seeds = "seeds/functions.txt"
strict = true
out_dir = "generated"

[runtime]
debug_port = 0
window_title = "Bust A Groove 2 Recompiled - Web"
memcard_dir = "/saves"
disc_speed = "2x"
bios_hle = false
fast_boot = false
overlay_cache = false
overlay_backend = "interp"
turbo_loads = false

[video]
renderer = "software"
supersampling = 1
antialiasing = false
texture_filtering = "nearest"
aspect_ratio = "4:3"
low_latency_input = true

[controller]
default_analog = false
deadzone = 12000
`;
// Mapa padrão do framework (README "Keyboard Map"); escrito para o runtime não criar um próprio.
const KEYBINDS_INI = `# BAG2 web keyboard bindings (SDL key names)
[player1]
up = Up
down = Down
left = Left
right = Right
cross = X
circle = S
square = Z
triangle = A
l1 = Q
r1 = W
l2 = E
r2 = R
l3 = T
r3 = Y
start = Return
select = Right Shift
ls_up = Up
ls_down = Down
ls_left = Left
ls_right = Right
rs_up = None
rs_down = None
rs_left = None
rs_right = None

[player2]
${['up','down','left','right','cross','circle','square','triangle','l1','r1','l2','r2','l3','r3','start','select','ls_up','ls_down','ls_left','ls_right','rs_up','rs_down','rs_left','rs_right'].map(k => `${k} = None`).join('\n')}
`;

// ---- áudio: o AudioContext tem de nascer num gesto do usuário (autoplay policy) ------------
function primeWebAudio() {
  const M = window.Module;
  if (!M.psxWebAudio) {
    // 'interactive' pede o menor buffer que o navegador aceitar; a fila da ponte (targetLead) e o
    // controle dinamico de taxa cuidam da estabilidade. Jogo de ritmo: cada 10 ms contam.
    let context: AudioContext;
    try { context = new AudioContext({ latencyHint: 'interactive' }); } catch { context = new AudioContext(); }
    M.psxWebAudio = { context, nextTime: context.currentTime, sources: new Set(), started: false, underruns: 0 };
  }
  M.psxWebAudio.context.resume().catch(() => {});
}

// ---- start ----------------------------------------------------------------------------------
async function start() {
  primeWebAudio();
  started = true; updateStart();
  const M = window.Module;
  try {
    M.FS.mkdirTree('/saves');
    M.FS.mkdirTree(DISC_DIR);
    M.FS.writeFile('/keybinds.ini', KEYBINDS_INI);
    const biosPath = await installOpenBios();
    let cueName: string;
    const d = selectedDisc();
    if (d) {
      await installFile(d.bin, `${DISC_DIR}/${d.bin.name}`);
      if (d.cue) { await installFile(d.cue, `${DISC_DIR}/${d.cue.name}`); cueName = d.cue.name; }
      else { cueName = d.bin.name.replace(/\.bin$/i, '.cue'); M.FS.writeFile(`${DISC_DIR}/${cueName}`, trivialCue(d.bin.name)); }
    } else {
      // ?dev: /dev-disc.bin servido pelo plugin do vite.config.ts (BAG2_DISC ou web/.disc.json).
      const binName = 'dev-disc.bin';
      await installUrl('/dev-disc.bin', `${DISC_DIR}/${binName}`, binName);
      cueName = 'dev-disc.cue';
      M.FS.writeFile(`${DISC_DIR}/${cueName}`, trivialCue(binName));
    }
    M.FS.writeFile('/game.toml', gameToml(cueName));
    setStatus('Iniciando runtime…');
    canvas.focus();
    M.psxWebAudioLocked = false;
    M.callMain(['--game', '/game.toml', '--bios', biosPath, '--disc', `${DISC_DIR}/${cueName}`, '--renderer', 'software', '--no-launcher']);
    pauseBtn.disabled = false;
    applyAudioLead();
    setStatus('Rodando. Clique no canvas e use o teclado.');
    startPerfHud();
    startWebGpuPresenter();
  } catch (e) {
    started = false; updateStart();
    log(String((e as Error)?.stack ?? e));
    setStatus('Falha ao iniciar — veja o log.');
  }
}
startBtn.addEventListener('click', start);
// O SDL ouve o teclado na janela, mas o foco precisa estar fora de inputs/botoes; qualquer clique na
// area da tela (inclusive no canvas WebGPU sobreposto) devolve o foco ao canvas do SDL.
const screen = $<HTMLDivElement>('screen');
screen.addEventListener('pointerdown', () => canvas.focus());
document.addEventListener('keydown', e => {
  if (!started) return;
  const t = e.target as HTMLElement | null;
  if (t && (t.tagName === 'INPUT' || t.tagName === 'SELECT' || t.tagName === 'BUTTON')) { t.blur(); canvas.focus(); }
  if (e.key === 'Tab' || e.key.startsWith('Arrow')) e.preventDefault();   // nao mover o foco/scroll com as setas
});
pauseBtn.addEventListener('click', () => {
  paused = !paused;
  window.Module._psx_web_set_paused?.(paused ? 1 : 0);
  pauseBtn.textContent = paused ? 'Continuar' : 'Pausar';
  if (!paused) canvas.focus();
});

// FPS aproximado = vblanks do guest por segundo (contador exportado pelo runtime web).
function startPerfHud() {
  const M = window.Module;
  if (!M._psx_web_perf_vblanks) return;
  let last = M._psx_web_perf_vblanks(), lastT = performance.now();
  let lastWait = (M as unknown as { _psx_web_perf_pacer_wait_us?: () => number })._psx_web_perf_pacer_wait_us?.() ?? 0;
  setInterval(() => {
    applyAudioLead();   // a ponte recalcula targetLead ao abrir o dispositivo; reimpor o valor do slider
    const now = M._psx_web_perf_vblanks!(), t = performance.now();
    const fps = (now - last) * 1000 / (t - lastT);
    const waitFn = (M as unknown as { _psx_web_perf_pacer_wait_us?: () => number })._psx_web_perf_pacer_wait_us;
    const wait = waitFn ? waitFn() : 0;
    const idle = waitFn ? ((wait - lastWait) / 1000) / (t - lastT) * 100 : 0;
    lastWait = wait;
    const au = M.psxWebAudio;
    const rate = (au as unknown as { rate?: number } | undefined)?.rate;
    const queue = au ? (au.nextTime - au.context.currentTime) * 1000 : 0;
    const dev = au ? ((au.context.baseLatency || 0) + ((au.context as AudioContext & { outputLatency?: number }).outputLatency || 0)) * 1000 : 0;
    perfEl.textContent = `${fps.toFixed(1)} vblank/s (alvo 59.94) · CPU livre ${idle.toFixed(0)}% · áudio: ${au?.underruns ?? 0} underruns, fila ${queue.toFixed(0)} ms + dispositivo ${dev.toFixed(0)} ms ≈ atraso ${(queue + dev).toFixed(0)} ms, taxa ${rate ? rate.toFixed(3) : '1.000'}`;
    last = now; lastT = t;
  }, 1000);
}

// ---- latencia de audio: a ponte (web_audio_bridge.c) mantem `targetLead` segundos de fila; o fork usa
// 160 ms no Windows, demais para um jogo de ritmo. O slider ajusta em tempo real.
const leadIn = $<HTMLInputElement>('audiolead'), leadLbl = $<HTMLSpanElement>('audioleadLbl');
function applyAudioLead() {
  const ms = +leadIn.value;
  leadLbl.textContent = `${ms} ms`;
  const au = window.Module.psxWebAudio as (typeof window.Module.psxWebAudio & { targetLead?: number }) | undefined;
  if (au) au.targetLead = ms / 1000;
}
leadIn.addEventListener('input', applyAudioLead);

// ---- WebGPU: apresenta o framebuffer do renderer software com filtro "sharp bilinear" ------------
// O runtime exporta psx_web_frame_ptr/w/h/serial (patch psxrecomp-web-frame-export.patch); o canvas
// WebGPU fica sobre o canvas do SDL (que continua recebendo o foco/teclado).
const gpuCanvas = $<HTMLCanvasElement>('gpu');
const gpuToggle = $<HTMLInputElement>('usegpu');
const scanToggle = $<HTMLInputElement>('scanlines');
async function startWebGpuPresenter() {
  const M = window.Module;
  if (!M._psx_web_frame_ptr || !M._psx_web_frame_serial) { log('WebGPU: runtime sem exportação de frame (build antiga).'); return; }
  const presenter = await WebGpuPresenter.create(gpuCanvas).catch((e) => { log('WebGPU: ' + e); return undefined; });
  if (!presenter) { log('WebGPU indisponível; usando o canvas do SDL.'); gpuToggle.disabled = true; return; }
  (window as unknown as { __bag2gpu: unknown }).__bag2gpu = presenter;
  presenter.onError = (m) => log('WebGPU erro: ' + m);
  log('WebGPU: apresentando o framebuffer.');
  let lastSerial = -1, frames = 0;
  (window as unknown as { __bag2frames: () => number }).__bag2frames = () => frames;
  const tick = () => {
    const on = gpuToggle.checked;
    gpuCanvas.style.display = on ? 'block' : 'none';
    if (on && M.HEAPU8) {
      const serial = M._psx_web_frame_serial!();
      if (serial !== lastSerial) {
        lastSerial = serial;
        const ptr = M._psx_web_frame_ptr!(), w = M._psx_web_frame_w!(), h = M._psx_web_frame_h!();
        if (ptr && w && h) {
          presenter.scanlines = scanToggle.checked ? 0.35 : 0;
          // HEAPU8 pode ser substituído quando a memória cresce: ler sempre via Module.HEAPU8.
          try { presenter.present(M.HEAPU8.subarray(ptr, ptr + w * h * 4), w, h, gpuCanvas); frames++; }
          catch (e) { if (frames === 0) log('WebGPU present: ' + e); }
        }
      }
    }
    requestAnimationFrame(tick);
  };
  requestAnimationFrame(tick);
}

// ---- Module + injeção do bag2.js -------------------------------------------------------------
window.Module = {
  noInitialRun: true,
  canvas,
  psxWebAudioLocked: true,
  // As pthreads (workers) recarregam o bag2.js pelo document.currentScript.src do <script> abaixo
  // (mainScriptUrlOrBlob não está no INCOMING_MODULE_JS_API desta build).
  locateFile: (p: string) => RUNTIME_BASE + p,       // bag2.wasm etc.
  print: (t: string) => log(t),
  printErr: (t: string) => log('ERR: ' + t),
  onRuntimeInitialized() {
    runtimeReady = true;
    setStatus(devMode ? 'Runtime pronto. Clique em Iniciar (lê /dev-disc.bin).' : 'Runtime pronto. Selecione o disco.');
    updateStart();
  },
  onAbort(reason: unknown) { log('ABORT: ' + String(reason)); setStatus('Runtime abortou — veja o log.'); },
} as EmModule;

if (typeof SharedArrayBuffer === 'undefined') {
  setStatus('SharedArrayBuffer indisponível: a página precisa dos cabeçalhos COOP/COEP (use o vite dev server).');
} else {
  const s = document.createElement('script');
  s.src = RUNTIME_BASE + 'bag2.js';
  s.onerror = () => setStatus(`Não achei ${RUNTIME_BASE}bag2.js — rode powershell -File tools\\recomp\\build-web.ps1`);
  document.body.appendChild(s);
}
