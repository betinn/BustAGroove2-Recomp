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

## 6. Segunda rodada: o que o usuário viu na primeira partida (2026-10-31)

Relato: o primeiro controle caiu no P2 e o segundo não fazia nada; "leves travadas" nos solos no palco do
STRIKE; menus e ícones da seleção/carregamento claros demais.

### Cores: transparência somada duas vezes no caminho ES
No lote misto opaco/semitransparente (modo 4) o desktop usa *dual-source blending* (`GL_ONE, GL_SRC1_ALPHA`); no
ES o shader soma `dst·fator` pelo *framebuffer fetch*, mas o `glEnable(GL_BLEND)` + `glBlendFuncSeparate(…SRC1…)`
continuava sendo chamado: o enum é inválido no ES, o erro deixava o fator anterior ligado e a GPU somava o destino
de novo por cima do que o shader já tinha somado. Corrigido (`glDisable(GL_BLEND)` nesse caminho sob `PSX_GLES`);
seleção de personagens conferida por captura (`android_check.py`).

### Controles: o padrão do runtime é teclado no P1
`PSX_DEBUG_TOOLS` não vira macro de compilação (só `PSX_NO_DEBUG_TOOLS` quando desligado), então o P1 nascia como
`"keyboard"` e o primeiro gamepad ia para a porta 2 (`open_player` reserva um aparelho por porta). Agora
`game.android.toml` tem `p1_device = "gamepad"` e `p2_device = "gamepad"`; no Android (patch, `pad_buttons_for`)
um assento "gamepad" sem controle cai no teclado, então o P1 sempre tem fonte. Log: `opened controller for slot 1
(instance 2): PS5 Controller` (DualSense por Bluetooth, mapeamento do SDL, nada a configurar).

### Engasgos: medição por quadro (`heat_hitch`)
O relatório por segundo esconde um quadro atrasado (o pacer recupera dentro do mesmo segundo). O plugin ganhou
`heat_hitch`: intervalo entre vblanks do jogo (`hitch_n` = intervalos > 25 ms), intervalo entre desenhos de host
na render thread (`rt_gap_n` = > 45 ms), tempo do pior desenho e dos passos de preparo GL; `stage_stats` ganhou
`prep_max_ms`/`prep_mip_ms`. `tools/tests/android_check.py` imprime isso a cada 10 s numa partida com capturas.

Achados no palco do STRIKE (2P STRIKE × HEAT, 150 s):
- **Entrada da partida**: um passo de preparo do palco de 119–197 ms (alocação dos dois arrays 1024² × n camadas
  por `TexImage3D`) e o `GenerateMipmap` do array inteiro com 59–63 ms, num quadro cada (fps 44–51 naquele
  segundo). Os personagens custam 9–13 ms. Feito: mipmaps na CPU na thread de carga (`rmk_asset_build_mips`,
  enviados nível a nível com cada camada), `glTexStorage3D` (imutável; `TexImage3D` por nível reespecificava a
  textura na Mali) e o array dos mapas de normal alocado num passo próprio → pior passo 62–80 ms, sem GenerateMipmap;
  o segundo da entrada ficou em 58,9 fps. A alocação de 250 MB numa chamada ainda custa esses 60–80 ms (uma vez por
  palco).
- **Durante a música**: 18–40 intervalos de 25–33 ms por partida no jogo, concentrados nos solos; na render thread
  os desenhos de host custam no máximo 2,8 ms, mas os intervalos > 45 ms entre eles se acumulam exatamente no
  trecho dos solos (33 → 62 em 30 s): a câmera fecha num dançarino e o shader por pixel (Phong, mapa de normal,
  sombra, especular) cobre a tela inteira a 5x; a GPU passa do quadro e o jogo, com 2 quadros em voo, só sente de vez
  em quando. Isto é o que o usuário sentiu.
- **4x contra 5x e disco no armazenamento interno**: a 4x (`internal_resolution = 960`) os intervalos > 45 ms da
  render thread caíram numa rodada (64 → 12) e não na seguinte (73); os do jogo não mudaram (35–66). Ler o disco do
  armazenamento interno (`/data/user/0/<pacote>/files`, ext4 direto, sem o FUSE do `Android/data`) também não mudou
  (45 / 40). **A variância entre rodadas idênticas é grande** (mesma config: 35–66 no jogo, 12–73 na render), então
  uma rodada só não decide nada abaixo de 2×; o que resta (um quadro atrasado a cada ~4–5 s na thread do jogo, de
  25–30 ms) fica **aberto**, com a ferramenta pronta para a próxima sessão (`heat_hitch`, `android_check.py`).
  A escala fica em 5x; `internal_resolution = 960` (4x) é o botão para um aparelho mais apertado.
