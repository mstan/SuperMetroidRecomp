# Super Metroid custom renderer worktree

Implementation branch: `codex/supermetroid-custom-renderer`, approved for
integration into `main`. Tracking: `beads-8wg.6.3`.

This is an experimental custom renderer. The requested worktree and Mods
functionality is implemented; this is not a full-game visual-fidelity or
sustained-performance certification.

## Simulation in the wider view

The renderer remains read-only. A separate simulation viewport extends the
ROM's enemy processing queues in slot order and the common player/enemy
projectile culls. Native 4:3 retains the original behavior. Enemies can attack
sooner when visible in a wider view. Fit samples its window geometry before
simulation; replay records that geometry, and snapshots preserve it. Netplay
uses the host's fixed initial viewport for every peer throughout the match.
The portable ROM-only activity reference remains a build-time selection.

## October 25 feedback corrections

HUD/dialogue BG3 pages remain screen-space through message HDMA and boss
scrolling. Their empty HUD plane preserves opaque black for color math, so
suppressing repeated UI texels does not expose power-bomb/boss fixed color.
The taller `$5A` map also addresses the liquid FX page at VRAM `$5C00`.
Only its UI page (`$5800..$5BFF`) is suppressed in the margins; classifying
the entire map as UI incorrectly confined Crocomire's lava to 4:3. A
regression fixture now crosses that page boundary on adjacent scanlines at
all four supported test widths, while retaining the HUD/color-math tests.
The authored room matte now clips the final world pixel, including liquid
effects, and follows the terrain's interpolated raster camera.

Extended enemy spritemaps identify BG2 tilemap children with `$FFFE`. These
are sampled as one canvas containing their verified owner, rather than
repeating a Mother Brain or Crocomire body every hardware tilemap width.
Crocomire's ROM additionally parks BG2 at horizontal scroll256 outside the
native view. Margin sampling reconstructs his unclamped position from the
verified ROM owner instead of wrapping that hidden layer onto the edge.
Crossing into or out of that parked state also uses owner interpolation;
blending the parking register would slide the body in the wrong direction.
The native raster is preserved. Tests cover both sides, four widths and
intermediate phases, including moving across the original culling threshold.
Samus's separate cannon sprite is verified against the original pose,
animation-offset and character tables before receiving body interpolation.
Native gameplay pixels and guest simulation are unchanged.

Regression fixtures cover moving UI tilemaps, opaque-black subscreen math,
single BG2 canvases, color-only room edges, signed door cameras, intermediate
camera phases and cannon ownership rejection. Thirteen real tester captures
pass native-row-30-through-223 and complete HUD layout comparisons at four
widths with both HUD settings (104 cases). An adjacent recorded post-grapple
pair also verifies cannon ownership; simulation-rate output stays exact.
These checks do not certify the entire game's margins or identify the precise
reported file-selection/phantom-door transition. Those reports remain tracked.

The isolated renderer QA harness uses dummy video/audio by default and the
shared host's profiling controls. Audio emulation can remain enabled without
speaker output.

Further Norfair captures identify a scroll-alias defect in room `$AA0E`:
BG2 VOFS256 and the room offset768 address the same row of its 256-high
hardware map. Recovering their difference modulo1024 invented a world-Y
offset of -512 and emptied the extended background. Recovery and temporal
interpolation now use each tilemap axis's actual period. The recorded
49,664 native BG2 samples reconstructed from room data now match VRAM,
where every sample differed before; native final output remains unchanged.

All runtime policies now use the interruptible frame scheduler. The old
fiber driver, including its unportable execution snapshots, is removed.
`off/on/force/verify/auto` and legacy `lle/hle` all reach the Eye fixture
with identical 3,900-frame guest traces when no replacements are registered.
Policy names control promoted replacements at shared call sites, rather
than selecting a different guest scheduler.

Build-specific snapshots use the shared identity/CRC envelope and validate
the SM extension before guest writes. `tools/test_frame_states.py` exercises
real pause/unpause, health restoration, fresh-process continuation, movement,
and full WRAM equality after five classes of rejected input. Supply a TCP
build, verified ROM and artifacts directory with `--exe/--rom/--artifacts`.
Native SRAM is unaffected. Old snapshots are deliberately rejected.

Presentation history follows `RtlStateGeneration`: real load/rewind jumps
invalidate it, while speculative rollback retains run-ahead's future raster.
The renderer harness's `--run-ahead 1 --require-wide-content` gate with the
Eye fixture checks actual extended world pixels after composition; width
and guest-trace equality alone previously passed a black-margin fallback.

## October 2026 feedback regressions

Plasma Spark's nonrectangular level allocation includes complete unused
256x256 screens filled with solid block `$83FF`, whose diagnostic tile is a
white cross. Widening now mattes only screens containing 256 such blocks.
A screen with any other block remains visible, preserving real walls,
hidden content and PLM changes. The matte follows the raster/interpolated
terrain position and clips the final world pixel; native pixels are unchanged.
Four-width, three-phase fixtures exercise the moving boundary and content
preservation. The October27 replay burndown records the built traversal,
save/load and immutable before/after evidence for this correction.

The HUD's BG3 tilemap can remain on the color-math subscreen after the
gameplay IRQ. Its status/minimap rows now stay within the native horizontal
span, preventing the dotted minimap border from wrapping into a widened
margin. Native pixels and the first gameplay scanline remain intact. Static
and parallax room backgrounds also respect authored room boundaries in the
margins instead of wrapping unused tilemap columns into visible scenery.

Unpause states 16 and 17 retain the centered stock menu image. State 16 still
draws the menu fade; state 17 restores gameplay graphics under forced blank.
Room widening resumes at state 18. This fixes the visible map-fade corruption;
it does not certify Steam Deck frame times during the graphics reload.

The Morph Ball Eye's window-1 light cone now has its own margin extension.
It uses the retail tangent geometry, pre-update enemy/camera owner state and
the updated HDMA half-width. The complete native table and each captured
window register must agree before extrapolation. This avoids horizontal
flattening at a clipped edge, including while the enemy angle changes.
X-ray and power bombs retain their window-2 paths. Rendering does not run
enemy logic or write game state.

