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

# CACHE PATH, so -DSNESRECOMP_ROOT=<engine checkout> works -- the framework
# README documents that flag as the way to build a game against an engine
# worktree. A plain set() here created a normal variable that SHADOWED the
# cache entry, so the override was accepted and then silently ignored: the
# build linked this repo's pinned snesrecomp against C emitted by a DIFFERENT
# engine, which surfaces as undefined runner symbols at link time rather than
# as a clear "wrong engine" error. SMW and MMX already use this form.
set(SNESRECOMP_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/snesrecomp" CACHE PATH
    "Path to the snesrecomp checkout used by this game")

find_package(Python3 COMPONENTS Interpreter REQUIRED)

if(NOT ANDROID)
    include(CTest)
endif()
# Android uses the SDL2 source tree fetched by android/fetch_sdl.sh. Desktop
# keeps the framework default (currently SDL3).
if(ANDROID)
    set(SNESRECOMP_SDL_BACKEND "SDL2" CACHE STRING
        "SDL backend (SDL3 or SDL2)" FORCE)
endif()

include(${SNESRECOMP_ROOT}/runner/runner.cmake)

# After runner.cmake, so these targets can use ${SNESRECOMP_RUNNER_INCLUDE_DIRS}
# rather than naming runner/src subdirectories themselves -- the runner's
# sources live in layer folders (cpu/, debug/, desktop/, ...) and that
# variable is the framework's contract for finding their headers.
if(BUILD_TESTING AND NOT ANDROID)
    add_executable(sm_display_test tests/sm_display_test.c src/sm_display.c)
    target_include_directories(sm_display_test PRIVATE
        src
        ${SNESRECOMP_RUNNER_INCLUDE_DIRS})
    add_test(NAME sm_display_geometry COMMAND sm_display_test)

    # The overlay compositing step (framework helper, this port's only
    # consumer of it so far). See the test's header for why it is worth one.
    add_executable(sm_overlay_blit_test
        tests/sm_overlay_blit_test.c
        ${SNESRECOMP_ROOT}/runner/src/snes_overlay_draw.c)
    target_include_directories(sm_overlay_blit_test PRIVATE
        ${SNESRECOMP_RUNNER_INCLUDE_DIRS})
    add_test(NAME sm_overlay_blit COMMAND sm_overlay_blit_test)

    # The open-gesture guard. A host that skips it gets a browser that
    # reopens the frame after it closes, forever -- a frozen game.
    add_executable(sm_savestate_gesture_test
        tests/sm_savestate_gesture_test.c
        ${SNESRECOMP_ROOT}/runner/src/snes_savestate_menu.c
        ${SNESRECOMP_ROOT}/runner/src/snes_overlay_draw.c)
    target_include_directories(sm_savestate_gesture_test PRIVATE
        ${SNESRECOMP_RUNNER_INCLUDE_DIRS})
    # snes_savestate_menu.c reaches SDL only for the SDLK_* keycodes, so the
    # headers are enough; nothing here links an SDL library.
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(SM_TEST_SDL2 QUIET sdl2)
        target_include_directories(sm_savestate_gesture_test PRIVATE
            ${SM_TEST_SDL2_INCLUDE_DIRS})
    endif()
    add_test(NAME sm_savestate_gesture COMMAND sm_savestate_gesture_test)

    add_executable(ppu_window_test tests/ppu_window_test.c)
    target_include_directories(ppu_window_test PRIVATE
        ${SNESRECOMP_RUNNER_INCLUDE_DIRS})
    add_test(NAME ppu_widescreen_windows COMMAND ppu_window_test)
    add_test(NAME sm_door_audio_trace
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/door_audio_trace_test.py)

endif()


