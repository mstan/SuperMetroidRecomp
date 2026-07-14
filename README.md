# SuperMetroidSNESRecomp

LLE-first recompilation of *Super Metroid* (SNES) into native C, using the
[snesrecomp](https://github.com/mstan/snesrecomp) framework. This repo
is the per-game side: the single-fiber runtime, the per-game `.cfg`, the
build glue, and optional HLE optimizations. The recompiled C output
(`src/gen/`, ~93 MB) is generated locally and is **not** committed.

This is game **#4** on snesrecomp, after Mega Man X, Super Mario World,
and The Legend of Zelda: A Link to the Past.

## What "static recompilation" means here

The ROM and interpreter are the architectural ground truth. Proven hot 65816
functions may be statically translated to C, while every absent or rejected
exact M/X variant executes the original ROM through LLE. HLE is an optional
optimization over that model. **The rest of the SNES is not recompiled** —
it's hardware: PPU rendering, the APU/SPC700 audio coprocessor, DMA and
HDMA channels, hardware register I/O, and bank-mapping all run through
the embedded SNES emulation in `snesrecomp/runner/src/snes/`. Recompile
the CPU, emulate the silicon.

The default LLE scheduler executes the real `WaitForNMI` loop and resumes at
its architectural continuation. Optional HLE mode replaces that wait with a
host-fiber yield for performance.

The ROM is **never** redistributed — you supply your own legally-dumped
copy.

## Current status

The default LLE-first build boots, renders, plays audio, completes the attract
demo, starts a new game, traverses doors, pauses, and saves at Samus's ship.
It remains a work in progress and needs broader end-to-end regression testing.

## Build

Prerequisites: a `snesrecomp` checkout at `./snesrecomp` (junction/symlink
to the sibling repo, pinned in `snesrecomp.pin`), a verified Super Metroid
ROM at the repo root, SDL2 + OpenGL, and the mingw64 toolchain (cmake,
gcc, ninja) on `PATH`.

```sh
# 1. (once) clone the snesrev/sm decomp as the symbol/oracle reference
#    (commit pinned in refs/snesrev-sm.pin), then ingest its symbols:
git clone --depth 1 https://github.com/snesrev/sm.git refs/snesrev-sm
python tools/ingest_sm_decomp.py   # funcs -> recomp/*.cfg; tables -> recomp/sm_decomp_symbols.json

# 2. deterministic profile-guided regeneration. Strict mode independently
#    regenerates and requires byte-identical output.
./tools/regen.sh --strict-idempotent

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

---

<p align="center">
  <sub><b>R.A.I.D. — Retro AI Development</b> · a Discord for AI-assisted retro reverse-engineering, decomp &amp; recomp</sub>
</p>

<p align="center">
  <a href="https://discord.gg/Ad9BwSzctP"><img src=".github/raid-discord.png" alt="Join the Retro AI Development (R.A.I.D.) Discord" width="200"></a>
</p>