`sm_renderer_test` covers these pixel contracts, malformed/foreign beam state,
missing owner latches and changing angles in both raster-storage variants.
The renderer test also builds and runs as portable C on Linux. For a live
eye-cone fixture, use `tools/fixtures/eye_beam_debug.txt` with
`tools/test_custom_renderer.py --script ... --frames 3900 --fps 90 --audio`.
It grants Morph Ball/health, enters room `$9E9F` through the ROM door loader
and positions Samus near the eye; the guest activates the effect normally.
Supply the usual `--exe`, `--rom` and `--artifacts` arguments. Use muted output
for unattended runs. This is targeted diagnostic coverage, not a playthrough.

Original-ROM SNES9x comparisons reproduce partial-reserve fill dithering and
Etecoon song cancellation after pausing. The port matches both behaviors;
the original animations and sound-effect semantics are preserved. Broader
enemy/effect culling reports and general Steam Deck audio crackles still need
their own reproductions; these renderer changes do not certify those cases.

## Requested feature audit

| Requirement | Implementation and evidence |
| --- | --- |
| Separate worktree | `codex/supermetroid-custom-renderer`; original checkout is not modified by this work. |
| Replace old widening implementation | Old `sm_widescreen.c`, generated override script and widening roots removed; regeneration/build no longer references them; current generated C has no `g_ws_active` hooks. |
| Follow sibling Mods approach | `sm_mods.c` uses the same built-in provider interface as F-Zero/Star Fox; provider tests cover choices, enable state and persistence. |
| Game-specific custom renderer when enabled | Immutable per-line raster snapshots plus Super Metroid room/object data; Mode 1/7, HUD, entities and documented effects; sampled 14-room/four-aspect native oracle passes. |
| Adaptive and fixed widescreen | Fit and 16:9/21:9/32:9 choices; geometry tests and live window resizing. Current run `sm-resize-7d08nic1` passes six resizes and stock trace comparison; 32:9 HUD image inspected. The fixture now writes canonical `Fit`, not rejected lowercase `fit`. |
| HUD anchoring | Edges/Center choices, synthetic mapping tests and inspected live captures. |
| Independent presentation FPS | Auto and fixed caps matching the sibling approach; interpolation pixel tests, clock tests and live extra presentations with matching simulation traces. Caps are targets, not hardware throughput guarantees. |
| Unchanged game logic and stock off path | Both mods default off; native-width guest raster and one guest execution per simulation; stock/custom per-frame WRAM/CPU probes match in retained integration runs. Guest instruction execution is not performed by the custom renderer. |
| Usable worktree handoff | Windows executable and `tools/run_custom_renderer.ps1`; launcher/direct-start smoke tests pass with separate playtest saves. ROMs, saves, build outputs and local validation captures are not versioned. |

The retained tests establish these implemented features, not every possible
gameplay state. Natural grapple attachments, comprehensive boss/cutscene
interactions, full-game visual review and cross-platform packaging are not
certified. The following notes preserve those limits and the exact checks.
Final current-build launcher check `sm-launcher-jzgf_s8d` passed actual UI
selection and persistence of both features at 32:9/120 FPS, with all 3,200
simulation records matching stock. The saved settings and Mods screenshot
were inspected. Current CTest remains 5/5 passing, and `git diff --check`
passes. These complete the requested experimental worktree handoff, without
asserting release-level or every-scene qualification.

## Try this worktree on Windows

From the worktree root, run `./tools/run_custom_renderer.ps1`. It opens the
existing `build-custom/SuperMetroidSNESRecomp.exe` with the MinGW runtime DLL
directory on PATH and keeps video settings/saves in `build-custom/playtest`.
The shared keybinding loader still uses `build-custom/keybinds.ini` beside
the executable; neither location is in the original checkout.
Select your own Super Metroid ROM in the launcher, then open Mods. Enable
Widescreen, choose Fit/16:9/21:9/32:9 and HUD Edges/Center. Enable Presentation
FPS independently and select its cap. Press Play to save those selections.
Disable both mods for stock rendering; FPS alone keeps the native width.
The original checkout and its saves are not used by this helper.

`-CheckOnly` checks the executable/runtime paths without starting the game or
creating the playtest directory. `-RuntimeBin <directory>` overrides the
default `C:\msys64\mingw64\bin`. After configuring Mods, optional
`-DirectRomPath <ROM>` starts directly and **skips the launcher**; omit that
argument when you want to change Mods. The helper does not build the game,
download a ROM, or override your saved mod choices. Full-game visual fidelity
and sustained requested FPS remain under validation, as detailed below.

## Validation notes

Brightness conversion now uses an exact 32-entry per-scanline lookup after
color math instead of repeating component expansion/scaling for every pixel.
Tests cover all component and brightness values, with brightness changing on
each raster line. A 1,000-draw 32:9 interpolated edge replay measured 3.484 ms
before and 3.286 ms after; bitmap SHA256 remained
`308CC7E4FF63BE097EB58A0FF5D10FB2C958E72DCC2F33BC8E01CB792F7B4929`.
This is a single isolated before/after sample, not a sustained live FPS gain.
Audio-enabled run `sm-renderer-uwgcxuye` passes 3,600 stock/combined state
comparisons. Its 3000-3600 gameplay window presents 1,178 frames in 9.999833s
(117.80 Hz), with composition averaging 3.152 ms. Final 32:9 replay has zero
native pixel differences and no fallback. Sustained 120 Hz remains unproven.

Snapshot validity regression: `SmRendererBeginFrame` now marks object-owner
state valid only when `SmRendererLatchObjectState` actually supplied it. A
post-guest fallback copy remains available for discrete inspection but cannot
authorize object interpolation. The five-frame regression covers missing
latches, recovery after a missing latch, two consecutive valid snapshots,
and one-shot latch consumption. Synthetic fixtures explicitly provide their
intended pre-NMI state. The production loop latches immediately before
`RtlRunFrame`; the integration harness now requires that live custom captures
contain a valid owner latch. Run `sm-renderer-erhhw52k` passes 3,200 identical
stock/wide state records with that gate enabled (audio off); all five CTest
suites pass. This gate does not validate every individual object's ownership.
The subsequent audio-enabled four-mode run `sm-renderer-nsxvw99v` also passes
all 3,200 state comparisons and the live owner-latch gate. Combined mode
produced 6,089 presentations in 53.268s; FPS-only produced 6,157 in 53.242s.
These demonstrate independent presentation, not sustained 120 FPS. The latest
unpaced 32:9 replay has 194 custom/30 HUD/zero fallback lines and zero of
49,152 native gameplay pixels differ. Its landing-site image was inspected.

