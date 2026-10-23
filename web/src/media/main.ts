// Media page: pick an XA stream from the disc, list channels, play music; play the opening FMV.
import { Disc, fileSource, urlSource, type IsoEntry } from '../bag2/disc';
import { listXaChannels, decodeXa, type XaChannelInfo } from './xa';
import { demuxStr, decodeFrame } from './str';

const $ = <T extends HTMLElement>(id: string) => document.getElementById(id) as T;
const status = $<HTMLSpanElement>('status'), xaSel = $<HTMLSelectElement>('xafile'), chSel = $<HTMLSelectElement>('channels');
const xaInfo = $<HTMLSpanElement>('xainfo'), strInfo = $<HTMLSpanElement>('strinfo'), video = $<HTMLCanvasElement>('video');
const log = (m: string) => { status.textContent = m; console.log('[media]', m); };

let disc: Disc | undefined;
let files: IsoEntry[] = [];
let sectors: Uint8Array | undefined;      // raw sectors of the selected XA file
let channels: XaChannelInfo[] = [];
let ctx: AudioContext | undefined;
let source: AudioBufferSourceNode | undefined;
let stopVideo = () => {};

function audio(): AudioContext { return (ctx ??= new AudioContext()); }
function stopAll() { source?.stop(); source = undefined; stopVideo(); }

async function openDisc(d: Disc) {
  disc = d;
  log('Lendo ISO...');
  files = await d.listRoot();
  const xa = files.filter(f => /\.XA$/i.test(f.name));
  xaSel.innerHTML = xa.map(f => `<option value="${f.name}">${f.name} (${(f.size / 1e6).toFixed(1)} MB)</option>`).join('');
  log(`${files.length} arquivos, ${xa.length} XA`);
}

async function loadXa(name: string) {
  const f = files.find(x => x.name === name)!;
  log(`Lendo ${name}...`);
  sectors = await disc!.rawSectors(f.lba, Math.ceil(f.size / 2048));
  channels = listXaChannels(sectors);
  chSel.innerHTML = channels.map((c, i) => {
    const secs = (c.sectorCount * (c.stereo ? 1 : 2) * (c.bits === 4 ? 1 : 0.5) * 4032 / (c.stereo ? 2 : 1)) / c.rate;
    return `<option value="${i}">arquivo ${c.file} canal ${c.channel}: ${c.stereo ? 'estéreo' : 'mono'} ${c.rate} Hz ${c.bits} bit, ${c.sectorCount} setores (~${secs.toFixed(0)} s)</option>`;
  }).join('');
  log(`${name}: ${channels.length} canais`);
}

$<HTMLButtonElement>('scan').addEventListener('click', () => loadXa(xaSel.value));
$<HTMLButtonElement>('playxa').addEventListener('click', async () => {
  if (!sectors || !channels.length) await loadXa(xaSel.value);
  const c = channels[+chSel.value || 0];
  stopAll();
  const t0 = performance.now();
  const pcm = decodeXa(sectors!, c.file, c.channel);
  const a = audio();
  const buf = a.createBuffer(2, pcm.left.length, pcm.rate);
  buf.copyToChannel(pcm.left as Float32Array<ArrayBuffer>, 0);
  buf.copyToChannel(pcm.right as Float32Array<ArrayBuffer>, 1);
  source = a.createBufferSource(); source.buffer = buf; source.connect(a.destination); source.start();
  xaInfo.textContent = `${(pcm.left.length / pcm.rate).toFixed(1)} s decodificados em ${(performance.now() - t0).toFixed(0)} ms`;
});
$<HTMLButtonElement>('stop').addEventListener('click', stopAll);

$<HTMLButtonElement>('playstr').addEventListener('click', async () => {
  const f = files.find(x => /BUST2_OP\.STR$/i.test(x.name));
  if (!f || !disc) return;
  stopAll();
  log('Lendo STR...');
  const raw = await disc.rawSectors(f.lba, Math.ceil(f.size / 2048));
  const dm = demuxStr(raw);
  const a = audio();
  const audioCh = listXaChannels(dm.audio)[0];
  let startAt = a.currentTime + 0.1;
  if (audioCh) {
    const pcm = decodeXa(dm.audio, audioCh.file, audioCh.channel);
    const buf = a.createBuffer(2, pcm.left.length, pcm.rate);
    buf.copyToChannel(pcm.left as Float32Array<ArrayBuffer>, 0);
    buf.copyToChannel(pcm.right as Float32Array<ArrayBuffer>, 1);
    source = a.createBufferSource(); source.buffer = buf; source.connect(a.destination); source.start(startAt);
  }
  const g = video.getContext('2d')!;
  const frames = dm.frames;
  const sps = dm.sectorsPerSecond || 150;
  strInfo.textContent = `${frames.length} frames ${frames[0]?.width}x${frames[0]?.height}, ${sps} setores/s`;
  let i = 0, running = true;
  stopVideo = () => { running = false; };
  const img = g.createImageData(frames[0]?.width ?? 320, frames[0]?.height ?? 240);
  const tick = () => {
    if (!running) return;
    const t = a.currentTime - startAt;
    while (i < frames.length && frames[i].sectorIndex / sps <= t) {
      const fr = decodeFrame(frames[i]);
      img.data.set(fr.rgba);
      if (video.width !== fr.width || video.height !== fr.height) { video.width = fr.width; video.height = fr.height; }
      g.putImageData(img, 0, 0);
      i++;
    }
    if (i < frames.length) requestAnimationFrame(tick); else log('Abertura terminada');
  };
  requestAnimationFrame(tick);
});

$<HTMLInputElement>('bin').addEventListener('change', async e => {
  const f = (e.target as HTMLInputElement).files?.[0]; if (f) await openDisc(new Disc(fileSource(f)));
});
if (new URLSearchParams(location.search).has('dev')) {
  urlSource('/dev-disc.bin').then(s => openDisc(new Disc(s))).catch(e => log('dev disc: ' + e));
}