- O que **não** era: a troca de buffer bloqueante a 120 Hz (`vsync = "off"` não mudou a conta: 33 contra 18–36) e
  pedir o modo de 60 Hz ao Android (piorou para 341: o "60 Hz" da Samsung alterna com 120 por baixo e, com o vsync
  mandando na cadência, cada troca atrasava o jogo). Prioridade de thread (`setpriority −8` no jogo e na render)
  também piorou (180) e foi desfeita. Os `psx_freeze_dump_*.json` de 100+ MB na pasta de dados vinham do `quit`: o
  driver da Mali aborta o processo no descarregamento (`ubsan: type-mismatch`) e o crash trace escrevia o dump;
  desligado no Android (patch em `crash_trace.c`).

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

## 7. Terceira rodada: o palco do ROBO-Z e a introdução das partidas (2026-10-31)

Relato do usuário depois de jogar bastante: o jogo está bom, mas no **palco do ROBO-Z (chefe final do 1 jogador)** a
música e o jogo travam muito, impossível passar; e alguns palcos (KITTY-N, KELLY) engasgam de leve na introdução da
partida (o voo da câmera antes de começar a dançar).

### Palco do ROBO-Z: a render thread presa no driver
- **Qual palco**: o 241 (heliporto, índice 11 do byte `0x8005FB56`), com o ROBO-Z GOLD gigante em cima do mar e o
  jogador no helicóptero. No 2P ele sai com `BAG2_UNLOCK=1 BAG2_STAGE_IDX=11 BAG2_MATCH="HEAT ROBO-Z_GOLD 0"`
  (`game_session.py` aceita `_` no lugar do espaço). O `log.txt` da sessão do usuário (68 min) tinha exatamente isso no
  fim: render 95–99 %, 45–55 fps por um minuto, e a sessão encerrada ali.
- **Reproduzido** (`android_check.py`, 2P HEAT × ROBO-Z GOLD no 241): 108 de 237 segundos abaixo de 59,5 (45–52 fps),
  render 96 %, `hitch_n` 1 502 em 90 s (≈190 quadros atrasados a cada 10 s; nos outros palcos 35–66 por partida
  inteira), 400–700 underruns a cada 18 s.
- **O que não era**: desligar personagens, geometria do palco ou pós-efeito (`android_perf.py`, um de cada vez) não
  mudou nada (render 90–96 % em todas; os personagens originais, pelo renderizador base, caíam a 30 fps); escala
  interna 3x igual a 5x (`video_set`); sem o espelho da tela larga (`gl_ws_ablate 1`) ou sem trocar de FBO
  (`gl_ws_ablate 3`) também igual. Janelas seguidas pegam trechos diferentes da música: o A/B confiável foi por
  **savestate** (o mesmo trecho de 6 s para cada variante, `tools/tests/android_ab.py`; o savestate fica na memória,
  então só compara dentro da mesma sessão).
- **Perfil de CPU no aparelho** (`tools/tests/android_profile.py`: `simpleperf`, novo `<profileable android:shell="true"/>` no manifesto; no Samsung só o
  evento padrão funciona, `cpu-clock`/`task-clock` são recusados): `simpleperf record --app io.github.betinn.bag2 -g
  --duration 10`, `binary_cache_builder.py -lib game/build-android` e `simpleperf report --symfs binary_cache` no PC
  (a `.so` sem strip tem os símbolos); `llvm-addr2line` nos endereços de retorno dá a linha do código que chamou o driver.
  A render thread (≈38 % das amostras do processo) e a thread de comandos do driver (`mali-cpu-comman`, 24 %) estavam
  presas em duas coisas:
  1. **`glBindFramebuffer`, 33 %**: cada lote texturizado do renderizador base fazia `hr_begin` (FBO de alta resolução)
     e `hr_end` (FBO 0). Na Mali (GPU por blocos) trocar de FBO fecha o passo de render (o driver grava os blocos e acorda
     a thread de comandos). Corrigido no 4º patch, só sob `PSX_GLES`: cache do FBO ligado em `gl_bind_fb_redirect`
     (ligar o mesmo FBO não chama o driver; apagar FBO e callback de plugin esquecem o cache) e `hr_end` sem voltar ao
     0 (a apresentação liga o 0 ela mesma). Lote texturizado: 17,5 % → 4,7 % da render thread.
  2. **`BufferSubData` do buffer de instâncias do palco, 21 %** (`rmk_stage.c`): cada sequência de pacotes do palco
     (dezenas por quadro, intercaladas com o jogo na ordem de pintor) escrevia num trecho do mesmo buffer, que desenhos
     ainda não executados liam; a Mali copiava o buffer inteiro a cada escrita (*copy-on-write*,
     `gpudMiscDisableCOW` no perfil). Agora cada sequência tem um buffer próprio do quadro, enchido por `BufferData`
     (um conjunto que cresce de 16 em 16 e é reaproveitado no quadro seguinte; o espelho da tela larga reusa o da
     sequência). Vale para o PC também (55 sequências por OT no 233, 4,5 µs por desenho, sem regressão).