Ceres rotation diagnostic: `ceres_rotation_debug.txt` enters the shaft through
normal new-game input, then sets the escape flag and a safe vertical position
in the fixture only. The guest's `$89:ACC3` room code generates rotation;
matrices are never forced. Run `sm-renderer-lzi_9xij` matched stock/wide state
traces across 11,000 simulation frames (audio off). Adjacent captures 10347/
10348 in room `$DF45` have matrices `(256,15,-15,256)` and
`(256,16,-16,256)`. All four fixed-aspect replays retain zero native pixel
differences with 194 custom Mode-7 lines and no fallback. The later frame
11000 also replays exactly; 32:9 rotation and the interpolated pair were
inspected. The capture validator checks adjacent numbers, room, escape state,
brightness, Mode 7 and changed rotation coefficients. This is live guest
rotation evidence, not a full natural escape playthrough or comprehensive
rotating projectile/steam interaction coverage.

Grapple segments and the endpoint now have a margin rendering path. Geometry
uses the retail angle approximation and signed Q16 steps from ROM sine/cosine
tables `$A0:B3C3/B443`; the nearby decomp `$94:A957` comment identifies code,
not the table's ROM address. Pre-NMI owner state supplies the drawing origin,
length and animation pointers. Native-footprint pieces must match captured
OAM before the host emits margin pixels. Omitted segments peek current art
through bounded `$94:B0F4` jumps without writing timers or invoking guest code.
`grapple_debug.txt` with `--capture-grapple` captures a real adjacent pair:
`sm-renderer-zg2213kg` frame 3319 has length 36/function `$C703`, five matching
native pieces and zero native pixel differences. Its stock/wide 3,500-frame
traces match. The synthetic X=256 crossing checks all 288 margin pixel writes,
animation jump handling, cycle rejection and immutable input RAM. Grapple
interpolation and flare reconstruction are implemented; broad swing/attachment
layering coverage is not yet complete.
The controlled edge fixture `grapple_edge_debug.txt` with
`--capture-grapple --grapple-min-length 96` passed 3,500 identical stock/wide
simulation frames in `sm-renderer-tjqgvbur` (audio off). Frame 3328 has six
matching native pieces, 118 grapple margin pixel writes, and zero native
pixel differences. The 32:9 replay visibly continues the beam across the left
native boundary. Camera/source forcing is diagnostic-only, not renderer logic.
Segment interpolation verifies current native OAM, keeps current art, preserves
endpoint identity as segment count changes, and rejects phase/face changes or
large source/endpoint jumps. Newly born segments retain their current geometry
relative to the interpolated origin. Tests cover translation while growing
and recovery into native pixels in both wide and FPS-only modes. The flare
uses `$93:A1A1` maps with facing offsets `$93:A225/A22B`; native OAM agreement
gates margin drawing. Its pose interpolation also covers native pieces and
keeps current animation/palette. Nine synthetic flare scenarios cover native,
edge and offscreen origins, animation changes, phase changes and teleports.
Fresh edge run `sm-renderer-2yk9hlqw` passes 3,500 stock/wide frames with audio
enabled. Flare-edge run `sm-renderer-s1lc7sto` also matches 3,500 frames (audio
off): replay matches one flare OAM piece, draws 206 grapple margin pixels and
retains zero native-center differences. The flare visibly straddles native
X=0. These forced-source fixtures do not validate natural swinging/attachments
or every overlap/priority case.

Room coverage diagnostics: the integration harness `--capture-rooms` saves
one capture per room/mode after five consecutive full-bright gameplay frames.
`tools/test_room_captures.py --exe build-custom/sm_render_capture.exe --rom
<ROM> --captures <integration-wide-directory> --artifacts build-custom/integration`
replays all of them at 4:3, 16:9, 21:9 and 32:9, requiring native-center
equality and no stock fallback. It retains bitmaps/logs and a JSON report.
This is a sampled-room oracle for native pixels, not a proof of wider visual
fidelity or full-game coverage; inspect the wider images separately.
The 18,000-frame attract run `sm-renderer-wckqtxgp` passed identical stock/
wide simulation traces (audio disabled) and captured fourteen Mode-1 rooms.
Batch `sm-rooms-65bxr_yb` passed all 56 room/aspect replays with zero native
differences and no fallback. All fourteen 32:9 images were inspected, covering
different terrain, room boundaries, doors, visible enemies, liquid surfaces,
backgrounds and populated HUD inventory. No obvious margin seam or misplaced
HUD group was found in those samples. These are one frame per room, not
animation/transition acceptance; no Mode-7 room was reached by this sequence.

## Contract

The guest always uses native 256-pixel scanout. Legacy `g_ws_active` remains
false. The generated widescreen culling overrides and their generation roots
are removed; regenerate the full guest sources before building this branch.
The custom renderer owns a separate surface large enough for 32:9.

`SmCaptureSimulationFrame` performs the native raster IRQ/HDMA/PPU pass once
per simulated frame. `SmRendererLatchObjectState` captures sprite-owner WRAM
before the next guest frame/NMI publishes OAM. Room/terrain WRAM is captured
after guest execution. This distinction is required: OAM still represents
the preceding game's drawing state, while post-frame positions have advanced.
`SmRendererCaptureLine` takes immutable register, palette,
OAM and VRAM snapshots after the raster register changes. Repeated presentations
only read snapshots. They do not tick scripted inputs, audio, or game logic.

The launcher's Mods provider exposes widescreen and presentation FPS separately.
`sm-video.ini` stores Fit, 16:9, 21:9 or 32:9, HUD anchoring, and FPS preferences.
Both features off selects the stock renderer. FPS alone uses the custom
renderer at native width. Simulation cadence is 60.098811862 Hz.

