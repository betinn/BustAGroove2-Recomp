# BustAGroove2Web

Reconstrução de **Bust A Groove 2 (USA, SLUS-01159)** para rodar no navegador, via engenharia reversa do
executável original, WebAssembly e WebGPU. Projeto pessoal, uso local apenas.

> Correção importante: o jogo é de **PlayStation 1**, não PS2. CPU MIPS R3000A, GPU 2D com
> rasterização de triângulos, GTE (coprocessador de geometria), SPU ADPCM, CD-ROM XA. Isso é
> bom: o PS1 é a plataforma com mais ferramentas de decompilação/recompilação disponíveis.

## Regras
- **Nenhum dado do jogo entra no repositório** (`.gitignore` bloqueia `.bin/.cue/.XA/.STR/SLUS_*/extracted/`).
  O navegador lê a imagem do disco do disco local do usuário (File System Access API / `<input type=file>`).
- Sem deploy. Tudo roda em `localhost`.

## Estado atual (2026-10-22)
Feito:
- Leitura da imagem `.bin` MODE2/2352 + ISO9660 direto em Python (`tools/bust2_extract.py`).
- Formato do arquivão `BUST2.BIN` decifrado: containers `DF2_`/`DFS_` aninhados, `DBI_` (overlays de
  código copiados para `0x8008F6C0`), `BPE_` (compressão Byte Pair Encoding, decoder portado do exe em
  `tools/bpe.py`), `SEDT` (bancos de som com `pBAV`).
- 750 texturas TIM extraídas e convertidas para PNG (`tools/tim2png.py`).
- Disassembler rápido com capstone (`tools/exe_dis.py`, `tools/exe_scan.py`).
- **Ghidra headless** (`ghidra/run.sh`): exe importado com `ghidra_psx_ldr`, assinaturas PsyQ nomearam 906
  funções de lib; restam 348 funções de código do jogo (107 KB). Pseudo-C completo em `ghidra/out/decomp.c`.
- Detalhes em [notes/formats.md](notes/formats.md); plano e guia de leitura em [notes/roadmap.md](notes/roadmap.md).
- **Baseline jogável no navegador** (`web/index.html`): EmulatorJS self-hosted em `web/public/emulatorjs/`
  (não versionado; gere com `tools/setup_emulatorjs.py <release.7z>`), core `pcsx_rearmed` com HLE BIOS
  (sem arquivo de BIOS). O `.bin` entra por seletor de arquivo ou, em dev, por `/?dev`. O jogo boota e roda.
  O core `mednafen_psx_hw` exige BIOS real (`scph5501.bin`).
- **Viewer WebGPU** (`web/viewer.html`): lê o `.bin`, abre o `BUST2.BIN`, decodifica modelo (esqueleto +
  32 tipos de primitiva), texturas (VRAM 1024×512 emulada, amostragem 4/8/16 bpp + CLUT no shader) e
  **animações** (streams delta de 12 bits) de qualquer personagem. Formato em [notes/model-format.md](notes/model-format.md).
- **Mídia** (`web/media.html`): música CD-XA ADPCM e abertura STR/MDEC decodificadas em TypeScript,
  tocando via WebAudio/canvas. Formato em [notes/media-formats.md](notes/media-formats.md).

- **Jogo recompilado rodando no navegador** (`web/recomp.html`): o `SLUS_011.59` foi traduzido de MIPS
  para C pelo PSXRecomp e compilado para WebAssembly (Emscripten, fork kem0x). OpenBIOS (MIT) no lugar
  da BIOS da Sony, renderer software num canvas, SPU via Web Audio, teclado como controle. Boot, abertura,
  menu, seleção de personagem e partida completa funcionam (~55–60 vblank/s). Overlays `DBI_` rodam no
  interpretador MIPS do runtime. Build: `powershell -File tools\recomp\build-web.ps1`; detalhes em
  [notes/recomp.md](notes/recomp.md) §7.
- **Build nativo** (`tools/recomp/bag2/build/Bust_A_Groove_2_Recompiled.exe`): mesmo código em
  OpenGL, 60 fps, com os 56 overlays únicos compilados antecipadamente a partir do disco
  (`tools/recomp/aot/overlays.json`, notes §8).

Pendente:
1. Rasterizador do GPU do PS1 em WebGPU (hoje o renderer é software; WebGPU só apresenta o frame).
   Ganho esperado: filtros/resolução interna maior, menos CPU na abertura (MDEC).
2. Formatos ainda em aberto: semântica das faixas em `001.bin` dos containers 15–50, eventos de
   animação (footer), matrizes de luz (código dos overlays).

## Ferramentas instaladas
Python 3.14 + capstone + py7zr, Node 24, Git, Ghidra 12.1.4 + ghidra_psx_ldr em `C:\tools`, JDK 21
(portátil em `C:\tools` e instalado via winget). Toolchain C/CMake/emsdk: ver [notes/recomp.md](notes/recomp.md).

## Uso
```bash
python tools/bust2_extract.py "<caminho>/Bust A Groove 2 (USA).bin" extracted
python tools/tim2png.py extracted/bust2 extracted/png
python tools/exe_dis.py extracted/disc/SLUS_011.59 80035dc4 80035f60
cd web && npm run dev
```
Páginas em dev: `http://localhost:5173/?dev` (emulador), `/recomp.html?dev` (**jogo recompilado em WASM**), `/viewer.html?dev` (modelos), `/media.html?dev` (música/abertura).
`?dev` usa o caminho em `web/.disc.json`; sem ele, use o seletor de arquivo da página.
