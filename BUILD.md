# Build do zero

Como sair de uma máquina Windows limpa e do disco original até o exe com o remake completo, igual à release
`v2.0.0-remake`. Cada passo diz o que gera e onde; o que já vem no git não precisa ser gerado.

O que **não** está no git e por quê: o disco e tudo o que sai dele (C gerado, arquivos extraídos, assets do remake,
texturas HD) são dados do jogo e o repositório é público. As ferramentas de terceiros (framework, compilador,
Real-ESRGAN) ficam em `C:\tools`, nas versões fixadas abaixo.

## 0. Requisitos

| Ferramenta | Versão | Onde (os scripts esperam este caminho) | Como |
|---|---|---|---|
| Windows | 10/11 x64, GPU com OpenGL 3.3 (Vulkan para o Real-ESRGAN) | — | — |
| Git for Windows | qualquer recente (o `bash` é usado no passo 3) | `C:\Program Files\Git` | instalador |
| Python | 3.14 | no PATH | instalador |
| GCC MinGW-w64 UCRT (WinLibs) | 16.2.0 posix/seh r2 | `C:\tools\winlibs\mingw64\bin` | zip do WinLibs (`BrechtSanders.WinLibs.POSIX.UCRT` no winget), extraído |
| CMake + Ninja | 4.4.4 / 1.13.2 | `%APPDATA%\Python\Python314\Scripts` | `pip install --user cmake ninja` (o cmake do WinLibs falha nos downloads) |
| PSXRecomp | commit `31df2e1e` + 3 patches deste repo | `C:\tools\psxrecomp` | passo 2 |
| Real-ESRGAN ncnn | v0.2.5.0 (release oficial, Windows) | `C:\tools\realesrgan\bin\realesrgan-ncnn-vulkan.exe` | zip do GitHub `xinntao/Real-ESRGAN`, extraído (só para os passos 7 e 8) |

`tools/recomp/env.ps1` põe essa toolchain no PATH (`. .\tools\recomp\env.ps1` numa sessão PowerShell). O SDL3 é
baixado pelo próprio CMake no primeiro build.

## 1. Repositório e disco

```bash
git clone https://github.com/betinn/BustAGroove2-Recomp
```

Coloque o seu dump em `disc/Bust A Groove 2 (USA).bin` e `.cue` e confira o SHA-1 contra
[disc/README.md](disc/README.md). Todo o resto lê o disco dali.

## 2. Framework PSXRecomp com os patches locais

```bash
git -c core.autocrlf=false clone --recursive https://github.com/RetroPortingToolKit/psxrecomp C:/tools/psxrecomp
cd C:/tools/psxrecomp
git checkout 31df2e1e
git apply --ignore-whitespace <repo>/tools/recomp/patches/psxrecomp-hd-cache-caps.patch
git apply --ignore-whitespace <repo>/tools/recomp/patches/psxrecomp-texture-bank-render-thread.patch
git apply --ignore-whitespace <repo>/tools/recomp/patches/psxrecomp-host-draw.patch
```

Nesta ordem. Sem o primeiro a partida cai para 13–35 fps; sem o terceiro os plugins do remake não compilam. Os
outros três patches de `tools/recomp/patches/` (`psxrecomp-web-*`) são da versão web abandonada e não entram.

O recompilador procura o perfil de BIOS dentro da pasta do projeto do jogo; crie a junção (PowerShell):

```powershell
New-Item -ItemType Junction -Path tools\recomp\bag2\bios -Target C:\tools\psxrecomp\bios
```

## 3. Ferramentas do recompilador e o C do OpenBIOS

```powershell
powershell -File tools\recomp\build.ps1 -Step framework   # psxrecomp-game, -toml, -bios (C:\tools\psxrecomp\recompiler\build)
powershell -File tools\recomp\build.ps1 -Step bios        # generated/OpenBIOS_*.c no framework (usa o bash do Git)
```

## 4. Extrair o disco e gerar o C do jogo

```bash
python tools/bust2_extract.py "disc/Bust A Groove 2 (USA).bin" extracted
```

Gera `extracted/disc/` (`SLUS_011.59`, `BUST2.BIN`, `XA/`, `STR/`) e `extracted/bust2/` (os containers do
`BUST2.BIN`). Depois:

```powershell
powershell -File tools\recomp\build.ps1 -Step game
```

Gera `tools/recomp/bag2/generated/SLUS_011.59_*.c` (o executável principal em C, ~27 MB) a partir de
`tools/recomp/bag2/game.toml` e `seeds/functions.txt`.

## 5. Overlays compilados antecipadamente (AOT)

O jogo carrega 56 overlays `DBI_` de dentro do `BUST2.BIN`; o perfil `tools/recomp/aot/overlays.json` diz quais e
onde. Com o `env.ps1` carregado, na pasta `C:\tools\psxrecomp\tools`:

