# PSXRecomp para Bust A Groove 2 — pesquisa e estado da toolchain

Data: 2026-10-22. Máquina: Windows 10 Pro 19045, sem workload C++ do VS 2022.

## 1. O que é o PSXRecomp

- Repo: <https://github.com/RetroPortingToolKit/psxrecomp> (antes `mstan/psxrecomp`).
  Clone local: `C:\tools\psxrecomp` (commit `31df2e1e`, 2026-10-22).
- Framework de **recompilação estática** de PS1: traduz MIPS R3000A → C e linka
  com um runtime de hardware compartilhado (GPU OpenGL/Vulkan/software, SPU,
  CD-ROM, MDEC, GTE, pads, memcards). Dois CMake trees: `recompiler/`
  (ferramentas `psxrecomp-game`, `psxrecomp-bios`, `psxrecomp-toml`,
  `psxrecomp-analyze`, CLI `psxrecomp`) e `runtime/` (motor + macro CMake
  `psxrecomp_add_game_runtime` que um projeto de jogo chama).
- **BIOS**: o BIOS recompilado é a base de tudo. O **OpenBIOS** (PCSX-Redux,
  MIT) vem embutido em `bios/openbios.bin` e é redistribuível — não precisa de
  SCPH1001.BIN. `bios_hle = true` só pula a animação de boot.
- **Overlays** (código carregado do disco em runtime, exatamente o caso dos
  blocos `DBI_` do BUST2.BIN): o runtime detecta código novo em RAM, roda num
  interpretador MIPS pequeno, captura em `overlay_captures.json` e compila
  (pipeline `static → gcc → tcc`) para um cache nativo. Para release, dá para
  "shardar" AOT (`docs/AOT_SHARDING.md`, `docs/COMPILING_OVERLAYS.md`) quando
  se conhece o loader e os byte-guards. Nada disso exige configurar os overlays
  no TOML de antemão; só o EXE principal é recompilado estaticamente.
- Renderers: OpenGL (padrão), Vulkan (experimental), software (referência e
  mais portátil — é o que a build web usa). Threads via fibers (Win32 Fibers /
  ucontext).
- Licença: **PolyForm Noncommercial 1.0.0** (© Matthew Stanley). Uso pessoal,
  local e não comercial é permitido; distribuir uma port pública também é
  não comercial, mas o `overlay_captures.json` contém código do jogo e não deve
  ser publicado. Componentes de terceiros mantêm licenças próprias
  (`THIRD_PARTY_ATTRIBUTION.md`, `licenses/`). O projeto não distribui BIOS,
  discos ou assets.

## 2. Formato de configuração (`game.toml`)

Esquema completo: `C:\tools\psxrecomp\docs\config_schema.md`. Um processo sempre
carrega um perfil de BIOS (`bios/OpenBIOS.toml`) e, opcionalmente, um
`game.toml`. Caminhos relativos resolvem a partir da raiz do projeto (o loader
sobe até achar `CMakeLists.txt`, `.gitignore` ou `.git`).

```toml
[game]
name = "..." ; id = "SLUS-01159"
exe = "caminho/SLUS_011.59"          # PS-X EXE extraído
disc = "caminho/jogo.cue"            # .cue (o .bin vem do FILE do cue)
load_address = "0x80010000"
entry_pc     = "0x80038270"
text_size    = "0x0004E800"
stack_base   = "0x801FFFF0"

[recompiler]
seeds = "seeds/functions.txt"        # lista de endereços de função (um por linha)
out_dir = "generated" ; strict = true
discovery = "whole-image"            # ou "reachable"
[[recompiler.patch]]                 # patches de 1 palavra MIPS, opcionais

[runtime]   # window_title, memcard_dir, disc_speed, bios_hle, overlay_cache, overlay_backend
[video]     # renderer = "opengl"|"software", supersampling, texture_filtering, aspect_ratio
[controller], [widescreen], [audit], [netplay], [prepare_disc]
```

