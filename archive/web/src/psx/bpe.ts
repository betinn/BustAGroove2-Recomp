// Byte Pair Encoding decoder, port of SLUS_011.59 @ 0x80035DC4 (see tools/extract/bpe.py).
export function bpeDecode(src: Uint8Array): Uint8Array {
  if (src[0] !== 0x42 || src[1] !== 0x50 || src[2] !== 0x45 || src[3] !== 0x5f) throw new Error('not BPE_');
  const end = (src[4] << 24 | src[5] << 16 | src[6] << 8 | src[7]) >>> 0;
  let p = 8;
  const out: number[] = [];
  const left = new Uint8Array(256), right = new Uint8Array(256);
  const stack: number[] = [];
  while (p < end) {
    for (let i = 0; i < 256; i++) left[i] = i;
    let c = 0;
    while (c < 256) {
      let n = src[p++];
      if (n > 127) { c += n - 127; if (c === 256) break; n = 0; }
      for (let i = 0; i <= n; i++) {
        left[c] = src[p++];
        if (left[c] !== c) right[c] = src[p++];
        c++;
      }
    }
    let count = (src[p] << 8) | src[p + 1]; p += 2;
    stack.length = 0;
    for (;;) {
      let ch: number;
      if (stack.length) ch = stack.pop()!;
      else { if (count === 0) break; count--; ch = src[p++]; }
      if (left[ch] === ch) out.push(ch);
      else { stack.push(right[ch]); stack.push(left[ch]); }
    }
  }
  return Uint8Array.from(out);
}