Provider contract tests enumerate every package, feature, option and choice,
check selection round-trips without enabling disabled mods, and verify failed
settings commits preserve the saved file and expose an error. Actual launcher
testing subsequently caught `RECOMP_UI_ENABLE_MODS=OFF` hiding the entire Mods
screen. CMake now forces it ON, matching F-Zero/Star Fox; the provider supplies
SNES archive metadata instead of the generic `.psxmod` label (archive installation
itself is not implemented by this built-in provider).

Viewed real framebuffer screenshots in `build-custom/qa-launcher`: dashboard
Mods entry, both features, Fit/16:9/21:9/32:9 dropdown, all FPS choices and
HUD Edges/Center. Scripted clicks selected both mods, 32:9/120 FPS, then Play
committed settings and launched gameplay; its 3,200-frame trace matched stock.
Disabling both through the UI and Play selected stock mode with a 256-wide BMP.
Selecting Center while disabled persisted `HudAnchored=0` without enabling mods.
`tools/test_launcher_mods.py` reproduces the click-through in an isolated folder
and checks saved settings, screenshot presence and exact trace equality. Its
first run passed at `build-custom/integration/sm-launcher-yixcc9ac`.
Screenshots still require visual inspection; their mere existence is not a layout
oracle. The four-mode audio-enabled integration run
`build-custom/integration/sm-renderer-u6c0utg7` passed identical 3,200-frame
state traces and BMP dimensions: stock/FPS-only 256x224, widescreen/combined
682x224. Combined mode produced 5,999 presentations in 53.852s; FPS-only
produced 6,158 in 53.242s. Both remain below a sustained 120-Hz target.

## Implemented and checked

- Live adaptive Fit resizing passed in the actual Windows game window with
  both mods enabled (120-FPS cap, audio on): client sizes 800x600, 960x540,
  1260x540, 1440x405, 600x700 and back to 960x540 produced render widths
  256, 342, 448, 682, 256 and 342. Viewed landing-site captures at 32:9 and
  16:9 retained energy at the left and minimap at the right. The first 3,200
  simulation records matched the fresh stock reference; the run completed
  3,600 frames. Artifacts: `build-custom/integration/sm-resize-l7zjbxqf`.
  Reproduce using `tools/test_adaptive_resize.py --exe <exe> --rom <rom>
  --artifacts <directory> --reference-trace <3200-frame-stock-state.csv>`.
  The test resizes only its own child window and keeps isolated saves/configs.
  It checks composed frame dimensions, not the final OS framebuffer or every
  HUD item; the landing-site scene has no weapon inventory to inspect.
- Mode 1 mosaic stays widescreen instead of falling back to a stock-width
  image. Quantization precedes scrolling/tile lookup, follows the native X=0
  grid into the left margin, and is enabled independently on BG1/BG2/BG3.
  Tests cover all 16 sizes, all three layers and enabled/disabled combinations
  with nonzero scroll and both margins. The same 96 configurations now also
  check half-frame scroll interpolation against independently calculated
  pixels, with per-line scroll changes. Each case proves the expected
  midpoint differs from both simulation endpoints, rejecting frozen or
  duplicate-frame presentation. Gameplay use includes Phantoon's dying
  fade (`$A7:DA86`); that scene has not yet been captured for native comparison.
  Mode 7 mosaic now also stays on the custom widescreen surface: screen
  coordinates are quantized before the affine transform and flips, with
  BG1 controlling the shared vertical sample. Changes in mosaic settings
  snap the transform instead of interpolating incompatible sample grids.
  A rotating patterned-tile regression covers all 16 sizes, four flip
  combinations and mosaic on/off through both margins (128 cases). Actual
  gameplay Mode 7 mosaic has not yet been captured. The existing Ceres capture
  still passes the strict native-center comparison: 0/49,152 mismatches,
  194 custom lines, 30 HUD lines and no stock fallback.
  A fresh 12,000-frame attract comparison (`qa-mosaic`/`qa-mosaic-stock`)
  produced identical 12,000-line state traces, SHA256
  `2BD91C69FC0942CB0398E113D6ED20C943A6C0FC9E16B6E9FF7CE100E76B02E2`.
  Audio was disabled. No gameplay mosaic occurred in that run; it validates
  longer simulation consistency, not the Phantoon effect's visual fidelity.
