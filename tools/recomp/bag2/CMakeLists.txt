cmake_minimum_required(VERSION 3.20)
# Projeto de jogo para o PSXRecomp. O framework NÃO é submódulo aqui: aponte
# -DPSXRECOMP_ROOT=C:/tools/psxrecomp (padrão abaixo).
set(PSXRECOMP_ROOT "C:/tools/psxrecomp" CACHE PATH "Path to psxrecomp framework checkout")
if(NOT EXISTS "${PSXRECOMP_ROOT}/runtime/runtime.cmake")
    message(FATAL_ERROR "PSXRecomp runtime missing: ${PSXRECOMP_ROOT}/runtime/runtime.cmake (set -DPSXRECOMP_ROOT)")
endif()

project(BustAGroove2Recomp LANGUAGES C CXX)

# Sem launcher recomp-ui (submódulo não baixado), sem netplay, sem wizard.
set(PSX_RECOMP_UI OFF CACHE BOOL "" FORCE)
set(PSX_NETPLAY   OFF CACHE BOOL "" FORCE)
include("${PSXRECOMP_ROOT}/runtime/runtime.cmake")

psxrecomp_add_game_runtime(psx-runtime
    PRELOADED_MODS_DIR NONE
    WINDOW_TITLE "Bust A Groove 2 Recompiled"
    DEFAULT_GAME_CONFIG_PATH "game.toml"
    MAX_PLAYERS 2
    # C gerado por psxrecomp-game --config game.toml
    GEN_MARKER    "generated/SLUS_011.59_dispatch.c"
    GEN_FULL_GLOB "generated/SLUS_011.59_full_*.c"
)

foreach(_cfg game.toml)
    add_custom_command(TARGET psx-runtime POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/${_cfg}" "$<TARGET_FILE_DIR:psx-runtime>/${_cfg}"
        VERBATIM)
endforeach()