Saída: `generated/<stem>_dispatch.c` + `generated/<stem>_full_NN.c` (shards de
~40k linhas) + `<stem>_decls.h`. O `<stem>` é o nome do EXE sem `.EXE/.BIN`
(aqui `SLUS_011.59`). `psxrecomp-toml <EXE> --output game.toml --seeds seeds.txt`
gera o esqueleto a partir do cabeçalho e os seeds por varredura de `jal`.

Invocação: `psxrecomp-game --config game.toml` (ou a forma antiga
`psxrecomp-game <EXE> --seeds f --out-dir d --strict`).

## 3. Build (como o upstream manda)

Requisitos: Git, Python 3, CMake ≥ 3.20, Ninja, compilador C++20 (recompiler)
e C99/C++17 (runtime). SDL3 3.4.10 é baixado automaticamente. Windows: MSVC,
MinGW (MSYS2 — é o que o CI do projeto usa, "release parity") ou o pacote
`cmake-clang-v1-windows-x64.zip` de
<https://github.com/RetroPortingToolKit/RetroPorting-Toolchains/releases>.

```sh
cd C:/tools/psxrecomp
cmake -S recompiler -B recompiler/build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build recompiler/build
bash tools/regen_bios.sh --config bios/OpenBIOS.toml      # gera generated/OpenBIOS_*.c (obrigatório)
cmake -S runtime -B runtime/build -G Ninja -DCMAKE_BUILD_TYPE=Release -DPSX_RECOMP_UI=OFF
cmake --build runtime/build --target psx-runtime           # runtime só-BIOS (teste)
```

Projeto de jogo (nosso: `tools/recomp/bag2/`):

```sh
C:/tools/psxrecomp/recompiler/build/psxrecomp-game --config tools/recomp/bag2/game.toml
cmake -S tools/recomp/bag2 -B tools/recomp/bag2/build -G Ninja -DCMAKE_BUILD_TYPE=Release -DPSXRECOMP_ROOT=C:/tools/psxrecomp
cmake --build tools/recomp/bag2/build --target psx-runtime
tools/recomp/bag2/build/psx-runtime.exe --game game.toml --disc "<cue>"
```

Nunca buildar em Debug sem querer: o C gerado em `-O0` demora demais.

## 4. Emscripten / WASM — como o RepsiMan fez

- O **upstream não tem suporte a Emscripten** (zero referências em CMake/docs).
  O suporte web está no fork **kem0x/psxrecomp** (`master` e branch
  `compat/windows-web-stability`): `runtime/runtime.cmake` do fork tem blocos
  `if(EMSCRIPTEN)` que usam a port SDL2 do Emscripten e linkam com
  `-pthread -sPROXY_TO_PTHREAD=1 -sPTHREAD_POOL_SIZE=2 -sALLOW_MEMORY_GROWTH=1
  -sINITIAL_MEMORY=402653184 -sMAXIMUM_MEMORY=1073741824 -sSTACK_SIZE=8388608
  -sDEFAULT_PTHREAD_STACK_SIZE=16777216 -sFORCE_FILESYSTEM=1 -sEXIT_RUNTIME=0`,
  além de hooks "web runtime enhancement", menu de pausa e pause seguro para WASM.
