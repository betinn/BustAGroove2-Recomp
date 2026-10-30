# BustAGroove2-Recomp

**Bust A Groove 2 (USA, SLUS-01159)** recompilado para `.exe` nativo de PC via
[PSXRecomp](https://github.com/RetroPortingToolKit/psxrecomp): 60 fps, 9x interno, PGXP, widescreen 16:9 nativo e
pacote de texturas HD. Projeto pessoal, uso local, sem distribuição de dados do jogo.

> O jogo é de **PlayStation 1**, não PS2: CPU MIPS R3000A, GPU 2D com rasterização de triângulos, GTE
> (coprocessador de geometria), SPU ADPCM, CD-ROM XA. É a plataforma com mais ferramentas de
> decompilação/recompilação disponíveis.

## Jogar no PC

Versões prontas ficam em `releases/` (fora do git; só [releases/README.md](releases/README.md) e, da v2.0.0 em
diante, os arquivos de texto de cada release são versionados). Pastas `vMAJOR.MINOR.PATCH-apelido`, cada uma com `VERSION.txt`.

| Versão | Conteúdo |
|---|---|
| `v1.0.0-estavel` | Build nativa original: resolução 320x240, janela. Backup, não alterar. |
| `v1.1.0-1080p` | 5x interno (1080p), PGXP (geometria sem tremor, texturas em perspectiva), xBR, FXAA, sem dithering, tela cheia sem borda (F11 alterna). |
| `v1.2.0-widescreen` | 9x interno (2160 linhas, supersampling para 1080p), sem FXAA, **widescreen 16:9 nativo**: o jogo renderiza mais cena nas laterais, nada esticado; menus, abertura e HUD em 4:3. Plugin em `tools/recomp/bag2/src/bag2_mods.c` + pacote `mods/preloaded/`; [notes/recomp.md](notes/recomp.md) §11. |
| `v1.3.0-texturas-hd` | v1.2.0 + **pacote de texturas HD** (mod `psx.enhancement.hd-textures`, formato DuckStation): 472 texturas capturadas e ampliadas 4x com Real-ESRGAN (`tools/hd_textures.py --filter esrgan`, alfa preservado) em `mods/texture-packs/SLUS-01159/replacements/`. HUD, menus, retratos, palcos e roupas nítidos; só OpenGL. [notes/recomp.md](notes/recomp.md) §12. |
| `v2.0.0-remake` | **Atual.** v1.3.0 + **remake visual completo**: os 18 personagens nos dois figurinos refeitos (Phong Tessellation, luz por pixel, normal, sombra própria, mãos com dedos, expressão do rosto), a geometria dos 15 palcos refeita, bloom e graduação de cor no tempo da música, sombra macia no chão, 620 texturas HD, ritmo da música no vblank real. Pasta auto-suficiente: dois cliques no exe, disco em `disc/`. [notes/remake-plano.md](notes/remake-plano.md). |

Para jogar:
1. Tenha o disco original como `.cue` + `.bin` (o repositório não contém o jogo; o dump usado está descrito em
   [disc/README.md](disc/README.md)).
2. Na `v2.0.0-remake`: dois cliques em `Bust_A_Groove_2_Recompiled.exe` (o disco fica em `disc/` dentro da
   pasta da release). Nas anteriores: rode `Jogar.bat` na pasta da release (usa o `.cue` da pasta padrão do
   disco; aceita outro caminho como argumento) ou:
   ```
   Bust_A_Groove_2_Recompiled.exe --game game.toml --disc "<arquivo.cue>"
   ```
3. Teclado (`keybinds.ini`): setas = direcional, `X` = ✕, `S` = ○, `Z` = □, `A` = △, `Q/W` = L1/R1,
   `E/R` = L2/R2, `Enter` = Start, `Shift direito` = Select. Gamepads SDL funcionam direto.

Ajustes: `game.toml` `[video]` (`internal_resolution`, `texture_filtering`, `antialiasing_mode`,
`dithering`, `geometry_correction`, `aspect_ratio` "16:9"/"4:3") e `settings.toml` (`fullscreen`,
`window_width`). Memory card em `saves/` (fora do git).

## Compilar

**Passo a passo do zero em [BUILD.md](BUILD.md)**: requisitos e versões, o framework com os patches, a extração
do disco, o C gerado, os overlays AOT, o exe, os assets do remake, as texturas HD e a montagem de uma release.

Resumo: o disco vai em `disc/` (identidade do dump em [disc/README.md](disc/README.md)); o projeto do jogo é
`tools/recomp/bag2/` e o framework fica em `C:\tools\psxrecomp` (commit `31df2e1e` + os patches de
`tools/recomp/patches/`). Com tudo gerado, recompilar só o exe:

```
powershell -File tools\recomp\build.ps1 -Step runtime
```

## Regras

- **Nenhum dado do jogo entra no repositório**: `.gitignore` bloqueia `.bin/.cue/.XA/.STR/SLUS_*`,
  `extracted/`, `bios/`, `saves/`, `releases/*` (exceto o README e os textos das releases) e as texturas capturadas/ampliadas
  (`texture-packs/`). O jogo é lido do disco local do usuário, em `disc/` (só o `disc/README.md` é versionado).
- Sem deploy nem publicação de binários do jogo.

## Android (Polygonia)

No celular/tablet o jogo roda pelo [Polygonia](https://github.com/betinn/Polygonia), fork GPLv3 do
DuckStation (versão de 2021, último commit com o app Android) rebatizado, com OpenBIOS embutido e boot
direto no jogo escolhido. Padrões: Vulkan, 4x, PGXP, widescreen 16:9. Histórico completo, toolchain e
decisões em [notes/android-fork.md](notes/android-fork.md).

## Ferramentas de engenharia reversa

Usadas para entender o disco e o executável; continuam no repositório.
- `tools/bust2_extract.py`: lê a imagem `.bin` MODE2/2352 + ISO9660 e abre o arquivão `BUST2.BIN`
  (containers `DF2_`/`DFS_` aninhados, overlays `DBI_`, compressão BPE em `tools/bpe.py`, bancos de
  som `SEDT`). Formato em [notes/formats.md](notes/formats.md).
- `tools/tim2png.py`, `tools/str2png.py`, `tools/xa2wav.py`: texturas TIM, vídeo STR e música XA.
  Formatos em [notes/model-format.md](notes/model-format.md) e [notes/media-formats.md](notes/media-formats.md).
- `tools/exe_dis.py`, `tools/exe_scan.py`: disassembler rápido com capstone.
- `ghidra/run.sh`: Ghidra headless com `ghidra_psx_ldr`; assinaturas PsyQ nomearam 906 funções de lib,
  restam 348 funções do jogo. Pseudo-C em `ghidra/out/` (fora do git).

```bash
python tools/bust2_extract.py "<caminho>/Bust A Groove 2 (USA).bin" extracted
python tools/tim2png.py extracted/bust2 extracted/png
python tools/exe_dis.py extracted/disc/SLUS_011.59 80035dc4 80035f60
```

Ferramentas instaladas: Python 3.14 + capstone + py7zr, Node 24, Git, Ghidra 12.1.4 + ghidra_psx_ldr em
`C:\tools`, JDK 21. Toolchain C/CMake: [notes/recomp.md](notes/recomp.md).

## Histórico: versão web (abandonada)

O repositório nasceu como "BustAGroove2Web": o jogo no navegador. Abandonada em 2026-10-23 em favor
do exe nativo; a pasta `web/` fica como registro e não recebe mais trabalho.
- `web/index.html`: baseline com EmulatorJS self-hosted (`tools/setup_emulatorjs.py`), core
  `pcsx_rearmed` com HLE BIOS.
- `web/viewer.html`: viewer WebGPU de modelos, texturas (VRAM emulada, CLUT no shader) e animações.
- `web/media.html`: música CD-XA e abertura STR/MDEC decodificadas em TypeScript.
- `web/recomp.html`: o mesmo código recompilado pelo PSXRecomp compilado para WebAssembly (Emscripten,
  fork kem0x), renderer software, SPU via Web Audio; partida completa a 60 vblank/s. Build:
  `tools/recomp/build-web.ps1`; patches em `tools/recomp/patches/psxrecomp-web-*.patch`;
  [notes/recomp.md](notes/recomp.md) §7 e §9.
- Ficou em aberto: rasterizador do GPU em WebGPU (o renderer era software). Plano original em
  [notes/roadmap.md](notes/roadmap.md).

Rodar (se quiser): `cd web && npm run dev`, páginas em `http://localhost:5173/?dev`, `/recomp.html?dev`,
`/viewer.html?dev`, `/media.html?dev` (`?dev` usa o caminho em `web/.disc.json`).