# Launcher UI backend: the SHARED recomp-ui launcher (F:\Projects\recomp-ui,
# consumed as the ./recomp-ui junction/submodule) so every recomp game drives
# ONE launcher repo. The former in-tree launcher_ng has been retired from the
# engine in favour of this shared, console-agnostic launcher.
# CACHE PATH for the same reason as SNESRECOMP_ROOT: a debug worktree, or a
# build pinned against a specific launcher revision, needs to point this
# somewhere other than the in-tree submodule.
set(RECOMP_UI_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/recomp-ui" CACHE PATH
    "Path to the shared recomp-ui checkout used by this game")
set(RECOMP_UI_ENABLE_MODS ON CACHE BOOL
    "Enable Super Metroid built-in presentation Mods" FORCE)
include(${RECOMP_UI_ROOT}/recomp_ui.cmake)

if(BUILD_TESTING AND NOT ANDROID)
    add_executable(sm_video_test
        tests/sm_video_test.c src/sm_video.c src/sm_mods.c
        ${SNESRECOMP_ROOT}/runner/src/desktop/host_clock.c)
    target_include_directories(sm_video_test PRIVATE src ${RECOMP_UI_ROOT}/src
        ${SNESRECOMP_RUNNER_INCLUDE_DIRS})
    add_test(NAME sm_custom_video
        COMMAND sm_video_test ${CMAKE_CURRENT_BINARY_DIR}/sm-video-test.ini)

    add_executable(sm_renderer_test tests/sm_renderer_test.c src/sm_renderer.c src/sm_mode7.c)
    target_include_directories(sm_renderer_test PRIVATE
        src ${SNESRECOMP_RUNNER_INCLUDE_DIRS})
    add_test(NAME sm_custom_renderer
        COMMAND sm_renderer_test ${CMAKE_CURRENT_BINARY_DIR}/sm-renderer-test.capture)
    add_executable(sm_mode7_test tests/sm_mode7_test.c src/sm_mode7.c)
    target_include_directories(sm_mode7_test PRIVATE src)
    add_test(NAME sm_custom_mode7 COMMAND sm_mode7_test)
    add_executable(sm_render_capture tools/sm_render_capture.c src/sm_renderer.c src/sm_video.c src/sm_mode7.c
        ${SNESRECOMP_ROOT}/runner/src/desktop/host_clock.c)
    target_include_directories(sm_render_capture PRIVATE src
        ${SNESRECOMP_RUNNER_INCLUDE_DIRS})
    # Assertions are the test oracle, including Release configurations.
    foreach(sm_test sm_video_test sm_renderer_test)
        if(MSVC)
            target_compile_options(${sm_test} PRIVATE /UNDEBUG)
        else()
            target_compile_options(${sm_test} PRIVATE -UNDEBUG)
        endif()
    endforeach()
    if(NOT WIN32)
        target_link_libraries(sm_video_test PRIVATE m)
        target_link_libraries(sm_renderer_test PRIVATE m)
        target_link_libraries(sm_render_capture PRIVATE m)
        target_link_libraries(sm_mode7_test PRIVATE m)
    endif()
endif()

# Generated recompiler output (produced by tools/regen.sh; not committed).
#
# SM_GEN_DIR is overridable so a build can be pointed at generated C emitted by
# a DIFFERENT engine revision without disturbing this repo's src/gen. That is
# what an engine A/B needs: same sources, same flags, two emits. SMK and F-Zero
# already expose the same knob.
set(SM_GEN_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src/gen" CACHE PATH
    "Directory containing the private generated Super Metroid C sources")
# Added to the target by snesrecomp_target_generated_code() below, once the
# target exists. That helper owns both outcomes: the generated C when it is
# there, and -- only with -DSNESRECOMP_SETUP_HOST=ON -- the setup-host
# dispatch tables when it is not. An empty src/gen without that option stays
# the hard error it has always been, in the framework's words.