- **Resultado** no mesmo trecho do savestate: 45 → 60 quadros/s, render 96 % → 51 %, `hitch_n` 115 → 2 a cada 6 s; o
  original (`0;0;0`) também foi a 60. Partida inteira (`android_check.py robo241_2`, 140 s de música): `hitch_n` 19
  (pior 29 ms), nenhum segundo abaixo de 59,5 na partida (os 3 do log são da abertura em FMV).
- **Confirmação, build final** (`android_perf.py robo_final1/2`, duas sessões, 2 janelas de 50 s cada no 241 com
  HEAT × ROBO-Z GOLD): 59,9 fps de média, mínimo 59,7–59,9, **nenhum segundo abaixo de 59,5, 0 underruns**, jogo 51 %,
  render 57–61 % (máx 64 %), `hitch_n` 3–7 por janela (pior 28 ms), no nível dos outros palcos. Capturas da partida e
  dos menus no tablet sem diferença visível (`final_vis_*.png`).

### Introdução das partidas, palco por palco
- **Medida** (`tools/tests/android_intro.py`): uma sessão, 2P HEAT × COMET com os 18 liberados, uma partida por palco
  (posições 0..10 da tela de palco, 241 e 245 pelo índice); `heat_hitch` e `stage_stats` zerados no menu, lidos a cada
  0,5 s até 28 s depois do X na tela de palco (carga + voo da câmera + começo da música); maior intervalo entre vblanks
  da thread do jogo (> 50 ms costuma ser audível), pior passo de preparo GL dos personagens e do palco, segundos do log
  abaixo de 59,5 e underruns do áudio. Cada partida recarrega os dois personagens e o palco (a descarga é 3 s depois do
  último desenho). A primeira linha é sempre a primeira partida da sessão.
- **Antes** (já com o buffer por sequência): 234 188 ms, 242 126, 235 85, 236 80, 233 60, 237 52; preparo do palco até
  211 ms num quadro e dos personagens até 95 ms; underruns no 234 (5) e no 242 (3).
- **Linha do tempo** (234; `heat_hitch`, `stage_stats` e o ponteiro da partida lidos a cada 0,25 s): o ponteiro da partida liga ~6 s depois do X; a carga de CPU do
  palco (arquivo de 264 MB, mapas de normal, mipmaps) começava aí e o preparo GL caía no voo da câmera: 63 ms (array das
  camadas) e 158 ms (array dos mapas de normal) → quadros do jogo de 51 e 140 ms.
- **O que mudou** (núcleo `src/remake/` e plugins; tudo vale para o PC, menos o que diz GLES):
  1. Criação GL em passos, um por quadro: palco = programa, buffers/texturas pequenas, array das camadas (o array dos
     mapas de normal já tinha passo próprio); personagem = programa, buffers, sombra no chão, array das camadas, array dos
     mapas de normal. Pior passo dos personagens 95 → 20–30 ms.
  2. **GLES: mapas de normal do palco à metade da resolução** (o nível 1 da cadeia de mips feita na CPU vira o nível 0):
     a alocação do array cai a 1/4 (e a memória de GPU do palco, ~240 MB a menos nos palcos grandes); o relevo à
     distância de jogo não muda.
  3. Carga do palco pelo **índice escolhido** (byte `0x8005FB56`, gravado na confirmação): quando ele muda, o asset
     começa a carregar ~6 s antes, na tela de carregamento. O índice 0 (234) é o valor do boot e o mesmo palco duas vezes
     não muda o byte: nesses casos vale a regra antiga (carga no primeiro desenho). O overlay do palco não serve de
     gatilho (só bate quando o ponteiro da partida já ligou).
  4. Threads de carga (personagens e palco) com `setpriority(10)` no Android: a rodada 2 teve picos isolados de 52–75 ms
     no 242, 236 e 241 com preparo ≤ 58 ms, que sumiram na rodada 3.
- `buffer_ms` ficou em 90: com as mudanças nenhum palco teve underrun fora da primeira partida, e subir o buffer atrasa a
  batida.

