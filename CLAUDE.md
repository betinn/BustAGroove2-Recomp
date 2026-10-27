# BustAGroove2-Recomp — contexto para sessões do Claude

Bust A Groove 2 (USA, SLUS-01159, **PlayStation 1**) recompilado para .exe nativo de PC via PSXRecomp.
Projeto pessoal, uso local. A versão para navegador (web/) foi **abandonada** em 2026-10-23; foco 100 % no exe.
Responder e escrever docs/commits em pt-BR. GitHub: `betinn/BustAGroove2-Recomp`; pasta local
`C:\Users\sem-m\source\GIT\BustAGroove2-Recomp` (renomeada de `BustAGroove2Web` em 2026-10-24).

**Android**: fork GPLv3 do DuckStation chamado **Polygonia** em `C:\Users\sem-m\source\GIT\Polygonia`
(GitHub `betinn/Polygonia`, público). Tudo sobre ele em `notes/android-fork.md`: commit base, toolchain
(`C:\tools\android-sdk`, JDK 11/21), bug do OpenBIOS, rename, Play. Keystore em
`C:\Users\sem-m\Polygonia-keys\` (fora de qualquer repo).

## Regras fixas
- **Nenhum dado do jogo entra no git**: .bin/.cue, XA/STR, `extracted/`, texturas extraídas/dumps, `saves/`,
  `releases/` (só `releases/README.md`). `tools/recomp/bag2/build/` é ignorado por `tools/recomp/bag2/.gitignore`.
- `releases/v1.0.0-estavel/` é backup: **nunca alterar**.
- Sem deploy/publicação. Commits com `Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>`.
- Edições de arquivo: ferramenta Edit ou Python com `encoding='utf-8'` (cp1252 corrompe acentos).
- Depois de qualquer teste: `Stop-Process -Name Bust_A_Groove_2_Recompiled -Force`.

## Onde está cada coisa
- Disco: `C:\Users\sem-m\OneDrive\Documentos\My Games\Bust a groove 2\Bust A Groove 2 (USA).cue` (+ .bin).
- Framework PSXRecomp (nativo): `C:\tools\psxrecomp` (docs em `docs/`: `WIDESCREEN.md`, `HD_TEXTURE_PACKS.md`,
  `MOD_PACKAGES.md`, `QUALITY_PRESETS.md`). Fork web (não usado mais): `C:\tools\psxrecomp-web`.
- Projeto do jogo: `tools/recomp/bag2/` — `CMakeLists.txt`, `game.toml` (config do runtime: `[video]`, `[widescreen]`),
  `settings.toml` (tela cheia), `Jogar.bat`, `src/bag2_mods.c` (plugins estáticos: widescreen),
  `mods/preloaded/packages/*/manifest.toml` (pacotes de mod ligados por padrão), `generated/` (C gerado + overlays AOT),
  `build/` (exe `Bust_A_Groove_2_Recompiled.exe`, `mods/bundled`, `mods/texture-packs/SLUS-01159/{dumps,replacements}`).
- Build: `powershell -File tools\recomp\build.ps1 -Step runtime` (só runtime/exe; `-Step all` regenera tudo).
  Ambiente: `tools/recomp/env.ps1` (GCC 16 WinLibs, CMake/Ninja). `game.toml`/`settings.toml` são copiados para
  `build/` no pós-build.
- Rodar: `tools\recomp\bag2\Jogar.bat` (acha o exe em `build\`) ou
  `Bust_A_Groove_2_Recompiled.exe --game game.toml --disc "<cue>"` dentro de `build/`. Log em stdout.
- Releases: `releases/vMAJOR.MINOR.PATCH-apelido/` com `VERSION.txt`; tabela e regras em `releases/README.md`.
  Atual: `v1.3.0-texturas-hd`. Para publicar uma versão nova: copiar `build/` (exe, `assets/`, `bios/`,
  `mods/bundled/`, `mods/texture-packs/SLUS-01159/replacements/`, `game.toml`, `settings.toml`, `keybinds.ini`,
  `input.ini`, `Jogar.bat`), escrever `VERSION.txt`, registrar em `releases/README.md` e no `README.md`.
- Notas técnicas: `notes/recomp.md` (§8 overlays AOT, §10 1080p, §11 widescreen/nitidez, §12 texturas HD), `notes/formats.md`,
  `notes/model-format.md`, `notes/media-formats.md`, `notes/remake-plano.md` (plano e estado do remake visual),
  `notes/remake-visual.md` (técnica do remake do HEAT,
  `tools/remake/`, saída em `tools/recomp/logs/remake/`). Logs e capturas: `tools/recomp/logs/`.

## Estado (2026-10-27)
- Jogo completo a 60 fps, 56 overlays compilados antecipadamente, 9x interno + PGXP + xBR, widescreen 16:9 nativo
  (menus/FMV em 4:3), tela cheia sem borda. Teclado em `keybinds.ini`; gamepad SDL automático.
- Pacote de texturas HD (v1.3.0): mod `psx.enhancement.hd-textures` ligado por override do manifest em
  `mods/preloaded/packages/psx.enhancement.hd-textures/`; 710 dumps (467 identidades) ampliados 4x com Real-ESRGAN
  (`tools/hd_textures.py --filter esrgan`, binário em `C:/tools/realesrgan/bin`, alfa via xBR, recortes aninhados
  deduplicados) em `build/mods/texture-packs/SLUS-01159/replacements/` (fora do git). Para capturar mais palcos:
  `build/mods/state.toml` com `dump = "true"`, `replacements = "false"` (strings), jogar, fechar a janela (não
  `Stop-Process`, senão perde o último lote), rodar o script de novo (incremental). Notas §12 e §12.1.
- **Framework patcheado localmente**, três patches em `tools/recomp/patches/`, nesta ordem:
  `psxrecomp-hd-cache-caps.patch` (caches do mod HD ampliados; sem isso a partida trava, 13–35 fps),
  `psxrecomp-texture-bank-render-thread.patch` (bancos de textura na thread de render, para o fallback por pacotes)
  e `psxrecomp-host-draw.patch` (desenho de host do remake; sem ele o plugin não compila). Reaplicar
  (`git apply --ignore-whitespace`) se `C:/tools/psxrecomp` for atualizado. Medição de desempenho: `build-debug/`
  (`-DPSX_DEBUG_TOOLS=ON`, TCP 4370) + `tools/remake/game_perf.py`.
- `tools/recomp/build.ps1` aborta no PowerShell 5.1 quando o cmake escreve aviso em stderr (`$ErrorActionPreference =
  'Stop'`); alternativa: rodar `cmake -S tools/recomp/bag2 -B tools/recomp/bag2/build ...` e `cmake --build ... --target
  psx-runtime` direto (flags na linha 50 do script) com o PATH de `env.ps1`.
- **Remake do HEAT no jogo** (fases 0 e 1 de `notes/remake-plano.md` concluídas; técnica em
  `notes/remake-visual.md`): plugin `src/bag2_heat.c` (hook de filtro em `FUN_80029D4C`, listado em
  `[recompiler] mod_function_entry_funcs`) entrega a pose num marcador na OT e o renderizador GL desenha com o
  núcleo portátil `src/remake/` (C99, GL 3.3/GLES 3.0: Phong Tessellation por instancing, luz por pixel,
  texturas HD com mipmaps; o mesmo código vai para o Polygonia). Pacote `mods/preloaded/packages/bag2.remake.heat`,
  asset v3 `mods/remake/heat.bin` (50 MB) gerado por `tools/remake/export_heat.py` (fora do git). Exige o patch
  local `psxrecomp-host-draw.patch`. Caminho por pacotes da fase 0 = fallback (`heat_mode` flags bit 2).
  Medido (60 s por janela): HEAT original jogo 47–52 %, render 50–62 %; remake novo 59,9 fps em todos os segundos, jogo 40–44 %, render 33–48 %; 4 HEATs (modo 6) 59,9, jogo 40 %, render 33 %; 0 underruns. Medir: `tools/remake/game_perf.py` (no máximo 2 variantes de 60 s por sessão). Testes na
  build-debug: `tools/remake/game_session.py`, `game_sidebyside.py` (modo 3 ou 7), comandos `heat_mode`,
  `heat_light`, `heat_stats`.
- As pastas `build/` e `build-debug/` foram reconfiguradas em 2026-10-26 (o cache do CMake apontava para o nome
  antigo da pasta); se o CMake reclamar de "does not match the source", apagar `CMakeCache.txt`, `CMakeFiles/` e
  `_deps/*-build`, `_deps/*-subbuild` e configurar de novo.
- Sem servidor TCP de debug na build (`PSX_DEBUG_TOOLS=OFF`): testes pelo computer-use. Toque curto de tecla não
  chega ao pad; usar `hold_key` ~0,25 s. O menu principal volta ao attract em poucos segundos.

## Próximo passo
- **Remake visual (prioridade)**: plano completo, fases e estado em `notes/remake-plano.md`. Próxima fase: 2,
  detalhe nos personagens (mãos, orelhas, mapas de normal; sombra própria e luzes do palco também cabem aqui).
  Ao avançar uma fase: atualizar a tabela de estado do `notes/remake-plano.md`, a linha "Remake do HEAT" acima e
  os achados em `notes/remake-visual.md`.
- Cobrir mais palcos no pack (dump incremental em partidas com outros personagens; Stage 1 muda por personagem).
- Upscaler melhor (xBRZ/ESRGAN) mantendo alfa discreto (0/143/255) — ver §12 sobre franjas.

## Testes com captura de tela
`PrintWindow` e `CopyFromScreen` voltam preto na janela GL sem borda. Funciona: captura de tela do app
(computer-use screenshot). O jogo lançado em segundo plano fica atrás das outras janelas: clicar nele ou
`SetForegroundWindow`. A abertura (STR) dura ~1 min e não pula com Start; seleção de personagem: `x` confirma.