# What this port actually owns.
#
# Everything that is not Super Metroid now comes from the framework through
# the snesrecomp_target_* helpers below. This list used to also carry private
# forks of config.c, post_mortem.c, glsl_shader.c, opengl.c and
# fiber_compat.c — roughly 2,400 lines of host code that was either identical
# to the framework's or ahead of it, and which therefore stopped inheriting
# fixes in both directions. The parts this port was ahead on (the Android
# fiber backend, the post-mortem's DB-tripwire and stack/dispatch dumps, the
# GL presenter itself) were moved UP into snesrecomp rather than deleted.
set(SM_HOST_SOURCES
    ${SNESRECOMP_RUNNER_SOURCES}
    # game-specific runtime
    src/main.c            # host shim: identity + this title's hooks
    src/sm_rtl.c          # THE PORT: single-fiber frame model, NMI/IRQ, HDMA
    src/sm_cpu_infra.c    # RtlGameInfo registration
    src/sm_post_mortem.c  # the "sm" crash-report section (framework hook)
    src/sm_spc_player.c   # SPC upload interception
    src/sm_renderer.c     # read-only custom widescreen renderer
    src/sm_mode7.c        # SM's mode-7 object projection
    src/sm_display.c      # SM presentation geometry
    src/sm_video.c        # SM aspect/HUD policy + presentation clock
    src/sm_mods.c         # built-in presentation mod provider
)

if(ANDROID)
    # SDLActivity loads "libmain.so" and calls its SDL_main.
    add_library(SuperMetroidSNESRecomp SHARED ${SM_HOST_SOURCES})
    set_target_properties(SuperMetroidSNESRecomp PROPERTIES OUTPUT_NAME main)
else()
    add_executable(SuperMetroidSNESRecomp ${SM_HOST_SOURCES})
endif()


# NOT the repo root. The root holds VERSION, and on a case-insensitive
# filesystem (Windows, and macOS by default) an -I of the root makes libc++'s
# `#include <version>` resolve to that file instead of the standard header --
# the compiler then reads "0.1.0" as C++ and the build dies inside <cmath>.
# Nothing here includes anything root-relative; keep it off the search path.
# snesrecomp_target_desktop_host() arms the framework's
# snesrecomp_guard_header_shadowing(), which refuses to configure if any
# include directory grows such a file again -- on Linux too, so it is caught
# here rather than in Windows CI.
target_include_directories(SuperMetroidSNESRecomp PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/recomp
    ${SNESRECOMP_RUNNER_INCLUDE_DIRS}
)

# Desktop SDL is selected through the shared framework helper. Android already
# has the vendored SDL2 target from android/app/jni/CMakeLists.txt, so wire that
# target directly instead of asking find_package() for a host installation.
if(ANDROID)
    target_link_libraries(SuperMetroidSNESRecomp PRIVATE SDL2::SDL2)
    target_compile_definitions(SuperMetroidSNESRecomp PRIVATE
        SNESRECOMP_SDL3=0
        SDL_MAIN_HANDLED)
else()
    snesrecomp_target_sdl(SuperMetroidSNESRecomp)
endif()

# ── Shared host services, from the framework instead of a private fork ──────
#
# The desktop host itself: launcher flow, ROM resolution, config.ini +
# [KeyMap] (which is what gives the in-game overlays their hotkeys), the
# window and the SDL/GL presenters, audio, gamepads, the save-state browser
# and rewind filmstrip, the pacing clock, crash handlers and the post-mortem
# report (TIER2 adds the interpreter-gap manifest, which this port reads after
# every run). src/main.c is the shim that names this title and supplies its
# hooks. Before 2026-09 all of it was a 2,800-line private copy of the host in
# src/main.c, ahead of the framework's in places and behind it in others.
snesrecomp_target_desktop_host(SuperMetroidSNESRecomp TIER2)
# The recompiled banks, or the setup host when there are none and the build
# asked for one. The release workflow builds exactly that: CI has no ROM, so
# it cannot generate, and what it can honestly ship is a setup pack whose
# launcher regenerates on the player's machine.
snesrecomp_target_generated_code(SuperMetroidSNESRecomp ${SM_GEN_DIR})
# gen_stubs.c holds this port's hand-written bodies for hle_func targets. They
# are part of the recompiled code's surface -- they call into generated banks
# and are called from them -- so they belong to the same build as src/gen and
# not to a setup host, which has neither.
if(NOT SNESRECOMP_SETUP_HOST)
    target_sources(SuperMetroidSNESRecomp PRIVATE src/gen_stubs.c)