- Geometry, independent Mods controls, persistence and clock invariants.
- Mode-1 background/OBJ compositor with windows and color math.
  Direct compositor regressions now cover 192 BG1 window cases (all enable/
  invert combinations, four two-window truth tables, overlapping/full-width/
  reversed bounds) and 128 color-window cases (all clipping/math-prevention
  policies, fixed/subscreen color, addition/subtraction and halving), checking
  native pixels and both margins. These tests establish the current generic
  edge-extension policy, not a reconstructed game-specific effect shape.
  Power-bomb windows now reconstruct unclipped scaled/preset bounds in the
  margins, gated by full native-table and per-raster register agreement.
  Relevant decomp: `sm_88.c` `$88:8219` blending, `$88:A206` shape profiles,
  WRAM `$0CE2..$0CF0` center/radii/speed; X-ray HDMA starts at `$88:8896`.
  Five captured power-bomb phases have exact native-center replay and wider
  curved bounds. X-ray has unclipped beam geometry and revealed-tile
  reconstruction; broader gameplay validation remains unfinished.
  Reproducible X-ray diagnostic: `tools/fixtures/xray_debug.txt`, 3,500
  frames, harness `--modes stock wide --require-xray`. The initial run
  `sm-renderer-rji4kn9f` has identical stock/wide traces and exact native
  capture replay, but visually exposes repeated BG2 tiles in the left margin
  and a beam boundary that stops widening outside native coordinates.
  Captured WRAM: frozen `$0A78=1`, phase `$0A7A=2`, angle `$0A82=$C0`,
  half-width `$0A84=10`, selected item `$09D2=5`. The harness checks these
  conditions so a non-activated fixture cannot masquerade as X-ray coverage.
  Geometry comes from `$91:C5FF..C998` and the Q8 tangent table `$91:C9D4`;
  source is Samus X +/-3, Y -16 (-12 crouching), relative to layer1 camera.
  The beam origin row is sourceY-1. Reveal tiles require a separate host
  reconstruction: `$91:CB8E` copies BG1 into a screen-sized BG2 map, then
  `$91:CD42/CDBE` apply block/BTS-dependent substitutions and LoadXrayBlocks.
  Extending ordinary room BG2 sampling is therefore incorrect during X-ray.
  `SmRendererXrayBounds` implements the retail Q8 tangent cones for up/down/
  left/right, oblique boundaries, widening and zero-width horizontal rays.
  It retains unbounded horizontal edges rather than turning native 0/255
  clipping into a constant-width beam. The live path requires full agreement
  with all 230 generated WRAM rows and per-line captured window registers;
  only margin membership changes. Sources in X=-256..511 use the distinct
  offscreen formulas from `$91:BE11` when appropriate, including the first
  ramp step (no apex row) and each boundary's signed 16-bit source correction.
  Outward-facing/otherwise nonmatching guest masks still fall back under the
  full-table agreement gate. Forty-four cardinal/spread cases check all 230 rows
  against incremental synthetic edge walks, plus invalid-input checks.
  Initial active-left capture replay extends 194 gameplay lines and retains
  zero native-center differences. The wider cone still reveals incorrect
  repeated BG2 tiles in that geometry-only build.
  The subsequent reveal path rebuilds room blocks on the host using the ROM
  type/BTS rules, linked horizontal/vertical extension blocks, area-dependent
  substitutions, multi-block writes, uncollected item PLMs and room-state
  special cases. It replaces only margin BG2 sampling in the reveal blend
  configuration; native pixels remain captured. Replacement quadrant swaps
  preserve the game's distinction from ordinary block pixel flips.
  The landing-site replay now has no repeated strips, matches all 1,024
  native reveal-map tiles and retains zero native-center pixel differences.
  Synthetic tests cover each simple/multi-block rule, linked BTS resolution,
  bounds, item collection and room overrides. This does not yet establish
  every room's visual fidelity, animated/dynamic tile interaction or all
  linked-block edge cases.
  Six further linked-block tests cover vertical chains, negative horizontal
  axis switches, the retail positive-horizontal axis-retention rule, cycles,
  out-of-room positive steps and zero-step unresolved extensions. Offscreen
  left/right inward cones have incremental edge-walk tests plus zero-width
  horizontal-row checks. No claim of exhaustive offscreen-angle coverage.
  `xray_offscreen_debug.txt` forces camera/position only in the diagnostic
  script. Run `sm-renderer-k6jtxcol` confirms source X=273, angle `$C0`,
  194 extended lines, zero native-center pixel differences and identical
  stock/wide traces across 3,500 simulations. It also reports **72/1,024
  reveal-map tile differences**. Subsequent `SM_XRAY_TILE_REPORT` CSV tracing
  accounts for all 72: each actual tile equals the native BG1 backup copied
  by X-ray, and each reconstructed tile equals room data. All differences
  are in six columns (world block X=43..48, twelve tiles each). This localizes
  the discrepancy to the copied native map after the forced camera jump,
  not reveal substitution. Do not count this run as reveal-map fidelity
  evidence; it is a useful stale-streamed-map diagnostic.
  Offline `SM_RENDER_REQUIRE_XRAY_MATCH=1` now requires an active extended
  beam and zero reveal-map differences, separately from the native-pixel gate.
  Four-mode audio-enabled X-ray run `sm-renderer-y42kma53` passed identical
  state traces across all 3,500 frames in stock, wide, wide+FPS and FPS-only.
  Wide+FPS presented 6,540 frames in 58.237s; native-width FPS-only presented
  6,655 in 58.232s. This verifies independent presentation, not sustained
  120-Hz throughput. The normal full-beam capture remains the fidelity gate;
  the forced-camera capture is intentionally rejected by the reveal-map gate.
  Fresh `sm-renderer-rvlo3_54` repeats the 3,500-frame stock/wide comparison
  with full-beam angle `$C0` required; state traces match. Its final replay
  again has 0/1,024 reveal tile differences and 0/49,152 native pixel
  differences (audio disabled, diagnostic equipment/demo setup).
  `xray_up_debug.txt` continuously holds Run+Up; `sm-renderer-9xeesc61`
  reaches angle `$F6` and matches stock/wide state traces for 3,500 frames.
  Replay extends 194 lines with zero native-center differences. Use
  `--require-xray --xray-angle 0xf6` to require that angle in future runs.
  An earlier segmented-button fixture returned to `$C0`; that run is not
  evidence for angled geometry.
  `SmRendererPowerBombWidths` now implements the unclipped scaled half-mask
  from `$88:8CC6/8D04/8D46`: high-byte fixed-point multiplication, inclusive
  segment endpoints with overwrite, centerward fill and empty outer rows.
  Invalid radius/non-monotonic profiles preserve the caller's output. Tests
  independently select covering segments for every radius/row (49,152 checks
  with a synthetic profile). Setting `SM_TEST_POWERBOMB_ROM` when running the
  renderer test also reads the supplied ROM's `$88:A206` profile; that passed
  another 49,152 comparisons locally. No ROM bytes were added to the repo.
  The live window path uses pre-NMI radius/preset values and indirect pointers,
  current generated row bytes/center, and the raster's one-line HDMA offset.
  It only overrides window 2 membership outside native X=0..255. Invalid
  metadata retains the generic policy; guest logic and effect timers are not
  changed. Twelve synthetic compositor scenarios cover offscreen/native
  centers, advanced pointers, inactive effects and corrupted tables.
  Fresh diagnostic captures in `sm-renderer-zw6ulem2/wide` cover phases
  90DF/91A8/8DE9/8EB2/8B98: all extend 194 gameplay lines and replay with
  zero differences among 49,152 native-center pixels below the HUD.
  After integration, `sm-renderer-9o8grqfv` ran the input-driven fixture for
  3,650 simulation frames in stock and wide modes with identical per-frame
  state traces (audio disabled). This is simulation evidence for that fixture,
  not exhaustive power-bomb interaction coverage.
  `--capture-power-bomb` in the integration harness records phase captures
  plus `SM_DMA_TRACE` register evidence. The input-driven
  `tools/fixtures/powerbomb_debug.txt` grants equipment and takes over a demo;
  this is diagnostic, not clean-playthrough evidence. The unsafe legacy
  `spawnpb` script operation is rejected because invoking guest code outside
  its fiber produced interpreter-cap failures. The harness now rejects those
  failures even if a run exits successfully.
  `tools/analyze_powerbomb_profile.py --rom <ROM> --wram <dump>
  --previous-wram <prior-dump>` compares all 192 native HDMA row pairs against
  scaled and current/previous preset profiles without modifying game state.
  Archived forced-equipment/pose captures are diagnostic evidence only:
  frame 3976 matches preset `$9F06` while stored `$0CF2=$9FC6`; frame 3978
  matches `$A086` while stored `$0CF2=$A146`. Both prove the pointer has already
  advanced by 192 bytes. Empty/offscreen tables match many candidate radii
  and must not be treated as proof of the unclipped shape. The indirect row
  tables are in ROM **bank $89**, at `$9800/$A101`, not shape-data bank $88;
  rows 511 and 512 both reference half-mask row zero. A fresh capture must
  still establish which DMA table pointer feeds the visible raster, rather
  than assuming post-frame WRAM equals the NMI-latched pointer.
  Background tile size, palette/priority bases and world-scroll origins are
  prepared once per captured scanline, after presentation scroll interpolation,
  instead of recomputed for every pixel. Room-margin sampling no longer does
  a discarded native tilemap lookup first. The Mode 1 mosaic regression now
  changes horizontal and vertical scroll on every raster line to catch stale
  prepared state. No context is reused across lines or presentations.
