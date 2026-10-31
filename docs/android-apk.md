# APK Android do Bust A Groove 2 recompilado

Fase 7 do remake (`docs/remake-plano.md`), decidida em 2026-10-30: o Android sai como **APK nativo do mesmo
jogo recompilado** (runtime do PSXRecomp + C gerado + plugins do remake), não mais pelo Polygonia (fork do
DuckStation, que fica como está; `docs/android-fork.md` continua valendo só para ele). O APK leva disco e BIOS:
**uso próprio, nunca Google Play nem repositório público**, e nenhum dado do jogo entra no git.

Aparelho de referência: Samsung Galaxy Tab S10 Ultra (SM-X920), Android 16 (API 36), Dimensity 9300+, GPU
Mali-G720-Immortalis MC12 (OpenGL ES 3.2, Vulkan), 12 GB, tela 2960×1848.

## 1. Arquitetura

```
APK (Gradle, android/)                         Aparelho (primeira execução)
├── lib/arm64-v8a/libBust_A_Groove_2_Recompiled.so   runtime + jogo + SDL3 estático (47 MB com strip)
├── classes.dex  SDLActivity (SDL3 3.4.10) + BAG2Activity + SetupActivity + AssetInstaller
└── assets/bag2/ game.toml, disc/, bios/, mods/{bundled,remake,texture-packs}, settings/keybinds/input
                 ──SetupActivity──► /sdcard/Android/data/io.github.betinn.bag2/files/ (4,0 GB)
```

- **Uma só biblioteca nativa.** O projeto do jogo (`game/CMakeLists.txt`, o mesmo do PC) é configurado com a
  toolchain do NDK 28 (`recomp/android/build.sh configure`); o framework, sob `ANDROID`, cria o alvo como
  `add_library(SHARED)` em vez de `add_executable` (SDL3 exige: a Java `SDLActivity` carrega a `.so` e chama o
  `SDL_main` dela). O SDL3 entra estático (FetchContent do framework, mesma versão do PC); o `JNI_OnLoad` e os
  `Java_org_libsdl_app_*` são exportados pela nossa `.so`, então `getLibraries()` devolve só ela.
- **O Gradle só empacota.** A `.so` é compilada fora do Gradle (cmake + ninja, ~2,5 min para os 600 objetos no
  PC) e copiada com `llvm-strip` para `android/app/src/main/jniLibs/arm64-v8a/`; os dados do jogo entram em
  `android/app/src/main/assets/bag2/` por links físicos (`recomp/android/stage.py full`). As duas pastas são
  ignoradas pelo git.
- **Dados em arquivos comuns.** O runtime lê `game.toml`, disco, BIOS, mods e cartões por `fopen`/`ifstream`
  ancorados na "pasta do exe". No Android essa âncora é `SDL_GetAndroidExternalStoragePath()`
  (`/sdcard/Android/data/io.github.betinn.bag2/files`), e a `SetupActivity` extrai `assets/bag2/**` para lá na
  primeira execução (ou quando o carimbo do manifesto muda), com barra de progresso; depois abre a
  `BAG2Activity`. A pasta é acessível por `adb` (push/pull de dados, `log.txt`, capturas), o que dá o ciclo de
  desenvolvimento: **APK pequeno (modo `external`, sem assets) + `adb push` dos dados uma vez.**
- **Debug server pela USB.** A build Android sai com `PSX_DEBUG_TOOLS=ON` por padrão (`BAG2_ANDROID_DEBUG_TOOLS`):
  `adb forward tcp:4370 tcp:4370` e os scripts de `tools/remake/` e `tools/tests/` (`game_session.py`,
  `game_perf.py`, comandos `heat_*`, `stage_*`) falam com o jogo no tablet como falam com a build-debug do PC.
- `stdout`/`stderr` do runtime vão para o logcat (tag `psxrecomp`) e para `<files>/log.txt` (patch).

## 2. O que mudou no framework: `recomp/patches/psxrecomp-android.patch` (4º patch, depois dos três do PC)

Tudo atrás de `__ANDROID__` / `PSX_GLES` (definido em `runtime.cmake` só quando `ANDROID`): a build do PC compila
o mesmo código de antes.

