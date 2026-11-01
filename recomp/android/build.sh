#!/usr/bin/env bash
# Build do APK Android do Bust A Groove 2 Recompiled (docs/android-apk.md). Git Bash, da raiz do repositório:
#   bash recomp/android/build.sh [passos...]
# Passos (padrão: native stage apk install push run):
#   configure  cmake do projeto do jogo com a toolchain do NDK em game/build-android (uma vez; repetir se
#              mudar flags). Variáveis: BAG2_ANDROID_DEBUG_TOOLS=ON|OFF (servidor TCP 4370; padrão ON).
#   native     compila libBust_A_Groove_2_Recompiled.so e copia com strip para android/app/src/main/jniLibs
#   stage      python recomp/android/stage.py $BAG2_STAGE_MODE (external | full; padrão external)
#   apk        gradlew assembleRelease (assinado com C:/Users/sem-m/BAG2-keys)
#   install    adb install -r do APK
#   push       adb push da árvore game/build-android/stage para a pasta de arquivos do app (modo external)
#   run        abre o app (am start) e acompanha o logcat do psxrecomp
#   log        só o logcat
#   stop       força a parada do app
set -euo pipefail
cd "$(dirname "$0")/../.."
. recomp/android/env.sh
PKG=io.github.betinn.bag2
FILES=/sdcard/Android/data/$PKG/files
NDK_BIN="$ANDROID_NDK_HOME/toolchains/llvm/prebuilt/windows-x86_64/bin"
APK=android/app/build/outputs/apk/release/app-release.apk
STEPS=("$@"); [ ${#STEPS[@]} -eq 0 ] && STEPS=(native stage apk install push run)
: "${BAG2_STAGE_MODE:=external}"
: "${BAG2_ANDROID_DEBUG_TOOLS:=ON}"

for step in "${STEPS[@]}"; do
  echo "== $step"
  case "$step" in
    configure)
      cmake -S game -B game/build-android -G Ninja \
        -DCMAKE_TOOLCHAIN_FILE="$ANDROID_NDK_HOME/build/cmake/android.toolchain.cmake" \
        -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-29 -DANDROID_STL=c++_static \
        -DCMAKE_BUILD_TYPE=Release -DPSXRECOMP_ROOT="$PSXRECOMP_ROOT" \
        -DPSX_RECOMP_UI=OFF -DPSX_ENABLE_VULKAN=OFF -DPSX_DEBUG_TOOLS="$BAG2_ANDROID_DEBUG_TOOLS" ;;
    native)
      [ -f game/build-android/build.ninja ] || bash "$0" configure
      cmake --build game/build-android --target psx-runtime
      mkdir -p android/app/src/main/jniLibs/arm64-v8a
      "$NDK_BIN/llvm-strip.exe" --strip-unneeded -o android/app/src/main/jniLibs/arm64-v8a/libBust_A_Groove_2_Recompiled.so \
        game/build-android/libBust_A_Groove_2_Recompiled.so
      ls -la android/app/src/main/jniLibs/arm64-v8a/ ;;
    stage)
      python recomp/android/stage.py "$BAG2_STAGE_MODE" ;;
    apk)
      (cd android && ./gradlew --no-daemon -q assembleRelease)
      ls -la "$APK" ;;
    install)
      adb install -r "$APK" ;;
    push)
      adb shell mkdir -p "$FILES"
      # adb push de uma pasta cria <dest>/<nome>; empurra o conteúdo item a item
      for item in game/build-android/stage/* game/build-android/stage/.[!.]*; do
        [ -e "$item" ] || continue
        adb push --sync "$item" "$FILES/"
      done ;;
    run)
      adb logcat -c || true
      adb shell am start -n "$PKG/.SetupActivity"
      adb logcat -v time -s psxrecomp:V SDL:V AndroidRuntime:E DEBUG:I libc:F ;;
    log)
      adb logcat -v time -s psxrecomp:V SDL:V AndroidRuntime:E DEBUG:I libc:F ;;
    stop)
      adb shell am force-stop "$PKG" ;;
    *) echo "passo desconhecido: $step"; exit 2 ;;
  esac
done
