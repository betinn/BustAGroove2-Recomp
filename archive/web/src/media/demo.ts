/**
 * Small browser hooks to try the media decoders: play one XA channel through
 * WebAudio, or play an STR movie (video on a canvas, interleaved XA audio
 * through WebAudio, frames timed from their sector position so audio and
 * video stay in sync).
 */

import { decodeXa, listXaChannels, type XaPcm } from './xa.ts';
import { MdecDecoder, demuxStr, type StrFrame } from './str.ts';

export interface Playback {
  /** Stops playback and releases the audio graph. */
  stop(): void;
  /** Total duration in seconds. */
  duration: number;
}

let sharedCtx: AudioContext | null = null;
function audioContext(): AudioContext {
  if (!sharedCtx) sharedCtx = new AudioContext();
  return sharedCtx;
}

function toAudioBuffer(ctx: AudioContext, pcm: XaPcm): AudioBuffer {
  const buf = ctx.createBuffer(2, pcm.left.length, pcm.rate);
  // decodeXa always allocates plain ArrayBuffer-backed arrays; the cast only narrows the generic.
  buf.copyToChannel(pcm.left as Float32Array<ArrayBuffer>, 0);
  buf.copyToChannel(pcm.right as Float32Array<ArrayBuffer>, 1);
  return buf;
}

/**
 * Decodes `fileNo`/`channel` from a raw-sector .XA file and plays it.
 * The whole file is read into memory (the disc's XA files are 5-95 MB) and
 * decoded in one go (about 100 ms for a 3-minute channel).
 */
export async function playXaFromFile(file: File, fileNo: number, channel: number): Promise<Playback> {
  const sectors = new Uint8Array(await file.arrayBuffer());
  const channels = listXaChannels(sectors);
  console.log('[xa] channels', channels);
  const pcm = decodeXa(sectors, fileNo, channel);
  if (pcm.left.length === 0) throw new Error(`no audio for file ${fileNo} channel ${channel}`);
  const ctx = audioContext();
  await ctx.resume();
  const src = ctx.createBufferSource();
  src.buffer = toAudioBuffer(ctx, pcm);
  src.connect(ctx.destination);
  src.start();
  return { stop: () => { try { src.stop(); } catch { /* already stopped */ } src.disconnect(); }, duration: pcm.left.length / pcm.rate };
}

/**
 * Plays a raw-sector .STR movie on `canvas`. Video frames are presented at
 * sectorIndex / sectorsPerSecond seconds, which matches the audio clock
 * exactly because the audio sectors come from the same stream.
 */
export async function playStr(file: File, canvas: HTMLCanvasElement): Promise<Playback> {
  const sectors = new Uint8Array(await file.arrayBuffer());
  const movie = demuxStr(sectors);
  if (movie.frames.length === 0) throw new Error('no video frames found');
  const first = movie.frames[0];
  canvas.width = first.width;
  canvas.height = first.height;
  const g = canvas.getContext('2d');
  if (!g) throw new Error('2d context unavailable');
  const image = g.createImageData(first.width, first.height);
  const decoder = new MdecDecoder();

  const ctx = audioContext();
  await ctx.resume();
  let src: AudioBufferSourceNode | null = null;
  const audioChannels = listXaChannels(movie.audio);
  if (audioChannels.length > 0) {
    const a = audioChannels[0];
    const pcm = decodeXa(movie.audio, a.file, a.channel);
    src = ctx.createBufferSource();
    src.buffer = toAudioBuffer(ctx, pcm);
    src.connect(ctx.destination);
  }
  const lastFrame = movie.frames[movie.frames.length - 1];
  const duration = (lastFrame.sectorIndex + lastFrame.chunkCount + 1) / movie.sectorsPerSecond;
  console.log(`[str] ${movie.frames.length} frames ${first.width}x${first.height}, ${movie.fps.toFixed(2)} fps, ${duration.toFixed(1)} s`);

  const t0 = ctx.currentTime + 0.05;
  if (src) src.start(t0);
  let shown: StrFrame | null = null;
  let next = 0;
  let raf = 0;
  const tick = () => {
    const elapsedSectors = (ctx.currentTime - t0) * movie.sectorsPerSecond;
    // Advance to the newest frame whose first sector has already been "read".
    while (next < movie.frames.length && movie.frames[next].sectorIndex <= elapsedSectors) next++;
    const frame = next > 0 ? movie.frames[next - 1] : null;
    if (frame && frame !== shown) {
      shown = frame;
      decoder.decode(frame.data, frame.width, frame.height, image.data);
      g.putImageData(image, 0, 0);
    }
    if (next < movie.frames.length || ctx.currentTime - t0 < duration) raf = requestAnimationFrame(tick);
  };
  raf = requestAnimationFrame(tick);
  return {
    stop: () => {
      cancelAnimationFrame(raf);
      if (src) { try { src.stop(); } catch { /* already stopped */ } src.disconnect(); }
    },
    duration,
  };
}