- Decompressed BG1 room-block sampling beyond the streamed tilemap.
- Streamed BG2 sampling from the second decompressed room map, with its own
  camera and ring-buffer offsets; library BG2 remains native tilemap-based.
- Ordered, immutable per-line captures, including raster palette/VRAM changes.
- Separate HUD status-band groups: energy left, weapons center, minimap right.
- Read-only normal/extended enemy spritemap reconstruction in the margins.
- Player beams/bombs/explosions, enemy projectiles (including shake offsets),
  and B4 pickup/effect owners now support bounded presentation interpolation.
  B4 continuity requires the same slot, palette/base, bounded position delta
  and unchanged instruction or ordinary four-byte advance when the previous
  timer was one. Arbitrary instruction jumps snap; guest timers never advance
  during presentation. Matching current ROM pieces handles OAM reordering and
  native-edge recovery through the same verified-owner path as projectiles.
  Twenty synthetic cases span left/native/right/margin origins, animation,
  instruction discontinuity, deletion and teleport rejection. Existing landing
  and A56B alpha-1 captures remain native-pixel exact. A real B4 pair was then
  captured at frames 4862/4863, slot 31 (`build-custom/qa-effect-live`), without
  modifying guest state. World position stays (574,106), camera motion moves
  screen X from 165 to 164, and the instruction advances C5C6→C5CA at timer
  expiry (1→2). This validates camera-relative movement plus real animation,
  not autonomous world-position movement. Viewed its 32:9 midpoint replay: 324 matched B4
  samples, 2104 total matched OBJ samples, 194 custom/30 HUD/0 fallback lines.
  Alpha-1 replay is 0/49,152 native-center mismatches. The observation run
  reached 18,000 frames; its first 12,000 trace records exactly match the
  prior stock reference. No 18,000-frame stock comparison is claimed.
  B4 instruction-list loops are deliberately not
  inferred as owner continuity without stronger evidence.
  Their margins are reconstructed from live ROM/WRAM data. Tests also cover
  beam flicker, deleted projectiles, bomb activation,
  effect lifetime and ordering against enemy drawing phases.
- Native build and a 3,200-frame 32:9 attract run.
- Raster-scroll interpolation and ROM-verified Samus-piece interpolation,
  with synthetic endpoint/midpoint and room-discontinuity tests.
- A real moving-frame capture exposed and corrected the pre-NMI/post-frame
  owner mismatch. New v2 captures retain both WRAM snapshots. A verified moving
  pair produces 194 matched OBJ samples and 194 interpolated scroll lines;
  its alpha=1 native center remains pixel-exact. V1 captures remain readable,
  but their missing owner history is not used for object interpolation.
- Mode 7 world scanout, including signed margins, flips, overflow behavior
  and transform interpolation. 98,000 sample comparisons against the native
  integer address calculation pass, as does the mixed HUD/world raster test.
- Fresh-game scripted Ceres run reached the shaft and exited normally after
  10,200 frames. Offline replays of Landing Site and Ceres each match all
  49,152 native pixels below the HUD at alpha=1. This comparison found and
  fixed the OBJ one-line sampling offset. HUD detection now follows the
  captured IRQ register state, not a fixed-height crop.
- Stock, 32:9 and 32:9/120-FPS runs produced identical 3,200-line WRAM-CRC/CPU
  traces: SHA256 `66976992EB0BAEA5B7C6696CD6F8E0DEF597797CB8880497204A58AC93CFC947`.
  The FPS run simulated 3,200 frames and presented 5,699 in 53.242 seconds.
  These runs were concurrent; this is not a single-process throughput benchmark.