| Arquivo | Mudança |
|---|---|
| `runtime/runtime.cmake` | `add_library(SHARED)` no Android; link com `GLESv3 EGL android log dl`; `PSX_GLES=1` |
| `runtime/src/psx_fiber.c` | backend AArch64 próprio (o bionic declara `makecontext`/`swapcontext` mas não os implementa): troca de contexto em assembly guardando x19–x30 e d8–d15 na pilha da fibra, trampolim que chama a entrada |
| `runtime/src/gpu_gl_renderer.c` | caminho **GLES 3.2** (§3) |
| `runtime/src/main.cpp` | contexto `SDL_GL_CONTEXT_PROFILE_ES` 3.2; `exe_dir_from_argv` devolve a pasta de arquivos do app; `chdir` nela no começo do `main` (os plugins abrem `mods/remake/*.bin` por caminho relativo); `stdout`/`stderr` para logcat + `log.txt` |
| `runtime/src/quality_probe.c` | perfil ES na sonda da GPU (segunda janela: no Android falha e a sonda só desiste) |

Fora do framework: `game/src/bag2_heat.c`/`bag2_stage.c` passam `gles = 1` ao núcleo `src/remake/` quando
`PSX_GLES` (o núcleo já escolhia `#version 300 es` por essa flag desde a fase 1); `timespec_get` exige API 29
(`minSdk 29`).

## 3. GL 3.3 core → OpenGL ES 3.2 no `gpu_gl_renderer.c`

Levantamento (2026-10-30): o renderizador usa ~90 entradas de GL, quase todas no núcleo do ES 3.0. O que não
tinha par e como ficou (tudo sob `PSX_GLES`, com os mesmos nomes para o corpo do arquivo não mudar):

| GL 3.3 | ES 3.2 (Mali-G720) |
|---|---|
| `#version 330` (29 shaders, todos por `compile_shader`) | reescrito em tempo de execução para `#version 320 es` + `#define PSX_GLES` + `precision highp` (float, int, samplers) |
| `noperspective` (varyings `v_col`, `v_uv`, `v_b3d`, `v_npos` dos programas GEO e TEX: a interpolação afim do PS1) | não existe no GLSL ES (nem a extensão NV na Mali). Emulado: o vertex shader manda `v*w` e `w`, o fragment divide (`lerp_tela(v·w)/lerp_tela(w)` = interpolação linear na tela, exata). Onde `w = 1` (sem PGXP de perspectiva) não muda nada |
| *dual-source blending* (`glBindFragDataLocationIndexed`, `GL_SRC1_ALPHA`: lote misto opaco/semitransparente do modo 4 num desenho só) | `EXT_blend_func_extended` não existe na Mali; usa `EXT_shader_framebuffer_fetch` (`inout frag`): o shader lê o destino e faz `src + dst·fator` ele mesmo, com `GL_BLEND` desligado nesse caminho |
| `glGetTexImage` (2 usos, só dumps) | textura num FBO temporário + `glReadPixels` |
| `glReadPixels(GL_BGRA)` (4 usos) | leitura RGBA + troca R/B na CPU (`EXT_read_format_bgra` só vale para o formato preferido) |
| `glTexImage2D(GL_RGBA, …, GL_BGRA)` | `internalformat = GL_BGRA_EXT` (`EXT_texture_format_BGRA8888`) |
| `glClearDepth`, `GL_BACK_LEFT` | `glClearDepthf`, `GL_BACK` |
| `glMapBuffer`, `glQueryCounter`/`GL_TIMESTAMP`, `GL_TIME_ELAPSED` | já eram opcionais (ponteiro nulo desliga o recurso); a Mali não tem *timer query* (`EXT_disjoint_timer_query` ausente), então `gpu_timers` não mede no tablet |

Extensões conferidas no aparelho (`dumpsys SurfaceFlinger`): `EXT_color_buffer_float` (alvos RGBA32F do PGXP e do
remake), `EXT_shader_framebuffer_fetch`, `ARM_shader_framebuffer_fetch`, `EXT_texture_format_BGRA8888`,
`EXT_sRGB_write_control`, `EXT_clip_control`, `EXT_texture_filter_anisotropic`, `OES_texture_float_linear`,
`KHR_texture_compression_astc_ldr`, `EXT_texture_border_clamp`. Ausentes: `EXT_blend_func_extended`,
`EXT_disjoint_timer_query`, `NV_shader_noperspective_interpolation`.

