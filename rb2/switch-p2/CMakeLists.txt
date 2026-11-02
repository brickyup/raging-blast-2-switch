# Dragon Ball: Raging Blast 2 (Xbox 360, Europe, 4E4D0826) for Nintendo Switch with the native p2 renderer.
# Project layout from the Silent Hill: Downpour Switch port (fungalspores/silent-hill-downpour-switch).
# SDK: masseffect-nx 46e6dbf + prototype-switch SDK patch (Horizon layer by StevensND / NebadasSwifty),
# renderer: prototype2-nx native renderer (nfsmw-nx -> project8-sw -> Mercs2-NX -> Prototype 2). GPL-3.0-or-later.
# Driven by build_switch_p2.sh.
cmake_minimum_required(VERSION 3.25)
project(rb2 VERSION 0.2.0 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(REXSDK_DIR "" CACHE PATH "ReXGlue SDK source (masseffect-nx Horizon fork + P2 patch)")
if(NOT REXSDK_DIR)
    message(FATAL_ERROR "Set REXSDK_DIR")
endif()
# The presenter's FSR 1 path; gates members of rex/ui/presenter.h, so it is set for every target.
add_compile_definitions(REX_HAS_FIDELITYFX_SDK=1)
add_subdirectory("${REXSDK_DIR}" rexglue-sdk)
# Release: basic FPS integration only, no SDK profiler polling the clock and temperature services.
set_source_files_properties("${REXSDK_DIR}/src/ui/switch_perf.cpp" DIRECTORY "${REXSDK_DIR}"
    PROPERTIES HEADER_FILE_ONLY TRUE)
target_sources(rexglue_switch_startup PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/switch_release_perf.cpp")
set_source_files_properties("${REXSDK_DIR}/src/ui/vulkan/vulkan_presenter.cpp" DIRECTORY "${REXSDK_DIR}/src/ui"
    PROPERTIES COMPILE_DEFINITIONS "REX_SWITCH_RELEASE_DIAGNOSTICS=1")

set(GEN "${CMAKE_CURRENT_SOURCE_DIR}/generated/default")
if(NOT EXISTS "${GEN}/sources.cmake")
    message(FATAL_ERROR "Run rexglue codegen rb2_manifest.toml first")
endif()
include("${GEN}/sources.cmake")
set(RB2_GENERATED "")
foreach(src ${GENERATED_SOURCES})
    if(IS_ABSOLUTE "${src}")
        list(APPEND RB2_GENERATED "${src}")
    else()
        list(APPEND RB2_GENERATED "${CMAKE_CURRENT_SOURCE_DIR}/${src}")
    endif()
endforeach()
add_library(rb2_recomp OBJECT ${RB2_GENERATED})
target_include_directories(rb2_recomp PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${CMAKE_CURRENT_SOURCE_DIR}/../src" "${GEN}")
target_link_libraries(rb2_recomp PRIVATE rex::runtime)
rexglue_apply_target_settings(rb2_recomp)
target_precompile_headers(rb2_recomp PRIVATE "${GEN}/rb2_pch.h")
target_compile_options(rb2_recomp PRIVATE -fno-tree-slp-vectorize -fno-tree-vectorize -fno-math-errno -fno-trapping-math)

file(GLOB RB2_APP_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp" "${CMAKE_CURRENT_SOURCE_DIR}/src/native_p2/*.cpp")
add_executable(rb2 ${RB2_APP_SOURCES})
target_include_directories(rb2 PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${CMAKE_CURRENT_SOURCE_DIR}/../src" "${GEN}" "${CMAKE_CURRENT_SOURCE_DIR}/src/native_p2"
    "${REXSDK_DIR}/src/core" "${REXSDK_DIR}/src/graphics" "${REXSDK_DIR}/src/ui/shaders"
    "${REXSDK_DIR}/thirdparty/glslang" "${REXSDK_DIR}/thirdparty/xxHash")
target_link_libraries(rb2 PRIVATE rb2_recomp rex::runtime)
rexglue_configure_target(rb2 GPU_PLUGINS xenos)

# CPU-only SDK sources the native renderer reuses (draw extent estimation, shader microcode interpretation).
target_sources(rb2 PRIVATE
    "${REXSDK_DIR}/src/graphics/register_file.cpp"
    "${REXSDK_DIR}/src/graphics/format/ucode.cpp"
    "${REXSDK_DIR}/src/graphics/pipeline/shader/interpreter.cpp")

set_property(TARGET rb2 APPEND PROPERTY LINK_DEPENDS "${REXGLUE_SWITCH_NVK_SDK}/lib/libvulkan.a")
set(RB2_SWITCH_HEAP_MB 2048 CACHE STRING "Capped Switch heap size in MiB")
set_source_files_properties("${REXSDK_DIR}/src/ui/runtime_switch.cpp" DIRECTORY "${REXSDK_DIR}"
    PROPERTIES COMPILE_DEFINITIONS "REX_SWITCH_HEAP_MB=${RB2_SWITCH_HEAP_MB}")
set(RB2_SWITCH_HOST_STACK_MB 8 CACHE STRING "Host stack budget per Xbox thread in MiB")
set_source_files_properties("${REXSDK_DIR}/src/system/xthread.cpp" DIRECTORY "${REXSDK_DIR}/src/system"
    PROPERTIES COMPILE_DEFINITIONS "REX_SWITCH_HOST_STACK_MB=${RB2_SWITCH_HOST_STACK_MB}")
set_source_files_properties("${GEN}/rb2_register.cpp"
    PROPERTIES COMPILE_OPTIONS "-O0" SKIP_PRECOMPILE_HEADERS ON)
# GCC fuses separate PPC fmul/fadd into FMA by default: results would drift from the Xbox 360.
foreach(t rb2 rb2_recomp)
    target_compile_options(${t} PRIVATE -ffp-contract=off -fno-strict-aliasing)
endforeach()
# icon.jpg is optional and not in the repository (256x256 JPEG); without it the NRO gets the default libnx icon.
set(RB2_ICON "")
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/icon.jpg")
    set(RB2_ICON ICON "${CMAKE_CURRENT_SOURCE_DIR}/icon.jpg")
endif()
rexglue_switch_add_nro(rb2 NAME "DB Raging Blast 2" AUTHOR "rb2-switch-port" VERSION "0.2.0" ${RB2_ICON})

# Guest code optimized across translation units (after Prototype 2 NX: needs prepare_direct_calls.py, which keeps the
# hooked functions behind their weak aliases).
option(RB2_GUEST_LTO "Optimize generated guest code across translation units" ON)
if(RB2_GUEST_LTO)
    target_compile_options(rb2_recomp PRIVATE -flto -fno-fat-lto-objects -frounding-math)
    target_link_options(rb2 PRIVATE -flto=2 -flto-partition=balanced -frounding-math -ffp-contract=off -fno-strict-aliasing)
endif()

# Profile-guided optimization (after prototype-switch, WORKLOG #126/#127). GEN: instrumented build, the game writes
# /rb2-pgo/*.gcda on the SD card (src/rb2_pgo.cpp); USE: optimized with them (copied to RB2_PGO_DATA). Both stages
# must use the same build directory (-fprofile-prefix-path strips it from the file names).
set(RB2_PGO "OFF" CACHE STRING "PGO stage: OFF, GEN or USE")
set(RB2_PGO_DATA "${CMAKE_CURRENT_SOURCE_DIR}/pgo-data" CACHE STRING "Directory with the .gcda files for RB2_PGO=USE")
if(RB2_PGO STREQUAL "GEN")
    # Indirect-call value profiling uses a thread-local libgcov variable that is null on guest threads: arcs only.
    foreach(t rb2_recomp rb2)
        target_compile_options(${t} PRIVATE -fprofile-generate=/rb2-pgo -fno-profile-values -fprofile-update=single
          "-fprofile-prefix-path=${CMAKE_BINARY_DIR}")
    endforeach()
    target_link_options(rb2 PRIVATE -fprofile-generate=/rb2-pgo)
    target_compile_definitions(rb2 PRIVATE RB2_PGO_GEN=1)
elseif(RB2_PGO STREQUAL "USE")
    foreach(t rb2_recomp rb2)
        target_compile_options(${t} PRIVATE "-fprofile-use=${RB2_PGO_DATA}" -fprofile-partial-training -fprofile-correction
          "-fprofile-prefix-path=${CMAKE_BINARY_DIR}" -Wno-missing-profile -Wno-coverage-mismatch)
    endforeach()
    target_link_options(rb2 PRIVATE "-fprofile-use=${RB2_PGO_DATA}" -fprofile-partial-training)
endif()
