# Ambiente da build Android (bash do Git for Windows). Fonte: . recomp/android/env.sh
# Git Bash converte /sdcard/... em C:/Program Files/Git/sdcard/...; a função adb desliga a conversão só para ele
# (o gradlew precisa dela ligada para o -classpath).
adb() { MSYS_NO_PATHCONV=1 command adb "$@"; }
export BAG2_TOOLS="${BAG2_TOOLS:-/c/tools}"
export PSXRECOMP_ROOT="$BAG2_TOOLS/psxrecomp"
export ANDROID_SDK_ROOT="$BAG2_TOOLS/android-sdk"
export ANDROID_NDK_HOME="$ANDROID_SDK_ROOT/ndk/28.2.13676358"
export JAVA_HOME="$BAG2_TOOLS/jdk-21.0.12.1+1"
export PATH="$APPDATA/Python/Python314/Scripts:$JAVA_HOME/bin:$ANDROID_SDK_ROOT/platform-tools:$PATH"
# Fontes das dependências já baixadas pela build do PC (sem novo download).
REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
export PSX_SDL3_SOURCE_DIR="$REPO/game/build/_deps/sdl3-src"
export PSX_ZLIB_SOURCE_DIR="$REPO/game/build/_deps/psx_zlib-src"
export PSX_LIBCHDR_SOURCE_DIR="$REPO/game/build/_deps/psx_libchdr-src"
export PSX_LIBWEBP_SOURCE_DIR="$REPO/game/build/_deps/psx_libwebp-src"
