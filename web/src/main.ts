// Baseline: boots the local disc image inside EmulatorJS (libretro cores compiled to WASM).
// EmulatorJS is self-hosted from /emulatorjs/data (not committed; see README).
declare global {
  interface Window {
    EJS_player: string; EJS_core: string; EJS_gameUrl: File | string; EJS_gameName: string;
    EJS_biosUrl: File | string; EJS_pathtodata: string; EJS_startOnLoaded: boolean;
    EJS_threads: boolean; EJS_disableCue: boolean; EJS_language: string; EJS_ready?: () => void;
    EJS_onGameStart?: () => void; EJS_emulator?: unknown;
  }
}

const $ = <T extends HTMLElement>(id: string) => document.getElementById(id) as T;
const binInput = $<HTMLInputElement>('bin');
const biosInput = $<HTMLInputElement>('bios');
const coreSel = $<HTMLSelectElement>('core');
const bootBtn = $<HTMLButtonElement>('boot');
const status = $<HTMLParagraphElement>('status');

const log = (m: string) => { status.textContent = m; console.log('[bag2]', m); };

binInput.addEventListener('change', () => {
  const files = Array.from(binInput.files ?? []);
  const bin = files.find(f => /\.(bin|iso|chd|pbp)$/i.test(f.name));
  bootBtn.disabled = !bin;
  log(bin ? `${bin.name} (${(bin.size / 1e6).toFixed(1)} MB) pronto` : 'Selecione o .bin');
});

function boot(bin: File | string) {
  bootBtn.disabled = true;
  log(`Iniciando ${coreSel.value}…`);

  window.EJS_player = '#game';
  window.EJS_core = coreSel.value;
  window.EJS_gameName = 'Bust A Groove 2';
  window.EJS_gameUrl = bin;            // File object: fica em memória, nunca sai da máquina
  window.EJS_biosUrl = biosInput.files?.[0] ?? '';
  window.EJS_pathtodata = '/emulatorjs/data/';
  window.EJS_startOnLoaded = true;
  window.EJS_threads = typeof SharedArrayBuffer !== 'undefined';
  window.EJS_disableCue = false;       // EmulatorJS gera o .cue sozinho para um .bin único
  window.EJS_language = 'pt-BR';
  window.EJS_ready = () => log('Emulador carregado, bootando disco…');
  window.EJS_onGameStart = () => log('Jogo rodando.');

  const s = document.createElement('script');
  s.src = '/emulatorjs/data/loader.js';
  s.onerror = () => log('Falha ao carregar /emulatorjs/data/loader.js — rode tools/setup_emulatorjs.py');
  document.body.appendChild(s);
}

bootBtn.addEventListener('click', () => {
  const files = Array.from(binInput.files ?? []);
  const bin = files.find(f => /\.(bin|iso|chd|pbp)$/i.test(f.name));
  if (bin) boot(bin);
});

// Dev shortcut: /?dev[&core=mednafen_psx_hw] boots the disc served by the Vite plugin (see vite.config.ts).
const params = new URLSearchParams(location.search);
if (params.has('dev')) {
  const c = params.get('core'); if (c) coreSel.value = c;
  boot('/dev-disc.bin');
}