| palco | antes: jogo máx / preparo pers. / underruns | em passos: jogo máx / preparo palco | rodada 1: jogo / palco / underruns | rodada 2: idem | rodada 3 (carga em prioridade baixa): jogo / pers. / palco / underruns |
|---|---|---|---|---|---|
| 234 ponte de aço | 188 / 95 / 5 | 105 / 124 | 41 / 53 / 0 | 52 / 58 / 0 | 65 / 48 / 70 / 3 |
| 233 Sushi Planet | 60 / 48 / 0 | 48 / 52 | 38 / 41 / 0 | 38 / 42 / 0 | 34 / 22 / 36 / 0 |
| 242 parque | 126 / 71 / 3 | 85 / 104 | 36 / 37 / 0 | 62 / 39 / 0 | 39 / 29 / 41 / 0 |
| 243 fábrica | 49 / 30 / 0 | 48 / 14 | 40 / 41 / 0 | 47 / 38 / 0 | 38 / 9 / 42 / 0 |
| 232 templo de pedra | 46 / 26 / 0 | 37 / 38 | 35 / 45 / 0 | 37 / 39 / 0 | 38 / 11 / 41 / 0 |
| 237 cenário de papel | 52 / 23 / 0 | 44 / 55 | 36 / 40 / 0 | 38 / 41 / 0 | 35 / 17 / 41 / 0 |
| 244 templo oriental | 48 / 51 / 0 | 36 / 29 | 40 / 42 / 0 | 40 / 38 / 0 | 36 / 22 / 38 / 0 |
| 235 discoteca | 85 / 23 / 0 | 41 / 65 | 40 / 36 / 0 | 39 / 44 / 0 | 36 / 25 / 39 / 0 |
| 236 rua | 80 / 52 / 0 | 45 / 55 | 35 / 42 / 0 | 75 / 40 / 0 | 39 / 29 / 41 / 0 |
| 231 navio pirata | 39 / 27 / 0 | 35 / 19 | 41 / 38 / 0 | 36 / 40 / 0 | 35 / 12 / 39 / 0 |
| 238 estúdio de TV | 36 / 23 / 0 | 40 / 23 | 50 / 42 / 0 | 40 / 35 / 0 | 38 / 19 / 41 / 0 |
| 241 heliporto (ROBO-Z) | 44 / 28 / 0 | 35 / 28 | 35 / 34 / 0 | 63 / 41 / 0 | 38 / 11 / 37 / 0 |
| 245 estúdio "Dancing Heroes" | 39 / 28 / 0 | 36 / 14 | 42 / 44 / 0 | 46 / 41 / 0 | 36 / 11 / 39 / 0 |

(ms; "jogo" = maior intervalo entre vblanks na thread do jogo entre o menu e 28 s depois do X; "palco" e "pers." = pior
passo de preparo GL.) Na rodada 3, 12 dos 13 palcos ficaram em 34–39 ms, sem underrun; os intervalos de 25–35 ms que
sobram aparecem também nos menus (o ritmo da thread do jogo no tablet, §6), não vêm da carga.

**Exceção: a primeira partida da sessão.** É a única acima de 50 ms nas rodadas 2 e 3 (234: 52 e 65 ms, 0 e 3
underruns; 41 ms na rodada 1): os três intervalos caem no mesmo instante (15,6 s depois do X), junto com os passos mais
caros da sessão (programa do personagem 47 ms, do palco 70 ms, contra 20–40 ms nas partidas seguintes). É a primeira
compilação de shaders e a primeira alocação grande do processo; acontece uma vez por abertura do app. Aquecer isso nos
menus (programas compilados uma vez e divididos entre os objetos, ou `glProgramBinary` em disco) fica como próximo passo.

KITTY-N e KELLY (citadas no relato): no 2P a tela de palco começa sempre na posição 0 (234), qualquer que seja o 1P
(`BAG2_INTRO_P1=KITTY-N`/`KELLY` deram 234 nas duas), então os palcos delas não foram identificados por nome; estão entre
os 13 medidos (todos os palcos de partida do jogo).

### Ferramentas novas e o que fica
- `tools/tests/android_intro.py` (introdução palco por palco), `android_ab.py` (A/B pelo savestate),
  `android_profile.py` (simpleperf), `android_perf.py` com `heat_hitch` por janela, `stage_stats z`.
- A resolução dinâmica aparece ligada no log do tablet ("dynamic resolution on: 3x..8x", superfícies alocadas a 8x)
  mesmo com `dynamic_resolution = false` no `game.android.toml`: vem do preset (`quality_base = "ultra"`); nem
  `dynamic_resolution = false` no `settings.toml` nem `PSX_DYNRES=0` desligaram. Não pesou aqui (3x = 5x no ROBO-Z), mas
  vale olhar se a memória de GPU apertar.
- Próximo passo da introdução: aquecer shaders e a primeira alocação nos menus (a exceção da primeira partida).
