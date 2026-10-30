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
| `v1.2.0-widescreen` | 9x interno (2160 linhas, supersampling para 1080p), sem FXAA, **widescreen 16:9 nativo**: o jogo renderiza mais cena nas laterais, nada esticado; menus, abertura e HUD em 4:3. Plugin em `game/src/bag2_mods.c` + pacote `mods/preloaded/`; [docs/recomp.md](docs/recomp.md) §11. |
| `v1.3.0-texturas-hd` | v1.2.0 + **pacote de texturas HD** (mod `psx.enhancement.hd-textures`, formato DuckStation): 472 texturas capturadas e ampliadas 4x com Real-ESRGAN (`tools/hd_textures.py --filter esrgan`, alfa preservado) em `mods/texture-packs/SLUS-01159/replacements/`. HUD, menus, retratos, palcos e roupas nítidos; só OpenGL. [docs/recomp.md](docs/recomp.md) §12. |
| `v2.0.0-remake` | **Atual.** v1.3.0 + **remake visual completo**: os 18 personagens nos dois figurinos refeitos (Phong Tessellation, luz por pixel, normal, sombra própria, mãos com dedos, expressão do rosto), a geometria dos 15 palcos refeita, bloom e graduação de cor no tempo da música, sombra macia no chão, 620 texturas HD, ritmo da música no vblank real. Pasta auto-suficiente: dois cliques no exe, disco em `disc/`. [docs/remake-plano.md](docs/remake-plano.md). |

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
`game/` e o framework fica em `C:\tools\psxrecomp` (commit `31df2e1e` + os patches de
`recomp/patches/`). Com tudo gerado, recompilar só o exe:

```
powershell -File recomp\build.ps1 -Step runtime
```

## Estrutura do repositório

| Pasta | O que tem |
|---|---|
| [`game/`](game) | O projeto do jogo: `CMakeLists.txt`, `game.toml` (config do runtime e dos ganchos), `settings.toml`, `src/` (plugins: widescreen, remake dos personagens `bag2_heat.c`, remake do palco `bag2_stage.c`, núcleo portátil `src/remake/`), `mods/preloaded/` (mods ligados por padrão), `seeds/`. Fora do git: `generated/` (C gerado), `build/`, `build-debug/` |
| [`recomp/`](recomp) | Build: `build.ps1`, `env.ps1` (toolchain), `patches/` (os 3 patches do PSXRecomp), `aot/` (perfil dos overlays AOT) |
| [`tools/remake/`](tools/remake) | O que gera os assets do remake: leitura dos modelos do disco (`bag2model.py`, `rig.py`), exportadores (`export_char.py`, `export_all.py`, `export_stage.py`), mãos, rosto e orelhas, e a captura de dados com o jogo rodando (`game_session.py`, `stage_*_keys.py`, `stage_clut_vars.py`, `stage_inventory.py`, `stage_dump*.py`) |
| [`tools/tests/`](tools/tests) | Testes e medições no jogo rodando pela build de testes: regressão (`chars_check.py`, `stage_check.py`), A/B, desempenho (`game_perf.py`, `stage_perf.py`), áudio, memória, savestate |
| [`tools/extract/`](tools/extract) | Extração e engenharia reversa do disco: `bust2_extract.py`, BPE, TIM, STR, XA, disassembler |
| `tools/hd_textures.py` | Amplia os dumps de textura (Real-ESRGAN) para o pacote HD |
| [`docs/`](docs) | Notas técnicas: framework e build (`recomp.md`), formatos do disco, plano e técnica do remake, Android |
| [`disc/`](disc) | O disco: só o `README.md` com a identidade do dump vai para o git |
| [`releases/`](releases) | Versões prontas (só os textos no git) |
| [`archive/`](archive) | O que não se usa mais: versão web, IA 3D descartada, Ghidra, roteiro antigo |

Fora do git também: `extracted/` (disco extraído) e `logs/` (saída dos scripts, venv dos exportadores, capturas).

## Regras

