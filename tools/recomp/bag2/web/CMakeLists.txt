cmake_minimum_required(VERSION 3.20)
# Alvo WASM do Bust A Groove 2 (fork kem0x/psxrecomp, branch compat/windows-web-stability).
# Modelo: C:\tools\RepsiMan\CMakeLists.txt. Configure SEMPRE com emcmake (ver tools/recomp/build-web.ps1):
#   emcmake cmake -S tools/recomp/bag2/web -B tools/recomp/bag2/build-web -G Ninja -DCMAKE_BUILD_TYPE=Release -DPSX_LAUNCHER=OFF -DPSX_DEBUG_TOOLS=OFF
project(BustAGroove2RecompWeb C CXX)

set(CMAKE_C_STANDARD 99)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(PSXRECOMP_ROOT "C:/tools/psxrecomp-web" CACHE PATH "Path to the kem0x/psxrecomp checkout (compat/windows-web-stability)")
if(NOT EXISTS "${PSXRECOMP_ROOT}/runtime/runtime.cmake")
    message(FATAL_ERROR "psxrecomp-web runtime missing: ${PSXRECOMP_ROOT}/runtime/runtime.cmake (set -DPSXRECOMP_ROOT)")
endif()
if(NOT EMSCRIPTEN)
    message(WARNING "This project is meant for Emscripten (emcmake). Native builds live in tools/recomp/bag2/CMakeLists.txt.")
endif()

# Sem launcher RmlUi (precisa de GL no thread do emulador) e sem debug TCP.
set(PSX_LAUNCHER OFF CACHE BOOL "" FORCE)
set(PSX_DEBUG_TOOLS OFF CACHE BOOL "" FORCE)
# BIOS_INTERPRETER: o BIOS (OpenBIOS) roda no interpretador MIPS, nao ha generated/SCPH1001_*.c.
set(PSXRECOMP_SKIP_BIOS_STALE_CHECK ON CACHE BOOL "" FORCE)
include("${PSXRECOMP_ROOT}/runtime/runtime.cmake")

# C gerado por C:\tools\psxrecomp-web\recompiler\build\psxrecomp-game.exe --config game-web.toml
set(BAG2_GEN_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../generated-web")
set(BAG2_GENERATED_FULL     "${BAG2_GEN_DIR}/SLUS_011.59_full.c")
set(BAG2_GENERATED_DISPATCH "${BAG2_GEN_DIR}/SLUS_011.59_dispatch.c")
foreach(_f ${BAG2_GENERATED_FULL} ${BAG2_GENERATED_DISPATCH})
    if(NOT EXISTS "${_f}")
        message(FATAL_ERROR "Generated C missing: ${_f} -- run psxrecomp-game --config game-web.toml first")
    endif()
endforeach()

if(EMSCRIPTEN)
    set(BAG2_DEFAULT_CONFIG "/game.toml")   # caminho dentro do MEMFS; a pagina escreve esse arquivo
else()
    set(BAG2_DEFAULT_CONFIG "game.toml")
endif()

psxrecomp_add_runtime_target(psx-runtime
    BIOS_INTERPRETER
    GAME_GENERATED_FULL_C     "${BAG2_GENERATED_FULL}"
    GAME_GENERATED_DISPATCH_C "${BAG2_GENERATED_DISPATCH}"
    # Overlays DBI_ compilados antecipadamente com o compile_overlays.py --static do fork
    # (entrada: static-inputs.json do pipeline AOT do upstream; ver notes/recomp.md).
    GAME_OVERLAY_STATIC_C     "${BAG2_GEN_DIR}/overlays_static.c"
    DEBUG_PORT 4472
    WINDOW_TITLE "Bust A Groove 2 Recompiled"
    DEFAULT_BIOS_PATH "bios/openbios.bin"
    DEFAULT_GAME_CONFIG_PATH "${BAG2_DEFAULT_CONFIG}"
    EXE_NAME "bag2"
)

if(EMSCRIPTEN)
    # WASM SIMD nos caminhos quentes (como no RepsiMan).
    set_source_files_properties(
        "${PSXRECOMP_ROOT}/runtime/src/mdec.c"
        "${PSXRECOMP_ROOT}/runtime/src/gpu_sw_renderer.c"
        "${PSXRECOMP_ROOT}/runtime/src/main.cpp"
        PROPERTIES COMPILE_OPTIONS "-msimd128")
    target_link_options(psx-runtime PRIVATE "-msimd128")

    # Saida: bag2.js + bag2.wasm (sem shell HTML; a pagina Vite web/recomp.html define `Module`
    # e injeta bag2.js). INVOKE_RUN=0: main() so roda quando a pagina chama Module.callMain(args).
    set_target_properties(psx-runtime PROPERTIES SUFFIX ".js")
    target_link_options(psx-runtime PRIVATE
        "-sINVOKE_RUN=0"
        "-sEXPORTED_RUNTIME_METHODS=FS,callMain,ENV"
        "-sENVIRONMENT=web,worker")
    # -DBAG2_WEB_DEBUG=ON: asserts do Emscripten + nomes de funcao no .wasm (stack trace legivel
    # num abort()). So muda o link; nao recompila o C gerado.
    option(BAG2_WEB_DEBUG "Emscripten ASSERTIONS + function names for readable abort traces" OFF)
    if(BAG2_WEB_DEBUG)
        target_link_options(psx-runtime PRIVATE "-sASSERTIONS=1" "--profiling-funcs")
    endif()

    # OpenBIOS (MIT) ao lado da saida para a pagina buscar.
    add_custom_command(TARGET psx-runtime POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E make_directory "$<TARGET_FILE_DIR:psx-runtime>/bios"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "C:/tools/psxrecomp/bios/openbios.bin" "$<TARGET_FILE_DIR:psx-runtime>/bios/openbios.bin"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "C:/tools/psxrecomp/bios/OpenBIOS.LICENSE" "$<TARGET_FILE_DIR:psx-runtime>/bios/OpenBIOS.LICENSE"
        VERBATIM)
endif()
