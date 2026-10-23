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

Pendente (ordem sugerida):
1. **Baseline jogável**: emulador PS1 em WASM (EmulatorJS + core `mednafen_psx` + OpenBIOS, sem BIOS
   dumpada) carregando o `.bin` local. Serve de referência visual e para comparar comportamento.
2. **Formatos próprios**: ~~modelos/animações~~ feitos; faltam step-charts (containers 15–50), eventos de
   animação (footer), posicionamento exato de texturas na VRAM e matrizes de luz (código dos overlays).
3. **Recompilação estática**: Ghidra + `ghidra_psx_ldr` + assinaturas PsyQ para separar libs Sony do
   código do jogo; então PSXRecomp (MIPS → C → Emscripten → WASM), com GPU/SPU reimplementados em
   WebGPU/WebAudio. Prova de conceito existente: "Pepsiman Recompiled" roda no navegador por esse caminho.

## Ferramentas instaladas / faltando
Tem: Python 3.14 + capstone + py7zr, Node 24, Git, Ghidra 12.1.4 + ghidra_psx_ldr em `C:	ools`, JDK 21 portátil em `C:	ools`. Falta: Emscripten (emsdk), CMake, compilador C (workload C++ do VS 2022).

## Uso
```bash
python tools/bust2_extract.py "<caminho>/Bust A Groove 2 (USA).bin" extracted
python tools/tim2png.py extracted/bust2 extracted/png
python tools/exe_dis.py extracted/disc/SLUS_011.59 80035dc4 80035f60
cd web && npm run dev
```
Páginas em dev: `http://localhost:5173/?dev` (emulador), `/viewer.html?dev` (modelos), `/media.html?dev` (música/abertura).
`?dev` usa o caminho em `web/.disc.json`; sem ele, use o seletor de arquivo da página.
