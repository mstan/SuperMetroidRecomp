# SuperMetroidSNESRecomp

Static recompilation of *Super Metroid* (SNES) into native C, using the
[snesrecomp](https://github.com/mstan/snesrecomp) framework. This repo
is the per-game side: the single-fiber runtime, the per-game `.cfg`, the
build glue, and the hand-written HLE shims. The recompiled C output
(`src/gen/`, ~93 MB) is generated locally and is **not** committed.

This is game **#4** on snesrecomp, after Mega Man X, Super Mario World,
and The Legend of Zelda: A Link to the Past.

## What "static recompilation" means here

The 65816 CPU code from the ROM is statically translated to C — every
function the game runs on the SNES's main CPU is a real generated C
function in `src/gen/`. **The rest of the SNES is not recompiled** —
it's hardware: PPU rendering, the APU/SPC700 audio coprocessor, DMA and
HDMA channels, hardware register I/O, and bank-mapping all run through
the embedded SNES emulation in `snesrecomp/runner/src/snes/`. Recompile
the CPU, emulate the silicon.

`WaitForNMI` ($80:8338) is HLE-replaced (`hle_func` in `recomp/bank00.cfg`
→ `HleSmWaitForNmi` in `src/gen_stubs.c`): the whole game runs on one
host fiber, and `WaitForNMI` yields it back to the host driver
(`src/sm_rtl.c`), which runs the recompiled NMI handler + emulates the
frame, then resumes the fiber exactly where it yielded.

The ROM is **never** redistributed — you supply your own legally-dumped
copy.

## Current status: bring-up (work in progress — NOT playable)

The port boots and executes the game logic, but does not render yet.

- ✅ Links and boots; `I_RESET` runs to completion and enters the main
  loop, yielding via `WaitForNMI` each frame.
- ✅ `game_state` ($7E:0998) advances through the opening-cinematic state.
- ❌ **No rendering** — per-frame graphics DMA isn't populating VRAM, so
  the screen stays black (a recurring `Warning! DMA from addr 0x880000`
  points at the malfunctioning transfer).
- ❌ Crashes at ~frame 1096 (~18 s) in `HdmaObjectHandler` — a runaway
  loop / stack overflow.

Next milestones: get graphics on screen → title screen → attract demo →
save menu → new game.

## Build

Prerequisites: a `snesrecomp` checkout at `./snesrecomp` (junction/symlink
to the sibling repo, pinned in `snesrecomp.pin`), a verified Super Metroid
ROM at the repo root, SDL2 + OpenGL, and the mingw64 toolchain (cmake,
gcc, ninja) on `PATH`.

```sh
# 1. (once) ingest symbols from the snesrev/sm decomp into recomp/*.cfg
python snesrecomp/tools/ingest_sm_decomp.py

# 2. regenerate the C (emits src/gen/*.c; ~93 MB; EXIT 1 on the stub-lint
#    is expected — unresolved indirect-dispatch sites are a known worklist)
python snesrecomp/tools/v2_regen.py \
    --rom "Super Metroid (Japan, USA) (En,Ja).sfc" \
    --cfg-dir recomp --out-dir src/gen
python snesrecomp/tools/v2_sync_funcs_h.py --cfg-dir recomp --out recomp/funcs.h

# 3. configure + build
cmake -G Ninja -B build -S . -DCMAKE_BUILD_TYPE=Debug -DCMAKE_C_COMPILER=gcc
cmake --build build -j 8

# 4. run (supply your own ROM)
./build/SuperMetroidSNESRecomp.exe "Super Metroid (Japan, USA) (En,Ja).sfc"
```

## Layout

| Path | What |
| --- | --- |
| `src/` | hand-written runtime: `sm_rtl.c` (single-fiber frame driver), `sm_cpu_infra.c`, `sm_spc_player.c`, `gen_stubs.c` (HLE bodies), `main.c`, `post_mortem.c` |
| `src/gen/` | generated recompiled C (not committed) |
| `recomp/` | per-bank `.cfg` (function boundaries, HLE/dispatch directives) + generated `funcs.h` |
| `snesrecomp/` | junction to the shared framework repo (tracked there; see `snesrecomp.pin`) |
| `tools/` | per-game helpers |
