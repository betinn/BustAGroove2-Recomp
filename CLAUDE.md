# BustAGroove2-Recomp — contexto para sessões do Claude

Bust A Groove 2 (USA, SLUS-01159, **PlayStation 1**) recompilado para .exe nativo de PC via PSXRecomp.
Projeto pessoal, uso local. A versão para navegador (archive/web/) foi **abandonada** em 2026-10-23; foco 100 % no exe.
Responder e escrever docs/commits em pt-BR. GitHub: `betinn/BustAGroove2-Recomp`; pasta local
`C:\Users\sem-m\source\GIT\BustAGroove2-Recomp` (renomeada de `BustAGroove2Web` em 2026-10-24).

**Android**: fork GPLv3 do DuckStation chamado **Polygonia** em `C:\Users\sem-m\source\GIT\Polygonia`
(GitHub `betinn/Polygonia`, público). Tudo sobre ele em `docs/android-fork.md`: commit base, toolchain
(`C:\tools\android-sdk`, JDK 11/21), bug do OpenBIOS, rename, Play. Keystore em
`C:\Users\sem-m\Polygonia-keys\` (fora de qualquer repo).

## Regras fixas
- (só `releases/README.md` e os textos de cada release desde a v2.0.0: `VERSION.txt`, `game.toml`,
  `settings.toml`, `keybinds.ini`, `input.ini`; nova release = novas exceções no `.gitignore`). `game/build/` é ignorado por `game/.gitignore`.
- `releases/v1.0.0-estavel/` é backup: **nunca alterar**.
- Edições de arquivo: ferramenta Edit ou Python com `encoding='utf-8'` (cp1252 corrompe acentos).
- Depois de qualquer teste: `Stop-Process -Name Bust_A_Groove_2_Recompiled -Force`.

## Onde está cada coisa
- Disco: `disc/Bust A Groove 2 (USA).cue` (+ .bin) na raiz do repo, fora do git (`*.bin`/`*.cue` ignorados);
  `disc/README.md` (no git) tem os hashes do dump. Todos os scripts, o `game.toml` (`../disc/...`) e o
  `Jogar.bat` leem dali. Original também em `C:\Users\sem-m\OneDrive\Documentos\My Games\Bust a groove 2\`.
- Build do zero: `BUILD.md` (raiz). Releases: `releases/README.md`.
- Estrutura (reorganizada em 2026-10-30; tabela no README): `game/` (projeto do jogo, era `tools/recomp/bag2`),
  `recomp/` (build.ps1, env.ps1, patches, aot; era `tools/recomp`), `tools/remake/` (geração dos assets e captura
  de dados: `game_session.py`, `stage_*_keys.py`...), `tools/tests/` (testes e medições no jogo), `tools/extract/`
  (extração do disco), `docs/` (era `notes/`), `archive/` (web, IA descartada, ghidra, roteiro antigo), `logs/`
  (fora do git; venv dos exportadores em `logs/remake/venv`). Scripts de `tools/tests/` acham os de `tools/remake/`
  por `sys.path`; rodar da raiz do repo.
- Framework PSXRecomp (nativo): `C:\tools\psxrecomp` (docs em `docs/`: `WIDESCREEN.md`, `HD_TEXTURE_PACKS.md`,
  `MOD_PACKAGES.md`, `QUALITY_PRESETS.md`). Fork web (não usado mais): `C:\tools\psxrecomp-web`.
- Projeto do jogo: `game/` — `CMakeLists.txt`, `game.toml` (config do runtime: `[video]`, `[widescreen]`),
  `settings.toml` (tela cheia), `Jogar.bat`, `src/bag2_mods.c` (plugins estáticos: widescreen),
  `mods/preloaded/packages/*/manifest.toml` (pacotes de mod ligados por padrão), `generated/` (C gerado + overlays AOT),
  `build/` (exe `Bust_A_Groove_2_Recompiled.exe`, `mods/bundled`, `mods/texture-packs/SLUS-01159/{dumps,replacements}`).
- Build: `powershell -File recomp\build.ps1 -Step runtime` (só runtime/exe; `-Step all` regenera tudo).
  Ambiente: `recomp/env.ps1` (GCC 16 WinLibs, CMake/Ninja). `game.toml`/`settings.toml` são copiados para
  `build/` no pós-build.
- Rodar: `game\Jogar.bat` (acha o exe em `build\`) ou
  `Bust_A_Groove_2_Recompiled.exe --game game.toml --disc "<cue>"` dentro de `build/`. Log em stdout.
- Releases: `releases/vMAJOR.MINOR.PATCH-apelido/` com `VERSION.txt`; tabela e regras em `releases/README.md`.
  Atual: `v2.0.0-remake` (auto-suficiente, ~4 GB, sem `.bat`: dois cliques no exe). Para publicar uma versão nova:
  copiar de `build/` o exe, `assets/`, `bios/`, `mods/bundled/`, `mods/remake/`,
  `mods/texture-packs/SLUS-01159/replacements/`, `settings.toml`, `keybinds.ini`, `input.ini`; o disco em `disc/`;
  o `game.toml` com `disc = "disc/Bust A Groove 2 (USA).cue"` (relativo à pasta do exe); escrever `VERSION.txt`,
  registrar em `releases/README.md` e no `README.md`. O exe fixa a pasta de trabalho na dele (`src/bag2_mods.c`).
- Notas técnicas: `docs/recomp.md` (§8 overlays AOT, §10 1080p, §11 widescreen/nitidez, §12 texturas HD, §13 ritmo da música), `docs/formats.md`,
  `docs/model-format.md`, `docs/media-formats.md`, `docs/remake-plano.md` (plano e estado do remake visual),
  `docs/remake-visual.md` (técnica do remake do HEAT,
  `tools/remake/`, saída em `logs/remake/`). Logs e capturas: `logs/`.

## Estado (2026-10-29, fase 6b do remake completa: pendências da fase 6 dos palcos)
- Jogo completo a 60 fps, 56 overlays compilados antecipadamente, 9x interno + PGXP + xBR, widescreen 16:9 nativo
  (menus/FMV em 4:3), tela cheia sem borda. Teclado em `keybinds.ini`; gamepad SDL automático.
- **Ritmo da música** (`docs/recomp.md` §13): o framework emula o vblank NTSC a 60,000 Hz e o som derivava do
  tick do jogo (+3,3 quadros a cada 20 s); `src/bag2_mods.c` põe o período do hardware (566121 ciclos, 59,826 Hz)
  no construtor (comando `vblank_ntsc` para A/B). `[audio] buffer_ms = 90` no `game.toml` (padrão 180: a batida
  chegava ~200 ms atrasada; 60 estala na abertura). Medir: `tools/tests/audio_sync.py` (deriva e enchimento do
  buffer), `tools/tests/audio_buffer_check.py` (underruns por fase).
- Pacote de texturas HD (v1.3.0): mod `psx.enhancement.hd-textures` ligado por override do manifest em
  `mods/preloaded/packages/psx.enhancement.hd-textures/`; 710 dumps (467 identidades) na v1.3.0; fase 6: dumps dos 11 palcos da tela de palco do 2 jogadores (`stage_inventory.py run`, 562 novos), 92 texturas novas; fase 6b: segunda volta com a música quase inteira (`stage_dump2.py`, 104 novos), 59 texturas novas, **620 substituições** ampliados 4x com Real-ESRGAN
  (`tools/hd_textures.py --filter esrgan`, binário em `C:/tools/realesrgan/bin`, alfa via xBR, recortes aninhados
  deduplicados) em `build/mods/texture-packs/SLUS-01159/replacements/` (fora do git). Para capturar mais palcos:
  `build/mods/state.toml` com `dump = "true"`, `replacements = "false"` (strings), jogar, fechar a janela (não
  `Stop-Process`, senão perde o último lote), rodar o script de novo (incremental). Notas §12 e §12.1.
- **Framework patcheado localmente**, três patches em `recomp/patches/`, nesta ordem:
  `psxrecomp-hd-cache-caps.patch` (caches do mod HD ampliados; sem isso a partida trava, 13–35 fps),
  `psxrecomp-texture-bank-render-thread.patch` (bancos de textura na thread de render, para o fallback por pacotes)
  e `psxrecomp-host-draw.patch` (desenho de host do remake; sem ele o plugin não compila; desde 2026-10-27 sem
  `glGetIntegerv` no save/restore, que custava 1,5–1,9 ms por desenho com o driver da NVIDIA em modo de thread;
  plugins usam só unidades de textura 8+ e não leem estado do GL por quadro; desde 2026-10-28 passa ao plugin, no
  espelho da tela larga, as colunas do centro que a apresentação cobre: `PSXModHostDrawTarget.center_x`). Reaplicar
  (`git apply --ignore-whitespace`) se `C:/tools/psxrecomp` for atualizado. Medição de desempenho: `build-debug/`
  (`-DPSX_DEBUG_TOOLS=ON`, TCP 4370) + `tools/tests/game_perf.py`.
- `recomp/build.ps1` aborta no PowerShell 5.1 quando o cmake escreve aviso em stderr (`$ErrorActionPreference =
  'Stop'`); alternativa: rodar `cmake -S game -B game/build ...` e `cmake --build ... --target
  psx-runtime` direto (flags na linha 50 do script) com o PATH de `env.ps1`.
- **Remake do HEAT no jogo** (fases 0, 1 e 2 de `docs/remake-plano.md`; a IA 3D da fase 2 foi testada offline e
  descartada, venv e modelo em `logs/remake/ai/`;
  técnica em `docs/remake-visual.md`): plugin `src/bag2_heat.c` (hook de filtro em `FUN_80029D4C`, listado em
  `[recompiler] mod_function_entry_funcs`) entrega a pose num marcador na OT e o renderizador GL desenha com o
  núcleo portátil `src/remake/` (C99, GL 3.3/GLES 3.0: Phong Tessellation por instancing, luz por pixel com as
  luzes do palco lidas do GTE, mapas de normal gerados na ativação, sombra própria por shadow map, texturas HD com
  mipmaps; o mesmo código vai para o Polygonia). Mãos com dedos por gesto (`tools/remake/hands.py`; o jogo troca a
  mão religando o irmão do antebraço, nó 15/18 +0x98), orelhas e nariz (`tools/remake/ears.py`). Expressão do
  rosto: o jogo copia olhos/boca de um atlas da página do rosto com `MoveImage` na VRAM; hooks em
  MoveImage/LoadImage (também em `mod_function_entry_funcs`; mudar a lista = regenerar com `psxrecomp-game`)
  mandam as casas no quadro e o shader remapeia o UV (`tools/remake/faces.py`). Pacote
  `mods/preloaded/packages/bag2.remake.heat`, asset v6 `mods/remake/heat.bin` (50 MB) gerado por
  `tools/remake/export_char.py HEAT` (ou `export_heat.py`; fora do git). Exige o patch local `psxrecomp-host-draw.patch`. Caminho por
  pacotes da fase 0 = fallback (`heat_mode` flags bit 2). Geometria nova: enrolamento do jogo (normal para dentro).
  Medido (60 s por janela, fase 2): HEAT original jogo 46–53 %, render 16–50 %; remake 59,9 fps em todos os
  segundos, jogo 40–43 %, render 21–44 %; 4 HEATs (modo 6) 59,9, jogo 40 %, render 35 %; 0 underruns. Medir:
  `tools/tests/game_perf.py` (no máximo 2 variantes de 60 s por sessão). Testes na build-debug:
  `tools/remake/game_session.py`, `game_sidebyside.py` (modo 3, 7, ou 4 com flags da cópia para A/B),
  `game_hands.py` (gestos forçados), `game_face.py` (expressão, original x remake no mesmo quadro), comandos
  `heat_mode`, `heat_light`, `heat_force`, `heat_stats`, `heat_face`, `heat_floor`.
- **Remake do elenco** (fases 4 e 5; `docs/remake-visual.md` "Fase 4 no jogo" e "Fase 5 no jogo"): só o HEAT é
  desenhado por `FUN_80029D4C`; os outros, por funções do overlay de cada um que chamam `FUN_80029588` por osso (gancho
  de filtro nessa entrada, em `mod_function_entry_funcs`). O plugin `src/bag2_heat.c` (nome e comandos `heat_*`
  mantidos) usa um asset por personagem e figurino, `mods/remake/<nome>.bin` e `<nome>_alt.bin` (**v7**, fora do git),
  de `tools/remake/export_char.py NOME[*]` ou `export_all.py`; os 18 personagens nos dois figurinos (36 assets). Na
  ativação só as tabelas; a malha e as texturas são lidas numa thread quando o `LoadImage` traz um TIM do personagem
  na área dos dançarinos (x ≥ 704, y ≥ 256; impressão digital de 64 palavras, amostra não uniforme), GL aos poucos,
  cópia de CPU solta depois. **Fase 5: só quem está em cena fica carregado** (`keep_loaded`, vblank 4×/s): sem desenho
  há 3 s, fora de uma partida em pausa e sem esperar a partida começar, o asset descarrega (GL apagado pela thread de
  render num marcador, também nos menus por um marcador vazio no fim da OT; depois a cópia de CPU) e volta sozinho na
  próxima partida. Cada nó do jogo é reconhecido pelo mesh (não pela posição): posições do corpo, grupos de mão e
  peças com osso próprio (v7); `ALT_POS` (trocas observadas: COMET, ROBO-Z cabeça, SUSHI BOY mão) e `OPTIONAL`
  (CHICHI&SALLY nó 4) no exportador; `tools/remake/hand_table.py` lê as trocas de mão do overlay. Assinatura da raiz:
  nó 0 + 2º nó com mesh. Figurino: dois assets gêmeos (mesmo modelo) decididos pela CLUT ou TIM que pertence a um só.
  Páginas na VRAM de cada jogador pelos pacotes do próprio personagem; expressão do rosto em coordenadas da VRAM;
  semitransparência. Conferido no 2 jogadores, os 18 em partidas espelhadas (`chars_check.py`): 0 quadros caindo no
  original. Testes: `chars_check.py` (sessão com várias partidas: árvore, `match_fail`, capturas original × remake do
  corpo e do rosto), `mem_check.py` (memória com e sem descarga), `savestate_check.py`, `char_tree.py`, `game_chars.py`;
  comandos `heat_chars` (carregados agora), `heat_mem` (histórico), `heat_hits`, `heat_evict 0|1`; flags do
  `heat_mode`: 1024 semi opaco, 2048 só o HEAT.
- **Remake do palco** (fase 3; `docs/remake-visual.md` "Fase 3 no jogo", formato em `docs/formats.md`
  "Palco em partida"): plugin `src/bag2_stage.c` (pacote `bag2.remake.stage`, gancho na entrada de `FUN_8002BEB4`)
  liga um pedido de desenho de host no slot 96 da OT (depois da cena, antes da OT do HUD pendurada no slot 95) e o
  núcleo `src/remake/rmk_post.c` faz bloom multiescala com adaptação + graduação de cor; na tela larga monta a cena
  com o centro canônico e as laterais do espelho e escreve cada parte de volta (`rmk_post_apply_wide`). Pulso no tempo da música: tick em `0x8005FBB4` (256 por batida), atraso do som 4
  quadros (±3 entre músicas). Sombra macia do HEAT no chão (`rmk_gl_draw_floor`, PCSS) no lugar da do jogo
  (`FUN_8002A46C` pulada para o HEAT; chão pela câmera `0x8005E4D0`, luz pelo ponteiro em `0x80060CB8`). Medido
  com o pacote HD (60 s por janela), depois do patch do desenho de host: 59,9 fps em todos os segundos, 0 underruns,
  render +1/0 e jogo +3/−2 pontos contra a base (base: render 25–27 %); modo 6 + palco 59,9, render 29 %. GPU por
  quadro do jogo (carimbos `GL_TIMESTAMP`, `src/gpu_timer.h`, comando `gpu_timers 1`): pós 0,9 ms, chão 0,2 ms,
  corpo 0,2–0,3 ms. Tempo de parede de cada desenho de host no `heat_stats` (`hd_us`). Comandos `stage_post` (flag 8 = metade direita,
  A/B no mesmo quadro; 32 = caminho antigo da tela larga), `stage_stats`, `heat_floor`, `heat_stats`; scripts
  `tools/tests/stage_ab.py`, `stage_pulse_check.py`, `stage_check.py` (regressão), `stage_gpu.py` (GPU por peça),
  `stage_dumps.py` (dumps HD de outros palcos), `game_tcp.py` (jogo aberto na partida para afinar ao vivo),
  `game_perf.py` (variante `heat_mode;stage_post`). O oponente é sorteado; o Stage 1 muda com o personagem.
- **Geometria dos palcos** (fase 6; `docs/remake-visual.md` "Fase 6 no jogo", formato em `docs/formats.md` "Palcos:
  conteúdo e desenho"): o modelo de cada palco (containers 231..245) tem o formato dos personagens; asset HTR7 + tabela
  `.stg` por palco (`mods/remake/palco<c>.bin/.stg`, fora do git) de `tools/remake/export_stage.py` (Real-ESRGAN, vincos,
  oclusão ambiente, alfa suave; texturas do código do overlay por `stage_keys.py` -> `stage_extra_keys.json`). O plugin
  `src/bag2_stage.c` troca na entrada de `DrawOTagEnv` (gancho novo) as sequências de pacotes do palco na OT por desenho
  de host (núcleo `src/remake/rmk_stage.c`): ordem de pintor exata do jogo, pacotes vivos (cor, UV, CLUT), matriz de cada
  nó do GTE na entrada de `FUN_80029588`, faces livres do código do overlay (chão, vigas) reconhecidas pelos vértices de
  `FUN_8001F278`/`RotTransPers*` (ganchos novos), luz por pixel sobre a cor do jogo, mapa de normal, especular, AO,
  sombra dos dançarinos, reflexo no chão (233, 235); textura animada fica com o jogo. Carga quando o jogo desenha o palco,
  descarga 3 s depois. Comando `stage_geo` (flags 128 metade direita, 256 cor por nó; `s` estatísticas, `k` texturas sem
  camada). A/B no mesmo quadro: `stage_geo_pause.py` (savestate); medir: `stage_perf.py`; montagem `stage_montage.py`.
  Mudar a lista `mod_function_entry_funcs` = regenerar com `psxrecomp-game` (agora 11 funções).
- **Fase 6b** (`docs/remake-visual.md`, "Fase 6b no jogo"): no 16:9 o espelho da tela larga desenha só as laterais
  (`RmkTarget.skip_x`; flag 2048 do `stage_geo` desenha o centro, para A/B); o RTPT/RTPS em linha dos overlays de palco
  entra por gancho de instrução: 20 endereços em `[recompiler] mod_instruction_sites` do `game.toml`. Mudar essa lista =
  regenerar os overlays estáticos (cwd `C:\tools\psxrecomp\tools`, `env.ps1` no PATH): `python aot_overlay_pipeline.py
  static --profile <repo>/recomp/aot/overlays.json --game-toml <repo>/game/game.toml --recompiler
  C:/tools/psxrecomp/recompiler/build/psxrecomp-game.exe --work-dir <repo>/recomp/aot/work-static --gcc
  C:/tools/winlibs/mingw64/bin/gcc.exe --out-dir <repo>/game/generated --workers 6` (só os 13 overlays
  de palco mudam; fazer cópia de `generated/` antes) e recompilar. Tempo de GPU do palco e dos dançarinos: `gpu_timers 1` +
  `tools/tests/stage_gpu_ab.py` (o `render_busy` varia ±5 pontos entre janelas da mesma variante). Segunda rodada
  (2026-10-29): Phong nas peças curvas, decidido por nó quando ele entra em cena (`classify_tess`; flag 16 do
  `stage_geo` desliga); 241 e 245 pelo índice do palco (byte `0x8005FB56`, `BAG2_STAGE_IDX` no `game_session.py`,
  `stage_hidden.py`; o 245 não tem partida: só o palco por ~55 s); texturas das faces livres fora dos TIMs lidas da VRAM
  (`stage_vram_keys.py` grava em `logs/remake/palcos/vram/`, `export_stage.py` usa); textura animada por retângulo da
  página (`stage_geo d` lista as marcas) e hash da CLUT na tabela `.stg`; janela de textura (E2h) no shader; `stage_geo
  p` diz de onde vêm as faces livres sem par (página/camada, endereço na RAM, nós da OT). Re-exportar
  um palco: `python tools/remake/export_stage.py <container>` (escreve nas duas pastas `mods/remake/`).
  Terceira rodada (2026-10-29): a tela dos vértices livres é a do GTE exata (`gte_div`, divisão UNR com a tabela do
  hardware) e os cantos saturados (tela em ±1024, divisão saturada perto da câmera) são resolvidos pelo vizinho no espaço
  (`fv_find_sat`, `fv_find_all_sat`); paletas animadas por MoveImage com uma camada por linha de origem: `python
  tools/remake/stage_clut_vars.py [posições]` grava `vram/<c>/clut_var.json` (dados do jogo), o `export_stage.py` monta
  as camadas (limite de 64 por palco, todas as variantes de uma CLUT ou nenhuma) e a tabela `.stg` v2; o plugin troca a
  camada a cada MoveImage (`stage_geo v` lista as cópias) e, depois de carregar savestate, deixa essas chaves com o jogo
  até a próxima cópia. O que sobra sem par é 2D (painéis do 233, billboards de brilho, sprites). Savestate salvo no
  meio do quadro: a OT restaurada já liga os marcadores de host postos antes de salvar, e o cursor das arenas é do
  host; por isso `bag2_heat.c` (`marks_blocked`) e `bag2_stage.c` (`emit_post`) não ligam marcador novo no quadro da
  carga (antes, marcador apontando para si mesmo: 10–18 s de travamento). Qualquer plugin novo que ligue marcador no
  meio do quadro precisa da mesma regra.
- **Fase 6c** (`docs/remake-visual.md`, "Fase 6c no jogo", 2026-10-30): partida pelo teclado na build normal conferida
  pelo computer-use (2 jogadores num teclado: `[controller] p2_device = "keyboard"` no `game.toml` da build + teclas
  próprias em `[player2]` do `keybinds.ini` da build, senão cada toque conta duas vezes). 241: as faces sem par fora de
  `0x8008xxxx` eram o nó 5 do modelo recusado por tamanho de buffer (`packet_words` não contava o `DR_TPAGE` de 2
  palavras das listas `PREFIX` 18..23/30/31; `RmkStagePrim.prefix`, `stage_geo s` `prefixo=`); 241/236/238
  re-exportados. Paletas animadas: variantes **afins** (`rgb' = k·rgb + b` sobre uma camada âncora; `export_stage.py`
  `clut_variants`/`affine_fit`, `AFFINE_TOL` 1,5/31; `.stg` v3 de 20 bytes por variante; núcleo: camada `n_layers +
  índice` no pacote, tabela 2 × n_var RGBA32F na unidade 11, bits 9+ do modo): 242 58 camadas, 234 63, 0 texturas
  animadas com o jogo. Telão do 235: os painéis leem o buffer de tela (página de 16 bpp, x < 512; não há cópia
  VRAM→VRAM) e o renderizador amostra a VRAM nativa sem o desenho de host; agora `geo_key_layer` dá a camada
  `RMK_STAGE_LAYER_FB` (0xE0..0xEF: buffer e coluna) e o núcleo copia a faixa 512 × 256 do fbo (`fb_capture`, unidade
  12) na primeira sequência do quadro que a lê e desenha emissivo; flag 4096 do `stage_geo` = telão com o jogo (A/B).
  `stage_geo v` lista todas as cópias VRAM→VRAM (`vt` só origem na tela, `v<n>` pagina). Pacote de face do modelo sem
  camada (página recarregada, CLUT marcada) agora fica com o jogo (`read_packet` −3, `textura_com_o_jogo=`; flag 8192 =
  como antes): o vidro do helicóptero do 241 é o reflexo que o jogo rola na página. O exportador precisa do venv:
  `logs/remake/venv/Scripts/python.exe tools/remake/export_stage.py <c>`.
- **Build-debug e o pacote HD**: `build-debug/mods/texture-packs/SLUS-01159/replacements` é uma junção para a pasta
  de `build/`; conferir "HD textures:" no log antes de medir (ficou quebrada de 2026-10-24 a 2026-10-27). Com o HD
  o renderizador desliga os bancos de textura (`psx_mod_texture_banks_supported()` = 0). O cartão de memória guarda
  o último personagem e o cursor começa nele: `BAG2_SELECT` (U/D/L/R) em `game_session.py`. O driver da NVIDIA
  alterna de modo entre sessões: sessão com a render em 49–62 % até na base é inválida, repetir.
- As pastas `build/` e `build-debug/` foram reconfiguradas em 2026-10-26 (o cache do CMake apontava para o nome
  antigo da pasta); se o CMake reclamar de "does not match the source", apagar `CMakeCache.txt`, `CMakeFiles/` e
  `_deps/*-build`, `_deps/*-subbuild` e configurar de novo.
- Sem servidor TCP de debug na build (`PSX_DEBUG_TOOLS=OFF`): testes pelo computer-use. Toque curto de tecla não
  chega ao pad; usar `hold_key` ~0,25 s. O menu principal volta ao attract em poucos segundos.

## Próximo passo
- **Remake visual (prioridade)**: plano completo, fases e estado em `docs/remake-plano.md`. Fases 0–6, 6b e 6c feitas
  (HEAT, palco, os dois jogadores, o elenco, a memória, os palcos pelo caminho novo e os três itens que faltavam). Próxima: **fase 7, o port para o
  Polygonia** (personagens e palcos juntos; ganchos e memória dos palcos em "Fase 7" do plano); depois 8 (abertura).
- Fase 6b fechou o render do 236 (+2 na ordem que dava +11), o RTPT em linha (chão do 233, asfalto do 236), a segunda
  volta de dumps, o Phong nas peças curvas, 241 e 245 no jogo, as texturas da VRAM, a janela de textura e a causa do
  HEAT × COMET (driver, não pixel). 13 palcos a 59,9 fps em todos os segundos, 0 underruns, jogo −2 a −5, render −17 a
  +2. Aberto (`docs/remake-visual.md`, "Limites e o que não deu (fase 6b)"): partida na build normal pelo teclado
  (computer-use negado nas duas primeiras rodadas; na terceira o usuário estava remoto); cobertura de 80–98 % dos pacotes do palco por quadro depois da terceira
  rodada (o resto: faces 2D do overlay em 0x8008xxxx, 76 por OT no 233; as paletas animadas do 242 e do 234, que não
  cabem nas 64 camadas; texturas cuja página muda, como a água do 231; efeitos).
- Upscaler melhor (xBRZ/ESRGAN) mantendo alfa discreto (0/143/255) — ver §12 sobre franjas.

## Testes de palcos e personagens: usar o modo 2 jogadores
Para avaliar um palco ou um personagem específico, jogar no **modo 2 jogadores** ("2player game" no menu
principal): ali se escolhem **os dois personagens e o palco**. Isso dá controle total sobre o teste, sem depender do
sorteio do oponente nem do personagem que o cartão de memória lembra, e deixa medições, A/B, regressão e dumps HD
repetíveis (mesmo palco e mesmos dançarinos nas duas variantes). Preferir esse modo a partidas de 1 jogador nos
testes. Os scripts de `tools/remake/` entram pelo 2 jogadores (`game_session.py` `enter_match`/`to_match_2p`, desde
2026-10-28): ambiente `BAG2_MATCH="P1 P2 palco"` (padrão `HEAT COMET 0`; `palco` = toques para a direita a partir
do palco do 1P; `BAG2_MATCH=1p` volta à rota antiga). O pad 2 fica ligado por `set_input` persistente na porta 2
(um `press` na porta 2 expira e o jogo pausa com "CONTROLLER REMOVED"); um toque de 6 quadros anda uma casa na grade
(15 andam duas). Grade do cartão do usuário (10 liberados): linha 0 HEAT, COMET; linha 1 SHORTY, STRIKE, TSUTOMU,
CAPOEIRA; linha 2 BI-O, HIRO, KITTY-N, KELLY. `s.who()` confere os personagens pela assinatura do nó 0 e do 2º nó com
mesh. Os 8 bloqueados no cartão: `BAG2_UNLOCK=1` escreve o "Unlock All Characters" do CodeBreaker (8 bytes = 1 em
`0x8004F667`) só na RAM antes do menu, e a sessão guarda e devolve os cartões (`game/saves/card*.mcd`; o
jogo grava o cartão depois das partidas); a grade ganha a linha 3 COLUMBO, ROBO-Z GOLD, PANDER, SUSHI BOY e a linha 4
McLOAD, CHICHI&SALLY, MICHAEL DOI, HUSTLE KONG (ícones "?", selecionáveis). Várias partidas numa sessão: `quit_match()`
(pausa, QUIT: menu principal com o cursor em "1player game") e `menu_to_match_2p(p1, p2, palco)`; na pausa os
dançarinos não são desenhados.

## Testes com captura de tela
`PrintWindow` e `CopyFromScreen` voltam preto na janela GL sem borda. Funciona: captura de tela do app
(computer-use screenshot). O jogo lançado em segundo plano fica atrás das outras janelas: clicar nele ou
`SetForegroundWindow`. A abertura (STR) dura ~1 min e não pula com Start; seleção de personagem: `x` confirma.

Antes e depois do README (`docs/img/comparacao/`): `tools/tests/compare_shots.py` salva um savestate na build-debug
com tudo ligado e o carrega numa sessão "original" (renderizador de software, nativo, 4:3, dithering, sem tela larga,
HD e PGXP; os pacotes do remake ficam ligados porque o savestate exige o mesmo layout da memória de mod, e o efeito
sai por `heat_mode 0`, `stage_geo 0`, `stage_post 0`); `tools/tests/compare_montage.py` monta as imagens. A captura
melhorada (`screenshot_wide_hires`) vem 6138 × 2160 (largura do modo 512 esticada): comprimir para 3840 × 2160.