endif()
# rom.cfg at the repo root (git-ignored, the ROM's absolute path) is staged
# beside the executable when none is there yet, so a launch opens on it.
snesrecomp_target_rom_cache(SuperMetroidSNESRecomp)
# The tracked config.ini seeds the one beside the executable when none is
# there yet; after that the file is the player's.
snesrecomp_target_config_seed(SuperMetroidSNESRecomp)

# ROM identity. rom_identity.txt is the one place the digests live: this turns
# it into snesrecomp_rom_identity.h for src/main.c, and tools/regen.sh,
# scripts/package_release.sh and the release workflow read the same file.
# Before this call the digests were written out THREE times — here in
# rom_identity.txt, as a byte array plus a CRC literal in src/main.c, and
# again in src/codegen_setup.c, whose comment claimed to be the single source
# of truth. A revision bump is one edit now.
snesrecomp_rom_identity(SuperMetroidSNESRecomp ${CMAKE_CURRENT_SOURCE_DIR}/rom_identity.txt)

# Win32 Fiber API on ucontext (desktop) / pthreads (Android) — sm_rtl.c's
# single-fiber frame model runs on it. The Android backend in the framework
# copy came from this port.
snesrecomp_target_fiber_compat(SuperMetroidSNESRecomp)

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

if(ANDROID)
    target_link_libraries(SuperMetroidSNESRecomp PRIVATE
        ${SNESRECOMP_RUNNER_LIBRARIES}
        android
        log
    )
else()
    target_link_libraries(SuperMetroidSNESRecomp PRIVATE
        ${SNESRECOMP_RUNNER_LIBRARIES}
    )
endif()

# The post-mortem's Win32 DbgHelp link (StackWalk64, SymFromAddr) and the GL
# presenter's OpenGL::GL link both come with snesrecomp_target_desktop_host()
# above, which knows it needs them.

# Dear ImGui pre-boot launcher (GUI) from the shared recomp-ui repo. One call
# wires sources/includes/RECOMP_LAUNCHER define/CXX/asset-staging (same SM
# boxart; recomp-ui's default pad.tga is already the SNES pad).
# CONSOLE is required by recomp-ui: it selects exactly one validated
# assets/consoles/<id>/ manifest, so an SNES game cannot silently stage N64
# cartridge or NES pad art next to the exe.
recomp_target_launcher_ui(SuperMetroidSNESRecomp
    CONSOLE snes
    BOXART ${CMAKE_CURRENT_SOURCE_DIR}/recomp/launcher/boxart.tga)

# Generate & rebuild from inside the launcher: pick ROM -> recompile locally
# -> cmake --build -> relaunch. The implementation is the framework's and the
# host wires it (snesrecomp_codegen_host_autowire); this project contributes
# no configuration.
target_sources(SuperMetroidSNESRecomp PRIVATE
    ${SNESRECOMP_ROOT}/host/snesrecomp_codegen_host.c)
target_include_directories(SuperMetroidSNESRecomp PRIVATE
    ${SNESRECOMP_ROOT}/host
    ${RECOMP_UI_ROOT}/src)

# GLSL shader presets (CRT/scanlines) for the OpenGL desktop backend. The
# launcher's shader dropdown scans assets/shaders beside the executable for
# *.glsl/*.glslp; src/main.c opts this game into that row.
if(NOT ANDROID)
    snesrecomp_target_stage_dir(SuperMetroidSNESRecomp
        ${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders
        assets/shaders)
endif()
