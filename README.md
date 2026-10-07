# SuperMetroidSNESRecomp

## Windows frame composition

The pinned shared framework uses cached HLE frame composition by default on
Windows x64. This host presentation optimization is separate from the guest
CPU's LLE scheduler and optional game-routine HLE described below. The existing
CPU, audio and status interfaces remain intact. Build the maintained
correctness-reference compositor separately with
`cmake -S . -B build-frame-lle -DCMAKE_BUILD_TYPE=Release -DSNESRECOMP_FRAME_IMPL=LLE`,
then `cmake --build build-frame-lle`. Selection is fixed at build time.
LLE can reduce performance; it remains available for correctness checks. Other
platforms keep LLE defaults, and existing CMake cache selections are preserved.
See [HLE defaults and opt-out](snesrecomp/docs/HLE_DEFAULTS.md).

The reviewed native-view, uncapped Windows route measured 427.737 to 1259.308 FPS
(+194.41%, process CPU -66.67%); it includes boot/menu work and natural demo play.
The owner accepted the normal-paced adaptive HLE build. These figures describe
that measured build/route, not a new measurement of later upstream title changes
or other platforms. Foreign compiler activity limits precision. Normal play
retains normal pacing/audio. See the shared framework's
[frame model](snesrecomp/docs/FRAME_MODEL_HOSTS.md).

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

## Screenshots

Custom widescreen at the landing site, with energy and the minimap anchored
to the outer edges:

![Super Metroid landing site rendered in ultrawide with edge-anchored HUD](docs/images/widescreen-landing-site.png)

Tourian gameplay with energy and reserves on the left, weapons centered,
and the minimap on the right:

![Super Metroid Tourian gameplay with the widescreen renderer and populated HUD](docs/images/widescreen-tourian.jpg)

Screenshots supplied by the project owner from the custom-renderer build.

## Windows and Linux releases

Download the Windows x64 ZIP or Linux x86_64 AppImage from this repository's
Releases page. Extract the entire Windows ZIP before launching the executable;
its DLLs and `assets/` directory are required. On Linux, make the AppImage
executable and launch it. Supply your own unmodified Super Metroid (Japan, USA)
ROM; no ROM or saved progress is included in either package.

Open **Mods** in the launcher to enable **Widescreen** (Fit, 16:9, 21:9 or 32:9)
and choose edge-anchored or centered HUD placement. **Presentation FPS** is a
separate mod with Auto and fixed frame-rate targets. Both are off by default;
turning both off uses stock rendering. Game logic retains its native timing.
These features are experimental: full-game visual fidelity is not certified,
and achievable FPS depends on hardware and the scene.

Settings and saves live beside the executable/AppImage. Keep that directory
writable, and preserve your settings and `saves/` when updating. The detailed
implementation and validation record is in `docs/custom-renderer.md`.

## Building from source

Prerequisites: a `snesrecomp` checkout at `./snesrecomp` (junction/symlink
to the sibling repo, pinned in `snesrecomp.pin`), a verified Super Metroid
ROM at the repo root, SDL3 + OpenGL, and the mingw64 toolchain (cmake,
gcc, ninja) on `PATH`. Regeneration also requires `rustup`; it builds and
requires the fast native analyzer by default. Set
`SNESRECOMP_ANALYSIS_BACKEND=python` only to use the slower reference path.

```sh
# 1. (once) clone the snesrev/sm decomp as the symbol/oracle reference
#    (commit pinned in refs/snesrev-sm.pin), then ingest its symbols:
git clone --depth 1 https://github.com/snesrev/sm.git refs/snesrev-sm
python tools/ingest_sm_decomp.py   # funcs -> recomp/*.cfg; tables -> recomp/sm_decomp_symbols.json

# 2. deterministic profile-scoped regeneration (no guest widescreen hooks). Strict mode
#    independently regenerates and requires byte-identical output.
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

## License

PolyForm Noncommercial 1.0.0. See `LICENSE`. Code in this repo is original;
vendored dependencies under `third_party/` retain their own licenses.

The game ROM and any data extracted from it are **not** in this repo and are
not licensed for redistribution.