The first Landing Site image exposed ship clipping at the native boundary.
Initially suspected BG2, the captured WRAM identified three gunship enemy
spritemaps. Read-only ROM spritemap reconstruction fixes the clipped ship in
offline replay of that same capture. This is one scene, not broad visual
acceptance. Enemy layering/transition coverage, special projectile effects, library
backgrounds, live Mode 7 escape validation and complete object interpolation remain incomplete.
Scroll and matched Samus pieces interpolate. Samus now uses her captured owner
origin delta after verifying each current ROM-map piece; previous OAM slot,
map offset, tile and palette may change with animation. The current animation
art and visibility are retained rather than cross-faded. Pose changes, jumps
over 32 pixels and unrecognized pieces remain discrete. Synthetic tests cover
all those cases and deliberately advance post-frame WRAM away from OAM.
The previously unmatched real animation pair (`qa-motion-v2`) now reports
566 Samus samples at alpha 0.5; alpha 1 passes the strict native comparison
with 194 custom lines, 30 HUD lines and zero fallback/mismatch. This does not
recover completely omitted OAM entries.
Native-footprint normal/extended enemy pieces now match the current ROM map
to a stable live owner and interpolate its origin independently of prior OAM
reservations and animation maps. The same owner delta applies to margin pieces.
Ambiguous overlapping owners with different deltas stay discrete, including
stationary/moving overlaps. Tests cover midpoint/endpoints, pre-NMI ownership, changed
enemy identity, invisibility and shaking (the latter stay discrete). This
enemy matching has synthetic coverage. Tests include map/tile/palette/offset
and reservation changes for normal/extended maps in the native view, straddling
X=256, and entirely offscreen. Real moving-enemy validation remains open.
Native-edge investigation found that ordinary enemy/projectile draw routines
retain OAM beyond X=255; our early footprint rejection caused the apparent
disappearance. Verified moving entries now recover their signed screen X from
the live ROM-map owner before interpolation, preserving their OAM slot/order.
Tests cover both edges for enemies/player/enemy shots, a far-left 9-bit alias
that must not appear at the right edge, and unrecognized hidden reservations.
Alpha 1 still uses the original native footprint; room A56B passes the strict
native gate. A real Landing Site edge pair (`qa-edge-live`, frames 3002/3003,
room `$91F8`, camera X 976 to 973) now verifies the recovery path: alpha 0.5
writes 26 recovered native-edge OBJ pixels. Its viewed midpoint shows the
gunship/Samus scene; alpha 1 has 194 custom lines, 30 HUD lines, no fallback
and zero native mismatches. The 6,000-frame diagnostic run completed normally
(audio disabled); its first 3,200 simulation records match the stock baseline.
Completely omitted OAM and other edge-motion scenes still need separate
coverage; this change does not invent missing entries.
After this change the room A56B capture still passes the strict native gate;
the Landing motion-pair offline benchmark measured 3.802ms/draw at 682 pixels
over 500 repetitions (not a live frame-rate measurement).
Player and enemy projectiles now verify the current ROM piece and use stable
owner-origin motion in native OAM and margins; previous reservation, animation
map, palette and tile need not match. Tests change all of these together in
native, edge-straddling and margin positions, and preserve pre-NMI ownership.
Overlapping player/enemy projectile owners with different deltas stay discrete
(tested), rather than selecting the first match. Tests also cover midpoint
and endpoint positions, birth, changed identity/type, jumps over 32 pixels,
deletion, beam flicker and earthquake exclusion. Visibility follows the retail
`$93:8254`/`$93:834D` rules; the shared visibility helper never advances guest
timers. Ceres Mode 7 projectile origins now use retail `$8B:8AD9` signed Q8
math, preserving separate product truncation and 16-bit wrapping. Bombs and
category-500 explosions stay unprojected as in `$93:834D`. Enemy reconstruction
also runs in Mode 7, using the already-captured Ceres steam projection offsets.
10,000 independent floor-based arithmetic checks pass, along with native/margin
projected midpoint tests and category exclusions. The existing Ceres shaft
capture remains native-center exact at 4:3, 16:9, 21:9 and 32:9; it does not
prove rotating projectile fidelity during escape. Projectile-specific real-motion captures
are still needed; synthetic tests are not broad gameplay acceptance.
Unverified entities remain
discrete. Real-game motion coverage and sustained high-FPS throughput still
need validation.

Latest margin/interpolation regression (`build-custom/integration/sm-renderer-pxooslh7`):
audio-enabled stock, widescreen and 120-FPS runs produced identical 3,200-frame
state traces; FPS mode presented 5,676 times in 53.323s (~106.4 Hz). This run
predates the per-presentation OAM ownership cache. That cache is keyed by both
OAM endpoints and tested against a mid-frame OAM rewrite. A subsequent run with
the cache and projectile interpolation (`sm-renderer-9pf5ajdo`) passed the same
audio-enabled trace comparisons and presented 5,958 times in 53.245s (~111.9 Hz).
This remains below the requested 120-Hz cap; it is not a sustained-target claim.
An isolated replay benchmark is available via `SM_RENDER_BENCHMARK_COUNT=N`
(1..10000) with `sm_render_capture`. It repeats one already-loaded frame and
reports C-clock time, excluding capture, guest execution, audio and SDL upload;
warm repeated snapshots are not a live throughput measurement. On the Landing
motion pair at alpha 0.5, 500-draw measurements before/after replacing variable
tile-size division/modulo with power-of-two shifts/masks were 1.572/1.248 ms at
256 pixels and 4.418/3.638 ms at 682 pixels. Output BMP hashes were identical.
Tests also cover 16x16 tile quadrants/flips on all BG layers. The subsequent
audio-enabled live run (`qa-fps-bitshift`) produced 6,058 presentations in
53.242s (~113.8 Hz), with the unchanged 3,200-frame trace hash. Renderer cost
improved, but this still does not prove sustained 120-Hz presentation.
Replaying the real Landing motion pair now
reports 5,462 matched object samples (includes enemies moving with the camera),
not only the previous 194 Samus samples.

## Reproduction

Additional visual coverage: fresh attract frame 18,000 (`qa-attract18000`),
room `$A56B`, game state 42, camera (256,256), room 32x32 blocks. Native-center
comparison passes at 4:3/16:9/21:9/32:9 with 194 custom lines, 30 HUD lines and
no stock fallback. Viewed the 32:9 bitmap: enemies, spikes and platforms are
present; the black right margin is beyond world X=512, the room boundary.
The 18,000-frame run completed normally; no matching 18,000-frame stock state
trace was run, so this extends image coverage, not state-trace equivalence.

Use an isolated working directory/config/save folder. The game may write SRAM.

- `SM_RUN_FRAMES=N`: exit normally after N simulated frames.
- `SNESRECOMP_WIDESCREEN=1`: select custom widescreen for existing smoke tools.
- `SM_VIDEO_ASPECT=32:9`: override the saved aspect for this run.
- `SM_PROFILE=1`: log count, total, mean and maximum wall time for guest
  execution, raster/capture, surface acquisition, custom composition,
  upload/presentation and state tracing. Disabled by default. Measurements
  include thread preemption/lock waits; they are not CPU-time profiles.
  `SM_PROFILE_START_FRAME=N` excludes earlier boot/transition frames. Output
  includes the measured window duration/presentation count and each stage's
  maximum-cost simulation frame. The harness exposes `--profile-start-frame`
  and checks sample counts and maximum-frame bounds in its JSON report.
