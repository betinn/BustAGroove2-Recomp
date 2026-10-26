cmake_minimum_required(VERSION 3.20)
# Projeto de jogo para o PSXRecomp. O framework NÃO é submódulo aqui: aponte
# -DPSXRECOMP_ROOT=C:/tools/psxrecomp (padrão abaixo).
set(PSXRECOMP_ROOT "C:/tools/psxrecomp" CACHE PATH "Path to psxrecomp framework checkout")
if(NOT EXISTS "${PSXRECOMP_ROOT}/runtime/runtime.cmake")
    message(FATAL_ERROR "PSXRecomp runtime missing: ${PSXRECOMP_ROOT}/runtime/runtime.cmake (set -DPSXRECOMP_ROOT)")
endif()

project(BustAGroove2Recomp LANGUAGES C CXX)

# O runtime e o C gerado sao C99/C++17. GCC 16 compila C++ como gnu++20 por padrao e ai
# path::u8string() vira std::u8string e gpu_hd_textures.cpp / duckstation_texture_pack.cpp
# nao compilam. runtime/CMakeLists.txt do framework e o RepsiMan fixam 17 do mesmo jeito.
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Sem launcher recomp-ui (submódulo não baixado), sem netplay, sem wizard.
set(PSX_RECOMP_UI OFF CACHE BOOL "" FORCE)
set(PSX_NETPLAY   OFF CACHE BOOL "" FORCE)
include("${PSXRECOMP_ROOT}/runtime/runtime.cmake")

psxrecomp_add_game_runtime(psx-runtime
    PRELOADED_MODS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/mods/preloaded"
    WINDOW_TITLE "Bust A Groove 2 Recompiled"
    DEFAULT_GAME_CONFIG_PATH "game.toml"
    MAX_PLAYERS 2
    # C gerado por psxrecomp-game --config game.toml
    GEN_MARKER    "generated/SLUS_011.59_dispatch.c"
    GEN_FULL_GLOB "generated/SLUS_011.59_full_*.c"
    # Overlays DBI_ compilados antecipadamente a partir do disco (tools/recomp/aot/overlays.json,
    # `aot_overlay_pipeline.py static`): sem isso o runtime interpreta/compila em tempo de execucao.
    GAME_OVERLAY_STATIC_C "${CMAKE_CURRENT_SOURCE_DIR}/generated/overlays_static.c"
)

# runtime.cmake deveria globar as unidades overlays_static_NNNN.c ao lado do dispatcher, mas nesta
# versao (31df2e1e) o glob nao as encontra no Windows; adiciona explicitamente.
file(GLOB _bag2_ov_parts CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/generated/overlays_static_*.c")
list(LENGTH _bag2_ov_parts _bag2_ov_n)
message(STATUS "BAG2: ${_bag2_ov_n} unidades de overlay estatico")
if(_bag2_ov_n GREATER 0)
    # runtime.cmake so define isto quando o seu proprio glob acha o dispatcher; sem a macro,
    # overlay_loader.c nao compila psx_overlay_static_code_matches e o link falha.
    target_compile_definitions(psx-runtime PRIVATE PSX_HAS_OVERLAY_DISPATCH=1)
    get_target_property(_bag2_srcs psx-runtime SOURCES)
    foreach(_p ${_bag2_ov_parts})
        list(FIND _bag2_srcs "${_p}" _idx)
        if(_idx EQUAL -1)
            target_sources(psx-runtime PRIVATE "${_p}")
        endif()
    endforeach()
endif()

# Plugins estaticos do jogo (widescreen 16:9), selecionados pelo pacote em mods/preloaded.
target_sources(psx-runtime PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src/bag2_mods.c")
# Remake do HEAT (prova de conceito): hook no desenho do personagem, pacote mods/preloaded/.../bag2.remake.heat.
target_sources(psx-runtime PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src/bag2_heat.c")

foreach(_cfg game.toml settings.toml)
    add_custom_command(TARGET psx-runtime POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/${_cfg}" "$<TARGET_FILE_DIR:psx-runtime>/${_cfg}"
        VERBATIM)
endforeach()
