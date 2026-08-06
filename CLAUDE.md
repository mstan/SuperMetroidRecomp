# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this repo is

The per-game side of a static recompilation of Super Metroid (SNES) into native C, built on the `snesrecomp` framework (game #4, after Mega Man X, Super Mario World, and A Link to the Past). LLE-first: proven-hot 65816 functions are statically translated to C; anything absent or rejected executes the original ROM through the embedded LLE interpreter. Only the CPU is recompiled — PPU, APU/SPC700, DMA/HDMA, and register I/O run through the SNES emulation in `snesrecomp/runner/src/snes/`.

`snesrecomp` (framework: recompiler, runner, tools) and `recomp-ui` (shared Dear ImGui launcher) are git submodules. The ROM (`Super Metroid (Japan, USA) (En,Ja).sfc` at the repo root) is never committed — `*.sfc` is gitignored.

## Hard rules

- **Never edit `src/gen/`** (generated, gitignored, ~93 MB). Fix the generator (`snesrecomp/recompiler/`), the cfg directives in `recomp/*.cfg`, or the C runtime in `src/`, then regenerate. No stubs or dummy returns.
- **Always full regen** — never a partial per-bank regen (`--banks`); partial output breaks cross-bank M/X variant references. After cfg changes, `recomp/funcs.h` must be re-synced (regen.sh does this).
- **Owner-gated (do not do without explicit decision):** merging investigation branches to main, releasing, reconciling the multi-tier engine branches.
- Root-level `_`-prefixed files and `*.jsonl` traces are per-session scratch (gitignored) — the expected place for throwaway diff/inspection scripts.

## Commands

```sh
# One-time: clone the snesrev/sm decomp (commit pinned in refs/snesrev-sm.pin)
# and ingest its symbols into recomp/*.cfg + recomp/sm_decomp_symbols.json
git clone --depth 1 https://github.com/snesrev/sm.git refs/snesrev-sm
python tools/ingest_sm_decomp.py

# Regenerate src/gen/ (deterministic; needs the ROM at repo root).
# --strict-idempotent regenerates twice and requires byte-identical output.
# --no-tests skips the framework test suite. Native analyzer needs rustup;
# SNESRECOMP_ANALYSIS_BACKEND=python selects the slower reference path.
./tools/regen.sh --strict-idempotent

# Configure + build (SDL2 + OpenGL required; fails if src/gen/ is empty)
cmake -G Ninja -B build -S . -DCMAKE_BUILD_TYPE=Debug -DCMAKE_C_COMPILER=gcc
cmake --build build -j 8

# Run (binary name has .exe even under MinGW paths; drop it on POSIX builds)
./build/SuperMetroidSNESRecomp "Super Metroid (Japan, USA) (En,Ja).sfc"

# Android (arm64 APK; needs Android SDK/NDK + JDK 17; see android/)
bash android/fetch_sdl.sh    # one-time: pin SDL2 source + Java glue
( cd android && ./gradlew assembleDebug )
# Then push rom.sfc, rom.cfg (absolute ROM path), and a config.ini with
# SkipLauncher=1 into /sdcard/Android/data/com.snesrecomp.supermetroid/files/
```

### Tests

```sh
ctest --test-dir build                       # all game-side tests
ctest --test-dir build -R sm_widescreen      # single test by name regex
python3 snesrecomp/tests/v2/run_tests.py     # framework (recompiler) suite
```

Game-side ctest targets: `sm_display_geometry`, `ppu_widescreen_windows`, `sm_widescreen_helpers` (plain C binaries in `tests/`), and `sm_widescreen_visual_smoke` (Python; skips with code 77 when artifacts are missing).

## Architecture

### Generation pipeline

`recomp/bankXX.cfg` files carry per-bank directives: `func <Name> <pc> end:<next>` boundaries (harvested from the snesrev/sm decomp — the symbol/behavior ground truth), `hle_func` (route a PC to a hand-written C body), and `indirect_dispatch` (authorize `JSR (abs,X)`-style indirect calls with enumerated targets; dispatch target tables come from `recomp/sm_decomp_symbols.json`). `tools/regen.sh` drives `snesrecomp/tools/v2_emit.py` with the runtime profile `profiles/attract_tier2.json` (selects observed AOT work), applies widescreen overrides, and syncs `recomp/funcs.h`. Unauthorized indirect calls with WRAM pointer bases become runtime dispatches through `cpu_dispatch_*` in the runner.

### Single-fiber frame model (src/sm_rtl.c)

Unlike MMX's multi-slot scheduler, Super Metroid runs as one linear program: reset falls into the main game loop (`$82:8948`), which calls `WaitForNMI` (`$80:8338`) from arbitrary call depth. The whole game therefore runs on ONE host fiber. `WaitForNMI` is HLE-replaced (`bank00.cfg` → body in `src/gen_stubs.c`) with a yield to the host; the host emulates the frame (NMI + PPU), then resumes the fiber, preserving the full C call stack. Architectural register state is saved/restored around each fiber switch. `src/fiber_compat.c` shims Win32 Fibers onto ucontext for macOS/Linux, and onto pthread+condvar handoffs on Android (bionic has no makecontext/swapcontext).

Other game-side runtime: `sm_cpu_infra.c` (game registration consumed by the runner), `sm_spc_player.c` (audio), `post_mortem.c` (crash reporting, see below), `main.c`, `sm_display.c`/`opengl.c`/`glsl_shader.c` (presentation).

### Widescreen

Opt-in and runtime-gated (`g_ws_active`); when off, original branches evaluate unchanged. `tools/apply_widescreen_overrides.py` injects idempotent, marker-tagged edits into generated C at stable ROM basic-block labels — the sanctioned exception to "never touch src/gen by hand," done by tool so regen can't silently drop it (a CMake custom target runs it with `--check` before every build). `recomp/widescreen_aot_roots.c` forces the required enemy/e-projectile hook families to be materialized at regen; `src/sm_widescreen.c` holds the host-side helpers.

### Debugging workflow

- `build/last_run_report.json` is the always-on post-mortem written on crash/exit: CPU state, recomp stack, abandons, tier2 coverage, dispatch-log ring, DB/PB ring, and an SM-specific `sm{}` section (game_state, enemy slots). It is the primary crash-diagnosis artifact — the TCP debug server is not usable for SM.
- Differential oracle: `snesrecomp/tools/snesref` (headless snes9x libretro, per-frame WRAM trace via `SNESREF_FRAMES`/`SNESREF_TRACE_FILE`); recomp side traces via `SNESRECOMP_WRAM_TRACE_FILE`. Whole-WRAM traces don't align frame-for-frame — diff a single semantic variable's timeline instead (e.g. game_state `$0998`).
- The `EnableSnes9xOracle` runtime option only makes sense from boot (it can't follow save-state loads); see the warning in `config.ini`.
- Env-gated probes: `SNESRECOMP_SBOUND=lo-hi` (S/DB at every block in a PC range), `SNESRECOMP_IBRWATCH=lo-hi` (interp-bridge per-step trace).

`DEVELOPMENT.md` is the durable in-repo dev log (root-cause writeups, current blockers, open items) — read it for the current state of bring-up work and append milestone writeups there.

## Configuration

`config.ini` (tracked) holds runtime settings: hotkeys, gamepad maps, renderer/audio options. Per-developer overrides go in `config.local.ini` (gitignored), applied after `config.ini`.
