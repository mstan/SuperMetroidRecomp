# Captured Mode-1 row planning

`SM_NATIVE_ROW_PLAN` selects the Super Metroid adapter for the framework's
pure `SnesRenderPlanCompose` job. It is a build-time development option,
currently OFF pending final qualification and owner approval. Build with
`SM_RASTER_IMPL=NATIVE` and `SM_NATIVE_ROW_PLAN=ON` to exercise it. The native
compositor with the option OFF and the separate LLE renderer remain available.

The adapter prepares enabled background rows once, then resolves main/subscreen
visibility and composition over spans with constant window decisions. Native
center and margin plans are separate: the eye, X-ray and power-bomb geometry
may extend only into margins. The authored room matte clips the finished world
pixel before background preparation. Existing native tile jobs, scalar scene
mapping, sprite emission, palette/color math, HUD and Mode-7 paths retain their
contracts. Room, object and camera policy stays in this title.

Inputs belong to one immutable captured raster line. The adapter adds a local
three-layer pixel row (4,104 bytes at the maximum supported width), with no new
persistent cache or serialized field. It does not write the live PPU or guest
memory. Each scanline starts from its own captured register/VRAM state, so no
new reuse crosses HDMA, IRQ or resource boundaries. Selection is included in
source/snapshot/replay identity and startup diagnostics. No new netplay
protocol or cross-build compatibility is claimed.

`sm_full_view_row_plan` always compiles the new path, independently of the
product default. Its 512 complete rows compare against independent scalar
composition/window evaluation across widths256/342/682/684, large tiles,
flips, mosaic, screen enables, windows, room mattes and color math. The existing
native renderer/effect fixtures also pass with the option ON on Windows and
Linux. Actual captured scenes and adjacent grapple/Mode-7 frames match the
native reference at four aspects and five interpolation phases (68 cases).

Matched original/candidate live checks cover grapple, power bombs, Ceres
rotation, extra presentations and run-ahead. These diagnostic fixtures use
equipment/position/escape setup and are not unassisted progression evidence.
Compare identical viewport/features across builds: the older harness's
stock-versus-wide trace assertion fails at frame3114 in both builds, while
the per-viewport original/candidate traces and images match. The title already
supports viewport-dependent simulation activity.

Current-build state qualification and balanced built-game performance acceptance
are still in progress. Preliminary pilot timings are not accepted campaign
results. Tracking: `beads-8wg.6.35`, roadmap `beads-49cy`, central optimization
campaign `beads-2wy8.1`. Owner approval of the combined candidate playtest is
required before merge. Original Xbox qualification remains separate.