- **RepsiMan** (<https://github.com/FermataRest/RepsiMan>, "Pepsiman Recompiled")
  pina o submódulo `psxrecomp` em `kem0x/psxrecomp@0c2b7081e3ce` (API antiga:
  `psxrecomp_add_runtime_target(... BIOS_INTERPRETER GAME_GENERATED_FULL_C
  GAME_GENERATED_DISPATCH_C DEFAULT_BIOS_PATH openbios-fastboot.bin)`), usa
  `renderer = "software"`, e no `CMakeLists.txt`:
  - `set_target_properties(psx-runtime PROPERTIES SUFFIX ".html")`
  - `-sINVOKE_RUN=0 -sEXPORTED_RUNTIME_METHODS=FS,callMain --shell-file web/shell.html`
  - `-msimd128` em `mdec.c`, `gpu_sw_renderer.c`, `main.cpp`
  - `DEFAULT_GAME_CONFIG_PATH "/game.toml"` (caminho dentro do FS virtual)
  - opcional `--preload-file PEPSIMAN@/game/disc` só para build privada de teste.
  O `web/shell.html` roda o runtime num worker, apresenta o framebuffer do
  renderer software num canvas, manda áudio da SPU pro Web Audio, pede ao
  usuário o CUE/BIN via file picker (File System Access API + IndexedDB para
  lembrar handles e memcards). `web/serve.py` serve com COOP/COEP
  (SharedArrayBuffer/threads). Passos:

```sh
git clone --recurse-submodules https://github.com/FermataRest/RepsiMan.git
# ativar emsdk (C:\tools\emsdk\emsdk_env.bat / emsdk activate latest)
cmake -S psxrecomp/recompiler -B psxrecomp/recompiler/build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build psxrecomp/recompiler/build
psxrecomp/recompiler/build/psxrecomp-game --config game.toml
emcmake cmake -S . -B build-web -G Ninja -DCMAKE_BUILD_TYPE=Release -DPSX_LAUNCHER=OFF -DPSX_DEBUG_TOOLS=OFF
cmake --build build-web -j4
python3 web/serve.py   # http://127.0.0.1:8080/Pepsiman_Recompiled.html
```

- Para o BAG2 web, o caminho mais curto é **usar o fork kem0x** (ou portar os
  blocos `if(EMSCRIPTEN)` dele para o upstream, que já avançou bastante) e
  copiar `web/shell.html` + `web/serve.py` do RepsiMan como base do shell.

## 5. Estado nesta máquina (2026-10-22)

Toolchain instalada (nada exigiu admin, exceto o que ficou pendente):

| Ferramenta | Versão | Onde | Como |
|---|---|---|---|
| GCC MinGW-w64 UCRT (WinLibs) | 16.2.0 (posix/seh, r2) | `C:\tools\winlibs\mingw64\bin` | zip baixado e verificado pelo winget (`BrechtSanders.WinLibs.POSIX.UCRT`, SHA256 `D5DBAFC4A170…276A`), extraído à mão porque o winget ficou preso atrás de um prompt UAC |
| CMake | 4.4.4 (Kitware) | `%APPDATA%\Python\Python314\Scripts\cmake.exe` | `pip install --user cmake ninja` |
| Ninja | 1.13.2 | idem | idem |
| Emscripten (emsdk) | 6.0.13 | `C:\tools\emsdk` (`emsdk_env.bat`) | `git clone` + `emsdk install/activate latest` |
| PSXRecomp | commit `31df2e1e` (2026-10-22) | `C:\tools\psxrecomp` | `git clone --recursive` |
| Python | 3.14 | já havia | — |

Pendente/observações:
- `winget install Kitware.CMake` (MSI) ficou **esperando o UAC** (`consent.exe`); o
  cmake do pip resolve. O MSYS2 também foi baixado pelo winget
  (`%LOCALAPPDATA%\Temp\WinGet\MSYS2.MSYS2.20260611\`) mas não foi instalado — não
  é necessário.
- O WinLibs traz um `cmake.exe` 4.4.3 próprio (GnuTLS, sem CA bundle) que falha
  em todo download do FetchContent ("SSL peer certificate ... not OK"). Por isso
  `tools/recomp/env.ps1` põe o cmake do pip ANTES no PATH.
- O clone foi feito com `core.autocrlf=true` (CRLF): os testes Python que
  inspecionam o código-fonte do framework quebram por isso (12/154 falhas no
  `ctest` do recompiler — `gl_internal_scale_guards`, `overlay_*`,
  `interp_refused_game_text` etc., todos "substring not found"). Os emissores em si
  funcionam. Para zerar: reclonar com `git -c core.autocrlf=false clone ...`.
- Não há SCPH1001.BIN: o runtime linka só o backend OpenBIOS (avisa
  "BIOS backends skipped: SCPH1001"), o que é suportado.

Arquivos deste repo:
- `tools/recomp/env.ps1` — PATH/CC/CXX da toolchain.
- `tools/recomp/build.ps1` — passos 1-4 reproduzíveis (`-Step framework|bios|game|runtime|all`, `-Run`).
- `tools/recomp/bag2/game.toml` — config do jogo (ver §2); `bios_config` aponta
  para `C:/tools/psxrecomp/bios/OpenBIOS.toml` (sem isso o `psxrecomp-game`
  aborta com "no BIOS profile found").
- `tools/recomp/bag2/CMakeLists.txt` — projeto de jogo (`psxrecomp_add_game_runtime`, sem recomp-ui/netplay).
- `tools/recomp/bag2/seeds/functions.txt` — 632 seeds do `psxrecomp-toml` (631 alvos de JAL + entry). Os endereços conhecidos 0x80035DC4 (BPE) e 0x8001F7E0 (instalador de overlay) já estão lá.
- `tools/recomp/logs/*.log` — saída de cada passo.

Resultados:
- `recompiler/build`: **OK** (psxrecomp-game/-bios/-toml/-analyze/psxrecomp.exe; só um aviso do ld ".rsrc merge failure: multiple non-default manifests").
- `regen_bios.sh --config bios/OpenBIOS.toml`: **OK** — 651 funções emitidas, 4 puladas (opcodes 0x2F/funct 0x3C), 3990 entradas de dispatch.
- `psxrecomp-game --config game.toml` (SLUS_011.59): **OK em 0,5 s** — 1042 funções
  descobertas (+24 entradas por pré-passo = 1066), 29519 blocos básicos, 893
  loops, 22 shards (`SLUS_011.59_full_00..21.c`, 785 411 linhas, ~27 MB) +
  `SLUS_011.59_dispatch.c` (1065 entradas) + `SLUS_011.59_decls.h`. 14 avisos
  "out-of-function branch ... emitting fallthrough" (normal em código de
  compilador antigo; candidatos a seeds extras se algo quebrar).
- Configure do runtime do jogo (`tools/recomp/bag2/build`): **OK** (SDL3 3.4.10
  baixado e compilado junto, backend OpenBIOS + C do jogo linkados).
- Build do runtime do jogo: **OK na 2ª tentativa**. Na 1ª, o GCC 16 compilou o
  runtime como gnu++20 (o template de projeto não fixa o padrão) e
  `gpu_hd_textures.cpp` / `duckstation_texture_pack.cpp` quebraram em
  `path::u8string()` (vira `std::u8string`). Corrigido fixando
  `CMAKE_CXX_STANDARD 17` em `tools/recomp/bag2/CMakeLists.txt`. Saída:
  `tools/recomp/bag2/build/Bust_A_Groove_2_Recompiled.exe` (38 MB, nome vem do
  WINDOW_TITLE), com `bios/openbios.bin`, `game.toml`, `mods/`, `assets/` ao lado.
- **Boot: FUNCIONA.** `Bust_A_Groove_2_Recompiled.exe --game game.toml --disc "<cue>"`
  cria contexto OpenGL 3.3 (RTX 3070 Ti), executa o OpenBIOS LLE a partir de
  0xBFC00000, carrega o SLUS_011.59 do disco, e roda estável a 60 fps
  (`emu_busy` 12–28 %). Capturas em `tools/recomp/logs/bag2-boot-*-window.png`:
  12 s = tela preta (boot do BIOS), 24 s = logo "Frame Graphics", 40 s = abertura
  do jogo (dançarino com partículas). Logs: `logs/bag2-boot.log`
  (`--hidden-window` + `PSX_FRAME_REPORT_MS=1000`) e `logs/bag2-boot-visible.log`.
  Em 40 s ainda não apareceu `overlay_captures.json` (os DBI_ só entram ao sair
  da abertura/menus); ao chegar lá o runtime vai capturar e, sem `gcc` no PATH,
  interpretar ("tcc tier active but no bundled toolchain ... overlay gaps ->
  interpreter"). Rodar com `env.ps1` carregado põe o gcc do WinLibs no PATH e
  habilita a compilação dos overlays (tier gcc).

## 6. Próximos passos

1. Jogar até um modo de dança (overlay DBI_) com `env.ps1` carregado e checar
   `overlay_captures.json` + `docs/COMPILING_OVERLAYS.md` para compilar os
   overlays AOT (`--static`) em vez de interpretar.
2. Se algum trecho ficar no interpretador/`psx_unknown_dispatch`, adicionar os
   endereços ao `seeds/functions.txt` (os 14 avisos "out-of-function branch" do
   log do psxrecomp-game são os primeiros candidatos) e regenerar.
3. Alvo WASM: (a) clonar `kem0x/psxrecomp` em `C:\tools\psxrecomp-web`, (b)
   `emsdk_env.bat`, (c) regenerar o C do jogo com o `psxrecomp-game` desse fork
   (API `psxrecomp_add_runtime_target` antiga; conferir `psxrecomp-toml-readme.md`
   do fork), (d) copiar `CMakeLists.txt`, `web/shell.html`, `web/serve.py` do
   RepsiMan trocando `SLPS_017.62` → `SLUS_011.59` e `renderer = "software"`,
   (e) `emcmake cmake -S . -B build-web -G Ninja -DCMAKE_BUILD_TYPE=Release
   -DPSX_LAUNCHER=OFF -DPSX_DEBUG_TOOLS=OFF && cmake --build build-web`.
   Overlays em WASM: não há gcc/tcc no browser, então ou os DBI_ são compilados
   AOT (shards estáticos) antes, ou ficam no interpretador.

## 7. Build web (WASM) — feito em 2026-10-23

Resultado: `web/recomp.html` roda o jogo recompilado no navegador (`/recomp.html?dev` ou via seletor de
`.bin`/`.cue`). Boot OpenBIOS → logos → abertura → menu principal a 60 vblank/s (abertura ~36–39 por
causa do MDEC no renderer software). Teclado: mapa do PS1 em `web/src/recomp/main.ts` (`KEYBINDS_INI`).

Toolchain: fork **kem0x/psxrecomp** (branch `compat-web`) em `C:\tools\psxrecomp-web`, Emscripten 6.0.13
(`C:\tools\emsdk`). Script: `powershell -File tools\recomp\build-web.ps1 -Step framework|game|configure|build|copy|all`.
Projeto CMake: `tools/recomp/bag2/web/CMakeLists.txt` (API antiga `psxrecomp_add_runtime_target`,
`BIOS_INTERPRETER`, renderer software, `-sINVOKE_RUN=0`, `-sEXPORTED_RUNTIME_METHODS=FS,callMain,ENV`,
`-msimd128`). Config do jogo: `tools/recomp/bag2/game-web.toml` (C gerado em `generated-web/`, o fork emite
um único `SLUS_011.59_full.c` de 21 MB). Saída copiada para `web/public/recomp/` (bag2.js 267 KB,
bag2.wasm 7,5 MB, `bios/openbios.bin`), ignorada pelo git.

Página (`web/src/recomp/main.ts`): define `window.Module` (noInitialRun, canvas, print → log), injeta
`/recomp/bag2.js`, escreve no MEMFS `/bios/openbios.bin`, `/keybinds.ini`, `/game.toml` e o disco em
`/game/disc/` (File do seletor ou `/dev-disc.bin` do plugin do Vite, com `.cue` trivial gerado se faltar),
cria o `AudioContext` no clique (autoplay) e chama `Module.callMain([...])`. O Vite já manda COOP/COEP
(SharedArrayBuffer/pthreads).

Patch necessário no fork (`tools/recomp/patches/psxrecomp-web-overlay-cpu-writes.patch`, aplicado em
`runtime/src/memory.c`): o runtime só marcava como "dirty" páginas de overlay escritas por DMA do CD; o
BAG2 descomprime os `DBI_` com a CPU (BPE em `0x80035DC4`), então o primeiro despacho para o overlay
caía em `unknown dispatch`. Agora stores da CPU acima de `g_overlay_region_floor` marcam a página.

Overlays estáticos no WASM: `compile_overlays.py --static` do fork, alimentado com o
`static-inputs.json` do pipeline AOT do upstream (§8), gera `generated-web/overlays_static.c`
(2422 funções, arquivo único), linkado via `GAME_OVERLAY_STATIC_C`. Sem isso tudo que é overlay roda no
interpretador MIPS (`overlay_backend = "interp"`).

## 8. Overlays compilados antecipadamente (AOT) — 2026-10-23

- Inventário: `tools/recomp/aot/dbi_inventory.json` (104 blocos `DBI_`, offsets absolutos no
  `BUST2.BIN`, SHA-256, classe de destino pelos ponteiros absolutos do payload).
- Perfil: `tools/recomp/aot/overlays.json` gerado por `make_profile.py` (schema `psxrecomp AOT methods
  v1`, `fixed_address_extents`, `allow_missing`, 56 imagens únicas — 159..194 e 252..485 são cópias
  byte a byte). O overlay principal 3 tem tabelas de dados que o descobridor estático tentava decodificar
  como código (`UNSUPPORTED_INSTRUCTION` em `0x800956B0..`); resolvido com `excluded_ranges`.
- `python aot_overlay_pipeline.py extract|static ...` (cwd `C:\tools\psxrecomp\tools`, gcc do WinLibs no
  PATH). O recompilador procura `bios/SCPH1001.toml` em `--project-root` = pasta do `game.toml`:
  `tools/recomp/bag2/bios` é uma junção para `C:\tools\psxrecomp\bios`. Resultado: 55/55 shards ok →
  `generated/overlays_static.c` + 56 unidades, linkadas com
  `GAME_OVERLAY_STATIC_C "${CMAKE_CURRENT_SOURCE_DIR}/generated/overlays_static.c"` (caminho absoluto:
  com relativo o glob das unidades `_NNNN.c` falha e o link dá 34k referências indefinidas).
- Endereços de carga dos overlays: `notes/formats.md` (seção "Overlays DBI_").
- Resultado no nativo (2026-10-23 02:13): com os overlays estáticos linkados, uma partida completa roda a
  60 fps (`emu_busy` ~37 %) e o `overlay_captures.json` gerado ao sair contém só os patches de kernel do
  PsyQ (`0x80002000`, `0x8000D000`), uma página do exe (`0x8004B000`) e 4 PCs em `0x80091000` — antes
  eram ~12 mil PCs interpretados por partida. Esses restos podem ser compilados com
  `compile_overlays.py --static --captures overlay_captures.json` e somados aos shards.
- `tools/recomp/bag2/CMakeLists.txt`: o `psxrecomp_overlay_static_sources` do upstream não encontrou as
  unidades nem definiu `PSX_HAS_OVERLAY_DISPATCH` nesta máquina; o projeto adiciona as unidades por
  `file(GLOB ...)` + `target_sources` e define a macro explicitamente.
- Web: `GAME_OVERLAY_STATIC_C` do fork gera `psx_overlay_dispatch` como `switch (addr)` **sem guarda de
  CRC** e com `case` duplicados (36 overlays de personagem no mesmo endereço): não compila
  (`redefinition of 'func_801CC590'`). A build web continua com `overlay_backend = "interp"` até portar o
  despacho guardado do upstream (`psx_ov_static_ranges` + `psx_overlay_static_code_matches`) para o fork,
  ou o suporte Emscripten do fork para o upstream.
- **Web com overlays estáticos (2026-10-23 07:00)**: `tools/recomp/split_static_overlays.py` corta o
  `overlays_static.c` do fork em uma TU por overlay (`generated-web/ov/ov_NNN.c`, funções `static`) e gera
  `overlays_dispatch.c`: por região de carga (4), a variante presente é reconhecida pelos 16 primeiros
  bytes do payload (únicos entre as 55 imagens) e despachada; `PSX_HAS_OVERLAY_DISPATCH=1` liga a chamada
  em `dirty_ram_interp.c`. Partida completa no navegador sem `unknown dispatch`; ~56 vblank/s (o gargalo
  é o renderer software + MDEC, não o despacho). `bag2.wasm` passa de 7,5 para 28,6 MB.
- **Apresentação WebGPU**: patch `tools/recomp/patches/psxrecomp-web-frame-export.patch` exporta
  `psx_web_frame_ptr/w/h/serial` (buffer ARGB do renderer software); `web/src/recomp/webgpu.ts` copia o
  frame do heap (`HEAPU8` exportado) para uma textura `bgra8unorm` e desenha com filtro sharp-bilinear e
  scanlines opcionais num canvas sobre o do SDL.

## 9. Desempenho da build web (2026-10-23 08:30)
Medido nesta máquina (Chrome, 8 threads) com `psx_web_perf_*`:

| Build | abertura (MDEC) | partida | CPU livre | underruns de áudio |
|---|---|---|---|---|
| inicial (`BAG2_WEB_DEBUG=ON`: `-sASSERTIONS=1 --profiling-funcs`) | 36 vblank/s | 55 | 0 % | milhares |
| sem ASSERTIONS, `-msimd128` global | 58 | 56 | 1 % | ~0,4/s |
| + `-flto -mnontrapping-fptoint -mbulk-memory` | 59,95 | 59,7 | 27–36 % | 0 (fora do boot) |

Ajustes:
- `tools/recomp/build-web.ps1` configura com `-DBAG2_WEB_DEBUG=OFF` (o default do CMake já era OFF, mas o
  cache antigo estava ON).
- `tools/recomp/patches/psxrecomp-web-audio-rate.patch` (`runtime/src/web_audio_bridge.c`): controle
  dinâmico de taxa — `playbackRate` entre 0,92 e 1,03 conforme a fila de áudio, suavizado; quando a
  emulação atrasa o som desacelera um pouco em vez de cortar. `Module.psxWebAudio.rateControl = false`
  desliga. HUD da página mostra vblank/s, CPU livre, underruns, fila e taxa.
- Teclado: o SDL ouve na janela; a página devolve o foco ao canvas em qualquer clique na área do jogo e
  bloqueia Tab/setas de mudar foco ou rolar.
- **Latência de áudio (sincronia do ritmo)**: a ponte do fork segurava 160 ms de fila no Windows
  (`minimumLead`) + `latencyHint: 0.04` → ~237 ms entre o jogo "tocar" e o usuário ouvir. Agora a página
  cria o `AudioContext` com `latencyHint: 'interactive'` (baseLatency 10 ms) e reimpõe
  `Module.psxWebAudio.targetLead` a cada segundo a partir do slider "fila de áudio" (padrão 50 ms):
  atraso total medido ≈ 120 ms (fila ~60 + dispositivo ~58), 0 underruns com 35 % de CPU livre. O
  HUD mostra o atraso estimado. Abaixo de ~40 ms pode voltar a cortar; o exe nativo (SDL direto) tem
  latência bem menor que qualquer navegador.

## 10. Versão PC melhorada (2026-10-23)
`tools/recomp/bag2/game.toml` `[video]`: `internal_resolution = "1080p"` (5x de 240 linhas = 1200,
resolvido para a janela), `geometry_correction`/`perspective_texturing` (PGXP), `texture_filtering = "xbr"`
(o log imprime "bilinear" para qualquer filtro ≠ nearest), `antialiasing_mode = "fxaa"`, `dithering = "off"`,
`render_thread = true`, `vsync = "on"`, presets `[quality.*]` (chaves permitidas: ver QUALITY_PRESETS.md —
`antialiasing_mode` não é preset; `antialiasing` bool é). Superado por §11 (9x, sem FXAA, 16:9). `settings.toml` ao lado do exe:
`fullscreen = 1` (sem borda 1920×1080 confirmado), `window_width = 1920`. `Jogar.bat` lança com o disco.
Capturas: `tools/recomp/logs/cap-1080p-*.png`. Backups: `releases/v1.0.0-estavel`
(intacto) e `releases/v1.1.0-1080p`.

## 11. Widescreen 16:9 nativo + 9x (2026-10-23)
Nitidez: `internal_resolution = "4k"` (9x de 240 = 2160 linhas, resolvido para 1080p = supersampling 2x por
eixo), `antialiasing_mode = "off"` (FXAA só borrava), PGXP e xBR mantidos. Teto da nitidez por renderização:
os polígonos já têm borda perfeita; o que resta serrilhado/borrado são as **texturas** (TIM de 4/8 bpp,
64–256 px) e os **sprites 2D** do HUD/menus, que são bitmaps nativos. O próximo degrau é o pacote de texturas
HD do runtime (`psx.enhancement.hd-textures`, formato DuckStation): ativar "Dump textures", jogar, ampliar os
PNG de `mods/texture-packs/SLUS-01159/dumps` (ESRGAN/xBR) mantendo nome, proporção e múltiplo inteiro, e
colocar em `replacements/` com "Load replacements" (só OpenGL; `docs/HD_TEXTURE_PACKS.md`).

Widescreen: `aspect_ratio = "16:9"` sozinho é recusado ("widescreen is mod-owned on PSX; clamping ... -> 4:3").
É preciso um plugin estático do jogo (`src/bag2_mods.c`: `psx_mod_set_fixed_display_aspect(16,9)` registrado
por `PSX_MOD_CONSTRUCTOR` + `psx_mod_register_activation_plugin("bag2.widescreen", ...)`) ligado por um pacote
de mod (`mods/preloaded/packages/bag2.enhancement.widescreen/1.0.0/manifest.toml`, format 5, feature
`default_enabled = true`), apontado no CMake por `PRELOADED_MODS_DIR` + `target_sources`. Modo "native-wide":
a área de desenho cresce 320 → 426 px (offset 53 via draw_offset), o GTE projeta mais mundo nas laterais,
nada é esticado. **`[widescreen] gte_game_mode = true` é obrigatório**: sem ele o classificador de cena
só reconhece "jogo" por `sprite_tag_funcs` e a partida alternava entre 16:9 e 4:3 pillarbox a cada poucos
quadros. Com ele, qualquer quadro com ≥3 vértices projetados pelo GTE (histerese 45 quadros) é jogo;
menus 2D, FMV (STR) e BIOS ficam em 4:3 centralizado automaticamente. `hud_sprt_squash`/`auto_ui_squash`
com `auto_ui_anchor = "edges"` ancoram as barras HEAT/HIRO nos cantos da tela larga.
Verificado em partida (palco de boate: colunas de luzes extras nas laterais, personagens em proporção
correta, HUD nos cantos; seleção de personagem e abertura em 4:3). Limite: em câmeras muito próximas
da borda do palco aparece o fim da geometria autoral (preto) — não há mais cena para revelar.
Capturas: `tools/recomp/logs/cap-ws-{stage,closeup,menu43}.jpg`. Release:
`releases/v1.2.0-widescreen`. Não usar `[widescreen.cull] auto_screen_x` por enquanto
(exige regen do código; o jogo não mostrou pop-in de polígonos nas bordas).

Captura de tela: `PrintWindow` e `CopyFromScreen` voltam preto com a janela GL sem borda; o que funciona é
a captura de tela do app (DXGI). Sempre `Stop-Process -Name Bust_A_Groove_2_Recompiled` depois dos testes.
