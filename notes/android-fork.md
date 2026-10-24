# Fork Android: Polygonia (DuckStation GPLv3)

Objetivo: jogar o Bust A Groove 2 no celular Android com app próprio, publicado na Google Play em
teste fechado (só o dono instala). Portar o PSXRecomp para Android foi descartado (custo); o caminho é
um fork do DuckStation na última versão sob GPLv3, rebatizado **Polygonia**, com código público.

## 1. Base do fork (licença)

- O DuckStation trocou a licença GPLv3 -> CC-BY-NC-ND 4.0 em **2024-09-01**, commit
  `7f4e5d55dbdef5a50e0aa4994f667fb03d854928` ("Misc: Update copyright headers"). Versões a partir daí
  não podem ser modificadas e redistribuídas.
- Mas o app Android saiu do repositório público bem antes: commit
  `6e49adb508c009c5f7998a38d47e1d0d174990d0` ("Remove Android app", **2021-06-08**). Depois disso o
  Android do DuckStation é fechado.
- Portanto a base é o **último commit com `android/` e GPLv3**:
  `81da9be2d1040665ebfaf2db6d7fdb710a48a383` (2021-06-07, "Android: Add quick load/save touchscreen
  buttons"), `git describe` = `0.1-4195-g81da9be2d`. `LICENSE` nesse commit = GNU GPL v3.
- Dependências vendoradas em `dep/`; o único submódulo (`dep/msvc/qt`) é o Qt do Windows, irrelevante
  para Android.
- Pasta local: `C:\Users\sem-m\source\GIT\Polygonia` (branch `main` = histórico do upstream até
  `81da9be` + commits do fork; remote `upstream` = stenzek/duckstation).

## 2. BIOS

O DuckStation de 2021 exige BIOS. Aceita imagem desconhecida de 512 KiB como fallback ("Using unknown
BIOS"). O **OpenBIOS** do PCSX-Redux (MIT, redistribuível) já é o BIOS do port de PC
(`tools/recomp/bag2/build/bios/openbios.bin`, 524288 bytes) e pode ir embutido no APK. Nenhum BIOS da
Sony entra no APK nem no git.

## 3. Toolchain exigida pelo `android/` de 2021

AGP 4.2.1, Gradle 7.0.2 (wrapper), compileSdk/targetSdk 29, build-tools 30.0.2, NDK 21.4.7075529,
CMake 3.10.2 (do SDK), JDK 11. ABIs: arm64-v8a, armeabi-v7a, x86_64.

Instalado nesta máquina (sem Android Studio):
- JDK 11: Temurin via winget, `C:\Program Files\Eclipse Adoptium\jdk-11.0.32.101-hotspot`.
- Android SDK: `C:\tools\android-sdk` (command-line tools `cmdline-tools/latest`, platform-tools/adb,
  platforms;android-29, build-tools;30.0.2, ndk;21.4.7075529, cmake;3.10.2.4988404). O `sdkmanager`
  atual roda com o JDK 21 do sistema.
- `android/local.properties`: `sdk.dir=C:/tools/android-sdk` (barras normais; `\t` em properties vira
  tabulação e quebra a build com "A sintaxe do nome do arquivo ... está incorreta").

## 4. Primeira execução no tablet e o travamento do OpenBIOS

Aparelho de teste: Samsung Galaxy Tab S10 Ultra (SM-X920), Dimensity 9300+ (MT6989), GPU Mali-G720
Immortalis MC12, Android 16, tela 1848x2960. `adb install` falha com `INSTALL_FAILED_VERIFICATION_FAILURE`
enquanto o Play Protect está ligado; desligar só durante os testes. APK de debug precisa de `adb install -t`.

Disco em `/sdcard/Polygonia/games/`, OpenBIOS em `/sdcard/duckstation/bios/openbios.bin` (pasta de usuário
do app original). Boot direto sem UI: `am start -n <pkg>/.EmulationActivity --es bootPath "<cue>" --ez
resumeState false`; configurações via `shared_prefs/<pkg>_preferences.xml` (chaves iguais ao INI do PC:
`GPU/Renderer`, `GPU/ResolutionScale`, `GPU/PGXPEnable`, `GPU/WidescreenHack`, `Display/AspectRatio`,
`GPU/TextureFilter`, `Controller1/TouchscreenControllerView`, `Audio/BufferSize`...).

Sintoma: tela preta, VPS 60, FPS 0, BIOS lia dois setores do disco (PVD e diretório raiz) e parava.
Diagnóstico com `Logging/LogToFile=true` (logcat descarta linhas; o arquivo `duckstation.log` não) e
promovendo os `Log_DebugPrintf` do `cdrom.cpp` a Dev (compilados fora em Release): o CD-ROM estava
correto (INT3/INT2 certos, DMA de 2048 bytes feito). O que travava era o **shell do OpenBIOS em RAM
(0x8003xxxx)**, que espera o bit 13 do GPUSTAT (campo entrelaçado) alternar; o DuckStation de 2021
devolvia o bit invertido. Correção: commit upstream `13c5ee8bf` (2021-09-29, "GPU: Fix incorrect value for
status bit 13 — Fixes openbios boot animation", GPLv3), uma linha em `src/core/gpu.cpp`. Outros commits
upstream sobre OpenBIOS (`fe41e9257` 2023, `34861074c` 2024, spam de Init) não foram necessários.

Também corrigido `src/scmversion/CMakeLists.txt` (`WIN32` -> `CMAKE_HOST_WIN32`): cross-compilando do
Windows o `.sh` não roda e o `scmversion.cpp` nunca nascia.

Resultado: BAG2 boota, FMV de abertura a 15 fps (taxa do vídeo) com VPS 60.

## 5. Fase 4: rebatizar e simplificar (Polygonia)

- Pacote Java movido para `io.github.betinn.polygonia` (script de renomeação mecânica em todos os
  `.java/.xml/.gradle/.cpp/.h`; JNI `Java_io_github_betinn_polygonia_*`; biblioteca
  `libpolygonia-native.so`). Cuidado: `src/common/file_system.cpp` do core também tem assinaturas
  JNI com o caminho da classe `FileHelper` — esquecer isso dá `NoSuchMethodError` no `create`.
- `AndroidHostInterface.createInstance`: pasta de usuário = `getExternalFilesDir(null)` e cópia do
  `assets/bios/openbios.bin` (MIT, com `OpenBIOS.LICENSE`) na primeira execução. `.gitignore` do
  fork ignora `*.bin/*.cue/...` com exceção explícita para esse arquivo.
- Manifest: só `VIBRATE`; sem `INTERNET`, sem permissões de armazenamento, sem
  `requestLegacyExternalStorage`; `EmulationActivity` com `screenOrientation="sensorLandscape"` e
  `resizeableActivity="false"` (o One UI abria o app em janela pop-up e ignorava a orientação).
- `MainActivity`: `applyPolygoniaDefaults()` grava os padrões na primeira execução
  (`Polygonia/DefaultsApplied`); `maybeAutoBoot()` depois do refresh da lista: jogo salvo em
  `Polygonia/BootGamePath` -> inicia; lista com 1 jogo -> salva e inicia; lista vazia -> abre o
  seletor de pasta (SAF). `startEmulation` salva o caminho escolhido. Diálogo de "update notes"
  removido.
- Ícone: gema hexagonal low-poly gerada por `make_icon.py` (PIL) nos 5 mipmaps + adaptive.
- Teste: APK de debug precisa de `adb install -t`; o Android 16 mostra aviso de página de 16 KB
  para apps depuráveis (NDK r21 não alinha a 16 KB; resolve com NDK r28 na modernização).

## 6. Google Play: alvo de API e build moderna

Desde 2026-08-31, apps novos e atualizações precisam de **targetSdk 36** (Android 16) para serem
enviados à Play, e bibliotecas nativas alinhadas a **16 KB**. Feito (commit "Moderniza a build Android"):
- AGP 8.7.3 + Gradle 8.9 (JDK 17+; o JDK 21 da Microsoft em `C:\Program Files\Microsoft\jdk-21...`
  serve como `JAVA_HOME`), compileSdk/targetSdk 36, minSdk 26, NDK `28.2.13676358`, CMake 3.22.1,
  só `arm64-v8a`, `jniLibs.useLegacyPackaging=false`.
- SDK extra em `C:\tools\android-sdk`: `platforms;android-36`, `build-tools;36.0.0`, `ndk;28.2.13676358`,
  `cmake;3.22.1`.
- Tropeços: `android:exported` obrigatório na `MainActivity`; `cubeb_aaudio.cpp` usa `decltype` de
  símbolos AAudio acima do minSdk e o NDK 26+ transforma isso em erro ("is unavailable: introduced in
  Android 28") -> `add_compile_definitions(__ANDROID_UNAVAILABLE_SYMBOLS_ARE_WEAK__)` no `CMakeLists.txt`
  (o cubeb faz `dlopen` em tempo de execução). O núcleo de 2021 compilou sem outras mudanças no clang 19.
- Resultado no tablet: sem aviso de 16 KB, boot direto no jogo salvo.
- Assinatura: keystore `C:\Users\sem-m\Polygonia-keys\polygonia-release.jks` (alias `polygonia`, RSA
  4096, 30 anos) + `keystore.properties` com a senha gerada aleatoriamente; `LEIA-ME.txt` lá pede
  backup. `app/build.gradle` lê o caminho de `POLYGONIA_KEYSTORE_PROPERTIES` (padrão = essa pasta).
- Roteiro da Play e textos da ficha: `Polygonia/docs/play-store.md`; política de privacidade:
  `Polygonia/docs/privacidade.md`.

## 7. Repositórios e artefatos (2026-10-24)

- Fork: <https://github.com/betinn/Polygonia> (público, GPLv3, histórico completo do upstream até
  `81da9be` + commits do fork; remote `upstream` = stenzek/duckstation). Tag `v0.1.0` com release e o
  APK assinado anexado.
- PC: <https://github.com/betinn/BustAGroove2-Recomp> (público; pasta local renomeada para
  `BustAGroove2-Recomp` em 2026-10-24). `git ls-files` auditado: 77 arquivos, 583 KB, nenhum dado de jogo.
- Artefatos: `C:\Users\sem-m\Polygonia-keys\artefatos\Polygonia-0.1-4197.{aab,apk}`
  (`versionCode` 4041 = nº de commits, `versionName` `0.1-4197`, assinatura SHA-256
  `717e5c32...f910db`). A release assinada foi instalada limpa no tablet e bootou o BAG2.
- Pendências: fazer o teste fechado na Play (roteiro em `docs/play-store.md`); medir uma partida
  inteira na build release com OSD ligado (`Display/ShowFPS`) para confirmar 60 fps sem engasgo com
  buffer 2048; o One UI abre apps lançados via `adb` em janela pop-up (pelo launcher abre normal);
  xBR completo indisponível no Mali/Vulkan (testar renderer OpenGL se quiser o filtro cheio).
