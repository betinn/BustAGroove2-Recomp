# Roteiro: do disco PS1 ao jogo nativo no navegador

Três camadas, cada uma útil sozinha. Ordem de ataque: 1 → 2 → 3.

## 1. Emulação (feito)
`web/` roda o disco original num emulador PS1 compilado para WebAssembly (EmulatorJS, core
`pcsx_rearmed`). Nada do jogo é reimplementado. Serve para:
- jogar já;
- gerar referência: screenshots, save states, valores de RAM para comparar com nossa versão;
- debugar: o core expõe memória, dá para inspecionar as estruturas que o exe monta em RAM.

## 2. Assets e formatos (em andamento)
Extrair tudo do `BUST2.BIN` e saber renderizar/tocar sem emulador:
- Texturas TIM → PNG (feito, `tools/tim2png.py`).
- Modelos/animações: formato próprio da Metro (blobs `bin` ainda sem nome em `notes/formats.md`).
  Caminho: achar no exe a função que lê esse blob (a partir dos callers de `dfs_get`), decompilar no
  Ghidra, traduzir para um parser TypeScript, desenhar em WebGPU.
- Música XA-ADPCM → decodificar em JS (referência: jPSXdec) e tocar via WebAudio.
- FMV STR (MDEC) → decodificar em JS (referência: jPSXdec).
- Sons SPU ADPCM (VAB dentro de `SEDT`).

## 3. Recompilação estática (próximo)
Transformar o código MIPS do `SLUS_011.59` (+ overlays `DBI_`) em C, compilar com Emscripten para
WASM, e substituir o hardware do PS1 por implementações web:
- GPU → WebGPU (rasterizador que imita o GPU do PS1: VRAM 1024×512 de 16 bits, primitivas
  GP0, texturas 4/8/16 bpp com CLUT, blending, dithering).
- GTE → funções C/JS (matemática de ponto fixo, já existe em todo emulador).
- SPU → WebAudio.
- CD-ROM → leitura direta do `.bin` carregado no navegador (File API).
- BIOS → OpenBIOS (PCSX-Redux, MIT) ou HLE das poucas syscalls usadas (lista em `tools/exe_scan.py`).

Ferramenta: **PSXRecomp** (MIPS→C, inclui runtime de hardware). Prova pública: "Pepsiman Recompiled"
rodando no navegador. Antes disso o Ghidra com `ghidra_psx_ldr` + assinaturas PsyQ separa o que é lib
da Sony (libgpu, libgte, libspu, libcd, libsnd…) do código do jogo em si. Só o código do jogo precisa
ser entendido; as libs têm API documentada (PsyQ SDK docs).

## Como ler o exe (para aprender)
- `tools/exe_scan.py SLUS_011.59 <const_hex>` acha onde uma constante (magic, endereço) é montada
  com `lui`/`ori`; mostra também as syscalls da BIOS usadas.
- `tools/exe_dis.py SLUS_011.59 <início> <fim>` desassembla um trecho.
- `ghidra/out/decomp.c` (gerado pelo headless) tem o pseudo-C de todas as funções. Procure por
  `dfs_get`-like (`0x8001f8cc`) e siga os callers.
- Convenção MIPS O32: args em `$a0..$a3`, retorno em `$v0`, `$sp` pilha, `$ra` endereço de retorno,
  `$s0..$s7` preservados pela função chamada. Branch tem *delay slot*: a instrução seguinte ao
  `j/jal/b*` executa sempre.