## 4. Build e ciclo de desenvolvimento

```bash
bash recomp/android/build.sh configure      # uma vez (cmake + NDK, SDL3/zlib/libchdr/libwebp das fontes já baixadas pela build do PC)
bash recomp/android/build.sh native stage apk install   # .so (~2,5 min) + APK pequeno (modo external) + adb install
bash recomp/android/build.sh push run       # dados (4 GB, uma vez) e logcat
BAG2_STAGE_MODE=full bash recomp/android/build.sh stage apk   # APK completo (assets dentro)
```

- `recomp/android/env.sh`: SDK/NDK em `C:\tools\android-sdk` (NDK 28.2.13676358, build-tools 36, platform 36 já
  instalados pelo Polygonia), JDK 21 em `C:\tools\jdk-21.0.12.1+1`, cmake/ninja do pip. Gradle 8.9 + AGP 8.7.3
  (wrapper copiado do Polygonia; distribuição já em `~/.gradle`).
- Assinatura: `C:\Users\sem-m\BAG2-keys\` (keystore própria, alias `bag2`, RSA 4096; `LEIA-ME.txt` pede backup;
  `app/build.gradle` lê o caminho em `BAG2_KEYSTORE_PROPERTIES`). Debug e release assinados com a mesma chave para
  `adb install -r` trocar uma pela outra sem desinstalar (desinstalar apaga os 4 GB extraídos).
- Artefatos assinados ficam em `C:/Users/sem-m/BAG2-keys/artefatos/` (fora do repo): `BustAGroove2-2.0.0-android.84-completo.apk`
  (1,46 GB, certificado SHA-256 `d9bce567…b6d5f5`, `versionCode` 84 = nº de commits).
- Tamanhos: `.so` 304 MB sem strip (fica em `game/build-android/` para `ndk-stack`), 47 MB com strip. Dados: 4,0 GB
  (remake 3,3 GB em 66 assets, disco 585 MB, pacote HD 132 MB); zlib reduz os assets do remake a ~30 % e o disco a
  ~67 %, então o APK completo deve ficar em ~1,6 GB (o aapt comprime `assets/`).

## 5. Estado e medições (2026-10-30)

O que já roda no tablet (build de desenvolvimento, `PSX_DEBUG_TOOLS=ON`, dados por `adb push`):
- Boot pelo OpenBIOS, abertura (FMV), menus 4:3 com faixas, partida 2P pelo harness
  (`python tools/tests/android_session.py 30 HEAT COMET 0`: `who()` = HEAT, COMET) no 233 com **personagens
  (Phong + texturas HD), geometria e pós-efeito do palco, pacote HD e 16:9** ligados — `heat_chars`: 2 assets
  carregados, 306 MB de GPU; `stage_stats`: `ready=1`; nenhum shader recusado pelo compilador da Mali.
- Paisagem forçada (`SDL_ORIENTATIONS` + targetSdk 35), janela 2960×1848, painel de 120 Hz (o runtime mantém o
  ritmo de 59,94 Hz por relógio de parede; `GL temporal frame blending disabled (host 120.0 Hz)`).
- Temperatura da bateria 26 °C depois de ~4 min de partida; processo com ~1,0 GB residentes (22 GB virtuais).

### Medições (`tools/tests/android_perf.py`, 60 s por janela na partida, log por segundo)

| variante (escala interna 5x = 1200 linhas) | fps méd / mín | s < 59,5 | jogo méd/máx | render méd/máx | underruns |
|---|---|---|---|---|---|
| remake inteiro (`heat_mode 1; stage_post 1; stage_geo 1`), 1 dançarino* | 59,9 / 59,7 | 0 | 49 / 51 % | 67 / 79 % | 0 |
| original (`0;0;0`), 1 dançarino* | 59,9 / 59,6 | 0 | 53 / 56 % | 86 / 90 % | 0 |

\* nessa rodada a rota do 2P deixou o P2 vazio (ver `enter_match_checked`). Como no PC, o remake **baixa** a
render thread (o desenho de host substitui os pacotes do PS1 e as texturas HD por lotes grandes).

### APK completo (assets dentro), instalado por `adb install`
`BAG2_STAGE_MODE=full bash recomp/android/build.sh stage apk`: 1,46 GB (Gradle leva ~5 min no merge dos assets),
`adb install -r` 79 s. Primeira execução com a pasta de dados vazia: a `SetupActivity` extraiu os 4,1 GB em **21 s**
(704 arquivos: 66 assets do remake, 620 texturas HD, disco, BIOS) e abriu o jogo; na execução seguinte o carimbo
`.bag2-assets-version` bate e o jogo abre direto. Atualizar o APK (`adb install -r`, mesma assinatura) não apaga
`Android/data`, então só o que mudar de tamanho é extraído de novo; desinstalar apaga tudo (inclusive `saves/`).

Dois `psx_freeze_dump_*.json` (100–124 MB) apareceram na pasta de dados às 23:48 e 23:53: foi a janela em que um
`adb forward tcp:4370` esquecido fez o harness do PC e o do tablet mandarem comandos (`quit`, `press`) ao mesmo
jogo; apagados, não voltaram. O watchdog de congelamento escreve na pasta de dados: conferir se aparecer.

### Entrada
- `[controller]` do `game.android.toml`: P1 `auto` (padrão da build com debug tools: primeiro gamepad SDL) e
  `p2_device = "gamepad"` (o próximo livre: `open_player` reserva um aparelho por porta). O SDL3 enumera os
  controles Bluetooth e USB pelo `SDLControllerManager` da activity; o mapeamento é o de `input.ini`.
- Teclado (Bluetooth ou `adb shell input keyevent`): só com `p1_device = "keyboard"` (sem gamepad, o P1 `auto`
  fica sem fonte). Conferido no tablet com essa troca: `KEYCODE_ENTER` na abertura levou ao menu principal
  (`keybinds.ini`, start = Return); um `keyevent` seco de DPAD/X é curto demais para o jogo ler (como o toque curto
  no PC): usar `input keyevent --longpress` ou o `press` do servidor TCP.
- **Pendente (precisa do aparelho físico):** parear um controle Bluetooth e conferir o P1; parear o segundo e
  conferir o 2 jogadores (P2 no segundo controle).

### PC sem regressão
`game/build` e `game/build-debug` recompilados com o 4º patch (só objetos do framework; o C gerado não muda).
Partida 2P HEAT × COMET no 233 pela build-debug (`game_session.py 12 HEAT COMET 0`): 59,9 fps, jogo 31 %, render
38 % a 9x, capturas de 4608×2160 iguais às da fase 6c (remake inteiro). Cuidado: um `adb forward tcp:4370`
esquecido faz o harness do PC falar com o tablet (por isso o do Android usa a porta 4371).

### Partida 2P inteira a 5x (HEAT × COMET no 233, 4 janelas de 45 s seguidas, `tablet_escala`)

| janela | fps méd / mín | s < 59,5 | jogo méd/máx | render méd/máx | underruns | bateria |
|---|---|---|---|---|---|---|
| 1 | 59,9 / 59,8 | 0 | 49 / 51 % | 79 / 87 % | 0 | 26,6 °C |
| 2 | 59,9 / 59,7 | 0 | 50 / 52 % | 71 / 90 % | 0 | 26,6 °C |
| 3 | 59,9 / 59,6 | 0 | 50 / 64 % | 58 / 76 % | 0 | 26,6 °C |
| 4 | 59,9 / 59,7 | 0 | 49 / 50 % | 39 / 42 % | 0 | 26,7 °C |

**Decisão: escala interna 5x** (`internal_resolution = "1080p"`, 2560×1200 na tela larga, apresentado em
2960×1665). A render thread chega a 90 % nos picos da música; 6x custaria 1,44× os pixels e passaria do quadro.
`dynres_force` não vale com `dynamic_resolution = false` (a varredura ao vivo exige ligá-la; fica como opção se
um aparelho mais fraco precisar: piso 3x, teto 8x). A Mali não tem *timer query*, então o tempo de GPU por
peça (`gpu_timers`) não é medido no tablet; a medida é a ocupação da render thread.

Rota do 2P no tablet: na primeira tentativa depois do boot o P2 ficou vazio (o `set_input` da porta 2 feito na
hora da seleção não foi visto); `enter_match_checked` repete pela pausa/QUIT e o harness agora liga a porta 2
logo depois do `ping`.
