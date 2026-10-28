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
- Sem deploy/publicação.
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
- Notas técnicas: `notes/recomp.md` (§8 overlays AOT, §10 1080p, §11 widescreen/nitidez, §12 texturas HD, §13 ritmo da música), `notes/formats.md`,
  `notes/model-format.md`, `notes/media-formats.md`, `notes/remake-plano.md` (plano e estado do remake visual),
  `notes/remake-visual.md` (técnica do remake do HEAT,
  `tools/remake/`, saída em `tools/recomp/logs/remake/`). Logs e capturas: `tools/recomp/logs/`.

## Estado (2026-10-28, fim da fase 4 do remake)
- Jogo completo a 60 fps, 56 overlays compilados antecipadamente, 9x interno + PGXP + xBR, widescreen 16:9 nativo
  (menus/FMV em 4:3), tela cheia sem borda. Teclado em `keybinds.ini`; gamepad SDL automático.
- **Ritmo da música** (`notes/recomp.md` §13): o framework emula o vblank NTSC a 60,000 Hz e o som derivava do
  tick do jogo (+3,3 quadros a cada 20 s); `src/bag2_mods.c` põe o período do hardware (566121 ciclos, 59,826 Hz)
  no construtor (comando `vblank_ntsc` para A/B). `[audio] buffer_ms = 90` no `game.toml` (padrão 180: a batida
  chegava ~200 ms atrasada; 60 estala na abertura). Medir: `tools/remake/audio_sync.py` (deriva e enchimento do
  buffer), `tools/remake/audio_buffer_check.py` (underruns por fase).
- Pacote de texturas HD (v1.3.0): mod `psx.enhancement.hd-textures` ligado por override do manifest em
  `mods/preloaded/packages/psx.enhancement.hd-textures/`; 710 dumps (467 identidades) ampliados 4x com Real-ESRGAN
  (`tools/hd_textures.py --filter esrgan`, binário em `C:/tools/realesrgan/bin`, alfa via xBR, recortes aninhados
  deduplicados) em `build/mods/texture-packs/SLUS-01159/replacements/` (fora do git). Para capturar mais palcos:
  `build/mods/state.toml` com `dump = "true"`, `replacements = "false"` (strings), jogar, fechar a janela (não
  `Stop-Process`, senão perde o último lote), rodar o script de novo (incremental). Notas §12 e §12.1.
- **Framework patcheado localmente**, três patches em `tools/recomp/patches/`, nesta ordem:
  `psxrecomp-hd-cache-caps.patch` (caches do mod HD ampliados; sem isso a partida trava, 13–35 fps),
  `psxrecomp-texture-bank-render-thread.patch` (bancos de textura na thread de render, para o fallback por pacotes)
  e `psxrecomp-host-draw.patch` (desenho de host do remake; sem ele o plugin não compila; desde 2026-10-27 sem
  `glGetIntegerv` no save/restore, que custava 1,5–1,9 ms por desenho com o driver da NVIDIA em modo de thread;
  plugins usam só unidades de textura 8+ e não leem estado do GL por quadro). Reaplicar
  (`git apply --ignore-whitespace`) se `C:/tools/psxrecomp` for atualizado. Medição de desempenho: `build-debug/`
  (`-DPSX_DEBUG_TOOLS=ON`, TCP 4370) + `tools/remake/game_perf.py`.
- `tools/recomp/build.ps1` aborta no PowerShell 5.1 quando o cmake escreve aviso em stderr (`$ErrorActionPreference =
  'Stop'`); alternativa: rodar `cmake -S tools/recomp/bag2 -B tools/recomp/bag2/build ...` e `cmake --build ... --target
  psx-runtime` direto (flags na linha 50 do script) com o PATH de `env.ps1`.