- `SM_CAPTURE_FRAME=N` and `SM_CAPTURE_PATH=path`: save a versioned local
  renderer snapshot, never a guest save state.
- `SM_STATE_TRACE=path`: post-raster per-frame WRAM CRC32 and CPU registers.
  This is a determinism probe, not a full CPU/PPU/APU equivalence oracle.
- `sm_render_capture capture rom aspect output.bmp [previous-capture alpha]`
  replays raster captures offline without guest execution.
  Set `SM_RENDER_REQUIRE_NATIVE_MATCH=1` to fail on native-area mismatches,
  alpha other than 1, absence of custom lines or any stock-fallback lines.
  Output reports custom/HUD/fallback line counts; the bitmap is retained on
  comparison failure for diagnosis. Interpolated-input rejection was verified.
- `SM_CAPTURE_MODE7=path`: capture the first full-brightness Mode 7 frame
  in a room-view game state (excludes title/intro setup).
- `SM_CAPTURE_MOTION_PREFIX=path` captures an adjacent pair with matching
  moving Samus OAM; `SM_CAPTURE_MOTION_AFTER=N` excludes earlier frames.
  `SM_CAPTURE_MOSAIC=path` saves the first visible gameplay Mode 1 mosaic
  frame, excluding menu/cinematic effects. It does not alter guest state.
- `SM_CAPTURE_EDGE_PREFIX=path` captures an adjacent verified-owner pair whose
  midpoint crosses back into native view from offscreen OAM. It inspects
  captured state only; replay reports recovered native-edge OBJ pixel writes.
- `SM_CAPTURE_EFFECT_PREFIX=path` saves the first adjacent moving B4 effect
  pair with a verified current OAM/ROM match in a full-bright room view.
  It logs the selected slot/frame numbers and does not spawn or move objects.
  Replay reports matched B4 effect samples separately from other OBJ owners.
- Existing `SNESRECOMP_FRAME_BMP*` settings capture presented output.

For reproducible isolated stock/wide/combined-FPS/FPS-only runs, use a native Python interpreter:

```text
python tools/test_custom_renderer.py --exe build-custom/SuperMetroidSNESRecomp.exe --rom <verified-ROM-path> --artifacts build-custom/integration --frames 3200 --audio
```

The harness retains configs, logs, traces and a JSON report in a new directory,
and `--profile` additionally collects the stage timings in that report. Avoid
concurrent builds or other benchmarks while measuring throughput.
It checks every simulation-frame trace against stock, verifies rendered BMP widths
(682 for widescreen, 256 for stock and FPS-only), and verifies that higher
FPS produces extra presentations. It reports actual throughput; a pass does
not mean that the requested FPS was sustained or that audio is glitch-free.
The audio-enabled regression retained the same trace SHA256 as the earlier
silent runs. Deadline accounting and waiting have subsequently been refined
to reduce missed presentations without discarding simulation debt.
After sharing main/subscreen tile samples and using deadline-based waits, the
latest standalone audio-enabled run presented 5,990 frames in 53.244 seconds
while simulating exactly 3,200 frames with the same trace hash (about 112.5
presentations/s against a 120 target). Sustained target throughput remains an
open validation/optimization item.

Opt-in stage profiling was validated in the four-mode audio-enabled run
`build-custom/integration/sm-renderer-8nnag_hg`: all 3,200-frame traces match.
Combined mode produced 6,094 presentations in 53.244s (114.45/s); FPS-only
produced 6,156 (115.62/s). Combined mean/max wall milliseconds were:

| Stage | Mean | Maximum |
| --- | ---: | ---: |
| Guest execution | 2.655 | 537.740 |
| Native raster and immutable capture | 1.357 | 5.542 |
| Surface acquisition | 0.034 | 0.542 |
| Composition | 0.561 | 5.562 |
| Upload/presentation | 0.778 | 57.963 |
| Diagnostic state trace | 0.206 | 0.409 |

These cover boot/menus/attract, not sustained room gameplay alone. Stock also
had a 553-ms guest-frame maximum, so attributing the entire FPS deficit to
custom composition would be unsupported. Next measurement should isolate
steady gameplay and locate individual guest/display stalls. The new harness
`--profile` JSON path passed a separate 120-frame/four-mode smoke test at
`build-custom/integration/sm-renderer-v1zjr8yj` (60-Hz cap, no extra-frame claim).

The subsequent isolated window test (`sm-renderer-sauwjq88`, frames 3000–3600,
audio on) completed all four 3,600-frame traces identically. During that
landing-site interval, 32:9/FPS presented 1,170 frames in 9.999442s (117.01/s),
versus native-width/FPS 1,197 in 10.013289s (119.54/s). Widescreen composition
averaged 3.579ms versus native 1.270ms; widescreen guest execution averaged
4.361ms and raster/capture 2.294ms. These are a short gameplay sample, not a
whole-game performance guarantee. The final capture write is inside the
raster diagnostic bucket (the 22.26ms native-width maximum occurred exactly
at frame 3600), so it must not be mistaken for ordinary scanout cost.
Window/count/max-frame validation also passed the
short harness run `sm-renderer-6g2pvmzv` (start 60, end 120).

Preparing background invariants per scanline subsequently reduced a 500-draw
682-wide interpolated edge-pair replay from 3.566 to 3.074ms/draw. Before/after
BMP SHA256 matched (`308CC7E4FF63BE097EB58A0FF5D10FB2C958E72DCC2F33BC8E01CB792F7B4929`).
Live four-mode validation `sm-renderer-7ikucxzf` retained identical 3,600-frame
state traces. In the same 3000–3600 window, widescreen composition averaged
3.138ms (previously 3.579), and presentations increased from 1170 to 1183 in
about ten seconds (118.30/s). This remains below a sustained 120-Hz claim.
The unpaced widescreen final BMP also matched the prior run byte-for-byte;
A56B and Ceres strict native-center replays remained 0/49,152 mismatches with
no stock fallback. All five tests and strict warning-enabled compilation pass,
including the strengthened per-raster-scroll mosaic cases.

The `sm_custom_video` and `sm_custom_renderer` CTest targets keep assertions
enabled in Release builds. Unit tests alone do not establish visual fidelity
or unchanged guest state across a full gameplay run.