```powershell
python aot_overlay_pipeline.py static --profile <repo>/tools/recomp/aot/overlays.json `
  --game-toml <repo>/tools/recomp/bag2/game.toml `
  --recompiler C:/tools/psxrecomp/recompiler/build/psxrecomp-game.exe `
  --work-dir <repo>/tools/recomp/aot/work-static --gcc C:/tools/winlibs/mingw64/bin/gcc.exe `
  --out-dir <repo>/tools/recomp/bag2/generated --workers 6
```

Gera `generated/overlays_static.c` e uma unidade por overlay, com os ganchos de instrução de
`[recompiler] mod_instruction_sites` do `game.toml`. Mudou essa lista ou `mod_function_entry_funcs`: repita os
passos 4 e 5.

## 6. O exe

```powershell
powershell -File tools\recomp\build.ps1 -Step runtime
```

Gera `tools/recomp/bag2/build/Bust_A_Groove_2_Recompiled.exe` e, ao lado, `bios/`, `assets/`, `mods/bundled/`,
`game.toml` e `settings.toml`. Ele já abre o jogo (com o disco de `disc/`), mas sem os assets do remake e sem as
texturas HD, que vêm nos passos 7 e 8.

No Windows PowerShell 5.1 o `build.ps1` às vezes aborta quando o cmake escreve um aviso em stderr. Nesse caso rode
o cmake direto, com o `env.ps1` carregado:

```powershell
cmake -S tools/recomp/bag2 -B tools/recomp/bag2/build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=gcc `
  -DCMAKE_CXX_COMPILER=g++ -DPSXRECOMP_ROOT=C:/tools/psxrecomp -DPSX_RECOMP_UI=OFF -DPSX_ENABLE_VULKAN=OFF
cmake --build tools/recomp/bag2/build --target psx-runtime
```

**Build de testes** (servidor TCP na porta 4370, usado pelos scripts de `tools/remake/`): o mesmo configure em
`tools/recomp/bag2/build-debug` com `-DPSX_DEBUG_TOOLS=ON`. Os passos 7 e 8 precisam dela.

## 7. Assets do remake (`mods/remake/`, 3,3 GB)

Ambiente Python dos exportadores:

```bash
python -m venv tools/recomp/logs/remake/venv
tools/recomp/logs/remake/venv/Scripts/python -m pip install -r tools/remake/requirements.txt
```

**Personagens** (os 18, nos dois figurinos: 36 arquivos `<nome>.bin` / `<nome>_alt.bin`), lidos direto do disco:

```bash
python tools/remake/export_all.py
```

**Palcos** (15 arquivos `palco231..245.bin` + `.stg`). Duas coisas vêm do jogo rodando, pela build de testes:
`tools/remake/stage_extra_keys.json` (já está no git; refazer com `stage_keys.py`) e as texturas e paletas lidas da
VRAM em `tools/recomp/logs/remake/palcos/vram/`:

```bash
python tools/remake/stage_vram_keys.py
python tools/remake/stage_clut_vars.py
tools/recomp/logs/remake/venv/Scripts/python tools/remake/export_stage.py
```

Os dois exportadores escrevem em `build/mods/remake/` e `build-debug/mods/remake/`.

## 8. Texturas HD (`mods/texture-packs/SLUS-01159/replacements/`)

As substituições são os dumps de textura do jogo ampliados 4x. Os dumps vêm de jogar com a captura ligada: em
`tools/recomp/bag2/build/mods/state.toml` ponha `dump = "true"` e `replacements = "false"` (como strings) no mod
`psx.enhancement.hd-textures`, jogue (feche pela janela, não pelo Gerenciador de Tarefas, senão o último lote se
perde) e, para os palcos, rode `python tools/remake/stage_inventory.py run` e `python tools/remake/stage_dump2.py`.
Depois:

```bash
python tools/hd_textures.py --filter esrgan
```

Este é o único passo que não se reproduz exatamente: o conjunto depende das telas capturadas (a v2.0.0 tem 620
substituições, parte capturada jogando à mão). A cópia de referência é a pasta da release.

## 9. Montar uma release

Ver [releases/README.md](releases/README.md): copiar o exe e as pastas da `build/`, o disco em `disc/`, o
`game.toml` com `disc = "disc/Bust A Groove 2 (USA).cue"`, escrever `VERSION.txt`. A pasta fica auto-suficiente
(dois cliques no exe).

## Onde aprofundar

- [notes/recomp.md](notes/recomp.md): o framework, o build, os overlays AOT (§8), 1080p/widescreen (§10, §11), as
  texturas HD (§12), o ritmo da música (§13).
- [notes/remake-plano.md](notes/remake-plano.md) e [notes/remake-visual.md](notes/remake-visual.md): o remake,
  fase a fase.
- [notes/formats.md](notes/formats.md), [notes/model-format.md](notes/model-format.md),
  [notes/media-formats.md](notes/media-formats.md): os formatos do disco.