- **Remake do HEAT no jogo** (fases 0, 1 e 2 de `notes/remake-plano.md`; a IA 3D da fase 2 foi testada offline e
  descartada, venv e modelo em `tools/recomp/logs/remake/ai/`;
  técnica em `notes/remake-visual.md`): plugin `src/bag2_heat.c` (hook de filtro em `FUN_80029D4C`, listado em
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
  `tools/remake/game_perf.py` (no máximo 2 variantes de 60 s por sessão). Testes na build-debug:
  `tools/remake/game_session.py`, `game_sidebyside.py` (modo 3, 7, ou 4 com flags da cópia para A/B),
  `game_hands.py` (gestos forçados), `game_face.py` (expressão, original x remake no mesmo quadro), comandos
  `heat_mode`, `heat_light`, `heat_force`, `heat_stats`, `heat_face`, `heat_floor`.
- **Remake do elenco** (fase 4; `notes/remake-visual.md` "Fase 4 no jogo"): só o HEAT é desenhado por
  `FUN_80029D4C`; os outros, por funções do overlay de cada um que chamam `FUN_80029588` por osso (gancho de filtro
  novo nessa entrada, em `mod_function_entry_funcs`). O mesmo plugin `src/bag2_heat.c` (nome e comandos `heat_*`
  mantidos) carrega um asset por personagem e figurino, `mods/remake/<nome>.bin` e `<nome>_alt.bin` (v6, fora do
  git), de `tools/remake/export_char.py NOME[*]` ou `export_all.py` (`export_heat.py` virou atalho). Na ativação só
  as tabelas; a malha e as texturas são lidas numa thread quando o `LoadImage` traz um TIM do personagem (impressão
  digital de 64 palavras), GL aos poucos, cópia de CPU solta depois. Personagem reconhecido pela assinatura dos
  ossos (posição na árvore ≠ índice no arquivo em alguns); páginas na VRAM de cada jogador lidas dos pacotes do
  próprio personagem; expressão do rosto em coordenadas da VRAM; semitransparência (passo 3 do núcleo e decalque
  semi no shader). Cada personagem tem dois figurinos (87/123 + 2k e 159/195 + 2k; o 2P da partida espelhada usa o
  alternativo); quando o modelo é o mesmo, decide o TIM que está na página. 15 personagens nos dois figurinos;
  CHICHI&SALLY, COLUMBO e SUSHI BOY recusados (outro esqueleto na lista de nós). Medido HEAT × COMET (60 s, 2
  sessões): 59,9 fps, 0 underruns, jogo −10/−6 e render +6/+6 pontos contra a base (só o HEAT, flag 2048). Testes:
  `chars_tour.py` (pares do 2 jogadores: árvore, capturas original × remake, `heat_chars`), `game_chars.py` (A/B
  no mesmo quadro por personagem), `char_tree.py` (qual nó o jogo liga em cada posição); comando `heat_chars`;
  flags novas do `heat_mode`: 1024 semi opaco, 2048 só o HEAT.
- **Remake do palco** (fase 3; `notes/remake-visual.md` "Fase 3 no jogo", formato em `notes/formats.md`
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
  `tools/remake/stage_ab.py`, `stage_pulse_check.py`, `stage_check.py` (regressão), `stage_gpu.py` (GPU por peça),
  `stage_dumps.py` (dumps HD de outros palcos), `game_tcp.py` (jogo aberto na partida para afinar ao vivo),
  `game_perf.py` (variante `heat_mode;stage_post`). O oponente é sorteado; o Stage 1 muda com o personagem.
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
- **Remake visual (prioridade)**: plano completo, fases e estado em `notes/remake-plano.md`. Fase 2 feita (a IA
  imagem→3D ficou só offline, pior que o remake atual; a expressão do rosto do original agora chega ao remake).
  Fase 3 feita (pós-efeito, pulso, sombra no chão, dumps dos Stage 1; geometria do palco adiada com dados; render
  dentro do orçamento depois do patch do desenho de host). Fase 4 feita (os dois jogadores e o elenco; o que falta
  está em `notes/remake-visual.md`, "Limites da fase 4"). Próxima: fase 5 (Polygonia) ou os buracos da fase 4.
- Cobrir mais palcos no pack (dump incremental em partidas com outros personagens; Stage 1 muda por personagem).
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
CAPOEIRA; linha 2 BI-O, HIRO, KITTY-N, KELLY. `s.who()` confere os personagens pela assinatura do nó 0.

## Testes com captura de tela
`PrintWindow` e `CopyFromScreen` voltam preto na janela GL sem borda. Funciona: captura de tela do app
(computer-use screenshot). O jogo lançado em segundo plano fica atrás das outras janelas: clicar nele ou
`SetForegroundWindow`. A abertura (STR) dura ~1 min e não pula com Start; seleção de personagem: `x` confirma.
