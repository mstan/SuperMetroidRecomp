cmake_minimum_required(VERSION 3.16)
project(SuperMetroidSNESRecomp C)

# Cross-platform (macOS/Linux/MinGW) build for SuperMetroidSNESRecomp.
#
# Mirrors the shared framework source list (snesrecomp/runner/runner.cmake)
# plus the Super Metroid-specific runtime in src/. Prerequisites:
#   - a snesrecomp checkout at ./snesrecomp (junction/symlink to the sibling repo)
#   - generated C in src/gen/ and recomp/funcs.h:
#       ./tools/regen.sh --strict-idempotent
#   - SDL2 + OpenGL

set(CMAKE_C_STANDARD 11)

set(SNESRECOMP_ROOT ${CMAKE_SOURCE_DIR}/snesrecomp)
include(${SNESRECOMP_ROOT}/runner/runner.cmake)

# Launcher UI backend: the SHARED recomp-ui launcher (F:\Projects\recomp-ui,
# consumed as the ./recomp-ui junction/submodule) so every recomp game drives
# ONE launcher repo. The former in-tree launcher_ng has been retired from the
# engine in favour of this shared, console-agnostic launcher.
include(${CMAKE_SOURCE_DIR}/recomp-ui/recomp_ui.cmake)

find_package(SDL2 REQUIRED)
find_package(OpenGL REQUIRED)
find_package(Python3 COMPONENTS Interpreter REQUIRED)

# Generated recompiler output (produced by tools/regen.sh; not committed).
file(GLOB SM_GEN_SOURCES CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/src/gen/*.c)
if(NOT SM_GEN_SOURCES)
    message(FATAL_ERROR
        "src/gen/ is empty — run the regen (see header) with a verified "
        "Super Metroid ROM at the repo root before building.")
endif()

# Generated sources are regenerated from the ROM and remain untracked. Apply
# the opt-in, runtime-gated enemy range branches before any bank is compiled so
# regen cannot silently discard widescreen spawning/culling support.
add_custom_target(sm_widescreen_overrides ALL
    COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_SOURCE_DIR}/tools/apply_widescreen_overrides.py
            --check --gen-dir ${CMAKE_SOURCE_DIR}/src/gen
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Applying Super Metroid widescreen enemy range overrides"
    VERBATIM
)

add_executable(SuperMetroidSNESRecomp
    ${SNESRECOMP_RUNNER_SOURCES}
    # game-specific runtime
    src/config.c
    src/glsl_shader.c
    src/main.c
    src/post_mortem.c
    src/opengl.c
    src/gen_stubs.c
    src/sm_cpu_infra.c
    src/sm_rtl.c
    src/sm_spc_player.c
    src/sm_widescreen.c
    src/fiber_compat.c    # Win32 Fiber API shim (ucontext) for non-Windows

    # OpenGL function loader
    third_party/gl_core/gl_core_3_1.c
    # recompiled banks (generated)
    ${SM_GEN_SOURCES}
)

add_dependencies(SuperMetroidSNESRecomp sm_widescreen_overrides)

target_include_directories(SuperMetroidSNESRecomp PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/recomp
    ${SNESRECOMP_RUNNER_INCLUDE_DIRS}
    ${SDL2_INCLUDE_DIRS}
)

# Release version stamp: configure with -DSNESRECOMP_BUILD_VERSION=<ver>
# (the CMake equivalent of the MSVC games' /p:SnesRecompBuildVersion); it
# lands in the post-mortem report's "build" section via the
# SNESRECOMP_BUILD_VERSION define. Unset (local builds) the define is
# omitted and main.c defaults to "dev".
set(SNESRECOMP_BUILD_VERSION "" CACHE STRING
    "Release version stamp baked into crash reports (empty = dev)")
if(SNESRECOMP_BUILD_VERSION)
    target_compile_definitions(SuperMetroidSNESRecomp PRIVATE
        SNESRECOMP_BUILD_VERSION="${SNESRECOMP_BUILD_VERSION}")
endif()

target_compile_definitions(SuperMetroidSNESRecomp PRIVATE
    SMW_DISABLE_LM=1
    SYSTEM_VOLUME_MIXER_AVAILABLE=0   # Win32 WASAPI volume control excluded
    SNESRECOMP_TRACE=$<BOOL:${SNESRECOMP_ENABLE_TRACE}>
    SNESRECOMP_REVERSE_DEBUG=0        # raw WRAM stores; matches non-debug generated C
)

# The recompiled C is machine-generated; quiet its warnings and tolerate the
# K&R-style implicit declarations clang/gcc reject by default.
if(NOT MSVC)
    target_compile_options(SuperMetroidSNESRecomp PRIVATE
        -w -Wno-implicit-function-declaration)
endif()

target_link_libraries(SuperMetroidSNESRecomp PRIVATE
    ${SNESRECOMP_RUNNER_LIBRARIES}
    SDL2::SDL2          # SDL2 only (no SDL2main); main.c defines SDL_MAIN_HANDLED
    OpenGL::GL
)

# post_mortem.c's crash backtrace uses the Win32 DbgHelp API (StackWalk64,
# SymFromAddr, ...) on Windows. The MSVC .vcxproj links it via #pragma
# comment(lib); the CMake/MinGW path needs it on the link line explicitly.
if(WIN32)
    target_link_libraries(SuperMetroidSNESRecomp PRIVATE dbghelp)
endif()

# Dear ImGui pre-boot launcher (GUI) from the shared recomp-ui repo. One call
# wires sources/includes/RECOMP_LAUNCHER define/CXX/asset-staging (same SM
# boxart; recomp-ui's default pad.tga is already the SNES pad).
recomp_target_launcher_ui(SuperMetroidSNESRecomp
    BOXART ${CMAKE_SOURCE_DIR}/recomp/launcher/boxart.tga)
