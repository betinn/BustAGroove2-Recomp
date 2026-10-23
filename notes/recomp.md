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
