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

## 6. Google Play: alvo de API

Desde 2026-08-31, apps novos e atualizações precisam de **targetSdk 36** (Android 16) para serem
enviados à Play. O fork de 2021 mira 29, então a versão da loja exige modernizar a build (AGP 8, JDK 17,
armazenamento por SAF/diretório do app em vez de `/sdcard/duckstation`, edge-to-edge, bibliotecas
nativas alinhadas a 16 KB).