- **Nenhum dado do jogo entra no repositório**: `.gitignore` bloqueia `.bin/.cue/.XA/.STR/SLUS_*`,
  `extracted/`, `bios/`, `saves/`, `releases/*` (exceto o README e os textos das releases) e as texturas capturadas/ampliadas
  (`texture-packs/`). O jogo é lido do disco local do usuário, em `disc/` (só o `disc/README.md` é versionado).
- Sem deploy nem publicação de binários do jogo.

## Android (Polygonia)

No celular/tablet o jogo roda pelo [Polygonia](https://github.com/betinn/Polygonia), fork GPLv3 do
DuckStation (versão de 2021, último commit com o app Android) rebatizado, com OpenBIOS embutido e boot
direto no jogo escolhido. Padrões: Vulkan, 4x, PGXP, widescreen 16:9. Histórico completo, toolchain e
decisões em [docs/android-fork.md](docs/android-fork.md).

## Ferramentas de engenharia reversa

Usadas para entender o disco e o executável.
- `tools/extract/bust2_extract.py`: lê a imagem `.bin` MODE2/2352 + ISO9660 e abre o arquivão `BUST2.BIN`
  (containers `DF2_`/`DFS_` aninhados, overlays `DBI_`, compressão BPE em `tools/extract/bpe.py`, bancos de
  som `SEDT`). Formato em [docs/formats.md](docs/formats.md).
- `tools/extract/tim2png.py`, `tools/extract/str2png.py`, `tools/extract/xa2wav.py`: texturas TIM, vídeo STR e música XA.
  Formatos em [docs/model-format.md](docs/model-format.md) e [docs/media-formats.md](docs/media-formats.md).
- `tools/extract/exe_dis.py`, `tools/extract/exe_scan.py`: disassembler rápido com capstone.
- `archive/ghidra/run.sh`: Ghidra headless com `ghidra_psx_ldr`; assinaturas PsyQ nomearam 906 funções de lib,
  restam 348 funções do jogo. Pseudo-C em `archive/ghidra/out/` (fora do git).

```bash
python tools/extract/bust2_extract.py "<caminho>/Bust A Groove 2 (USA).bin" extracted
python tools/extract/tim2png.py extracted/bust2 extracted/png
python tools/extract/exe_dis.py extracted/disc/SLUS_011.59 80035dc4 80035f60
```

Ferramentas instaladas: Python 3.14 + capstone + py7zr, Node 24, Git, Ghidra 12.1.4 + ghidra_psx_ldr em
`C:\tools`, JDK 21. Toolchain C/CMake: [docs/recomp.md](docs/recomp.md).

## Histórico: versão web (abandonada)

O repositório nasceu como "BustAGroove2Web": o jogo no navegador. Abandonada em 2026-10-23 em favor
do exe nativo; a pasta `archive/web/` fica como registro e não recebe mais trabalho.
- `archive/web/index.html`: baseline com EmulatorJS self-hosted (`archive/web-recomp/setup_emulatorjs.py`), core
  `pcsx_rearmed` com HLE BIOS.
- `archive/web/viewer.html`: viewer WebGPU de modelos, texturas (VRAM emulada, CLUT no shader) e animações.
- `archive/web/media.html`: música CD-XA e abertura STR/MDEC decodificadas em TypeScript.
- `archive/web/recomp.html`: o mesmo código recompilado pelo PSXRecomp compilado para WebAssembly (Emscripten,
  fork kem0x), renderer software, SPU via Web Audio; partida completa a 60 vblank/s. Build:
  `archive/web-recomp/build-web.ps1`; patches em `archive/web-recomp/patches/psxrecomp-web-*.patch`;
  [docs/recomp.md](docs/recomp.md) §7 e §9.
- Ficou em aberto: rasterizador do GPU em WebGPU (o renderer era software). Plano original em
  [archive/roadmap.md](archive/roadmap.md).

Rodar (se quiser): `cd archive/web && npm run dev`, páginas em `http://localhost:5173/?dev`, `/recomp.html?dev`,
`/viewer.html?dev`, `/media.html?dev` (`?dev` usa o caminho em `archive/web/.disc.json`).
