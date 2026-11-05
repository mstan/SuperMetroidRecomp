# Exposed widescreen rooms

Tracking: `beads-8wg.6.44`. Branch: `feat/exposed-widescreen-rooms-20261105`.
Base: current unified main `cfb1d6b` (v0.4.2), not the stale ordinary checkout.

## Approved behavior

Expose secrets and connected rooms instead of concealing them with black
rectangles. Extend authored foreground walls and hatches where possible.
Show each neighboring room's own scenery, palette and animated environmental
effects. The persisted **Neighboring room logic** option defaults On. Off
retains scenery, environmental animation and working door bubbles, while
disabling neighboring gameplay actors. Valid shots must trigger either door
face and synchronize both sides while respecting ammunition and locks.

On permits ordinary enemy simulation and real combat across open connections;
walls and closed doors block interaction. Bosses, progression scripts,
cutscenes and encounter locks retain normal room-entry triggers. Preserve
native saves. Continuous camera/world traversal is now the requested default;
retain the native door transition as an option (owner clarification 2026-11-05). Current-build save/load, rewind,
rollback, replay and netplay must include resident simulation state/settings.
Old save-state migration is unnecessary.

The former Stable/Mods split was unified before this work. Apply this behavior
to the current unified product, preserving its optional enhancement packages.
This request supersedes deliberate secret/arena concealment in prior feedback.

## Implemented and evaluated

- ROM-derived connected-room terrain, definitions, graphics and palettes.
  Library backgrounds, streamed second-layer backgrounds, parallax and
  incoming-door room-state selection are decoded separately from active RAM.
- View-bounded, cycle-safe room traversal aligned from actual exits/arrivals.
  Door-plane clipping permits overlapping authored allocations and neighbors
  in unused portions of L-shaped rooms. Elevators retain native transitions.
- Preview composition in widened margins, including unused active-room
  allocation, on portable/native/full-copy renderer paths. Arena staging
  masks no longer conceal scenery. Native viewport/HUD remain separately owned.
- Cartridge sweep: 255/255 rooms produce previews; 641 resident placements and
  166,448 sampled pixels, with real active terrain and repeated publications.
  This is terrain-decoding coverage, not complete visual/gameplay acceptance.
- The full candidate game builds and all 28 existing CTests pass, including
  portable/native/full-copy renderer paths, row planner, exposed arena
  geometry, HUD, controls, map, activity and snapshot support checks.
- Recorded Speed Booster Hall raster replay: 0/49,152 native gameplay pixels
  differ. First preview image was inspected. Motion/full-game fidelity is not
  established by this single capture.
- Independent runtime prototype executes native cartridge enemy/FX routines
  through the existing interpreter core with a private WRAM/MMIO bus.
  No active-machine bus, audio output or SRAM is installed. Unsupported bus
  accesses fail explicitly. Boss populations are entry-only in this prototype.
- `$92FD` and lava room `$AA0E`: 60 native ticks move two enemy slots; lava
  FX state advances. Interleaved independent contexts remain byte-identical,
  active RAM stays unchanged, and resident save/restore continuation matches
  exactly, including private VRAM. Native lava animation publishes changed
  tiles into private VRAM. Truncated resident snapshots are rejected.
- Fresh candidate boot/landing, native room-loader access to Parlor and Speed
  Booster Hall, and current-build save/load with continued input pass. This
  bypasses Ceres completion, room access and health prerequisites explicitly;
  it does not test door-hit requirements or natural progression. The prior
  build produces the same 17 interpreter trap diagnostics and identical full
  WRAM at all three compared gameplay snapshots.
- Early automatic room captures contained blank native terrain and are
  excluded from visual acceptance despite zero native-pixel mismatches.
  A guarded current-build restore then produced a settled Hall capture: all
  four aspect renders have zero native gameplay pixel differences, and the
  32:9 image was inspected with actual terrain and neighboring scenery.

## Live resident integration (2026-11-05)

The native resident runtime is connected to frame boundaries and publishes
immutable, room-owned RAM, VRAM, palettes, ordinary enemies and environmental
projectiles. Neighboring room logic defaults On; Off suppresses ordinary actor
initialization/update while native FX, palette animation and animated tiles
continue. Effective settings latch on room entry, including same-room loads.
Primary boss definitions are filtered from pre-entry populations individually.
Entry-only encounters still require broader live qualification.

- Real unloaded-room $93AA beside Landing Site: On contains three native
  enemies; Off contains none. Actual 32:9 terrain, liquid and sprites were
  inspected. Both captured native gameplay centers match (0/49,152 pixels).
- The private compositor uses pending gameplay color-math registers rather
  than HUD hardware mirrors, fixing black neighboring terrain. Portal bounds
  no longer depend on the viewport used when a room was loaded.
- Whole-manager On/Off probes each advance 60 native ticks and restore the
  exact next serialized state, without writing active RAM. Native lava
  animation advances with logic Off. Truncated snapshots are rejected.
- Current-build execution/file snapshots include residents; renderer capture
  v3 owns the complete room scene. The renderer test destroys/mutates the
  producer immediately and verifies capture restoration and Off liquid pixels.
- Replay v4 records and checks a separate complete resident-state checksum.
  Real resident-bearing gameplay recording, save/load discontinuity and
  headless playback match all native and resident checkpoints (106 executions).
  Fresh-boot recording/playback also matches 516 executions. Existing shared
  trap diagnostics remain present and are not counted as feature regressions.
- Network activity profile v2 freezes enhanced mode, neighbor logic and
  transition choice with viewport width. Actual two-peer qualification and
  net-enabled compilation remain outstanding.
- All 28 CTests pass with this integration, including all renderer variants,
  persistent options, immutable publication/capture and replay parsing.

## Outstanding implementation and qualification

**Not release ready.** The Continuous/Native choice is persisted, but actual
continuous handoff is the next implementation step; it must not be represented
as completed merely because its menu control exists.

1. Implement continuous world/camera traversal and active/resident ownership
   handoff, preserving motion, actors, projectiles, collision, FX and room-entry
   scripts. Retain the native transition option; elevators/warps need appropriate
   native behavior. Prevent actor reinitialization/pop when entering a resident.
2. Implement paired native door PLMs, both-face valid-shot hit detection and
   shared unlock/animation. Retain ammunition, lock and progression rules.
3. Implement cross-open-door combat and projectile/actor transfer without
   duplicated hits/rewards. Walls and closed doors must block interaction.
4. Complete foreground/hatch continuation and dynamic PLM/item/door visuals;
   qualify liquids/raster waves, transition motion, intermediate high-FPS frames,
   Fit resizing and both HUD anchors.
5. Extend the disconnected Ceres room graph/tileset decoding. The earlier
   255/255 result covers the currently indexed Zebes graph, not all Ceres rooms.
6. Qualify rewind, run-ahead, rollback, replay with transitions, two-peer
   determinism, representative native gameplay and matched runtime performance.
7. Prepare a compact owner review with normal audio in a normal window, then
   release via the established channel only after required acceptance. Verify
   the replacement before removing the superseded delivery ZIP.

## Local evidence and retained work

Workspace: `G:/Temp/Codex/supermetroid/exposed-rooms-20261105`.
`source/` contains this isolated checkout; `build/` contains candidate tools
and the candidate game build. `first-preview.bmp` is the inspected offline
32:9 raster replay. `settled-hall/hall.capture`, four aspect images/logs and
`render-results.json` retain current-build visual evidence. `live-scenery` and
`baseline-live-scenery` retain matched gameplay state/diagnostic evidence.
Capacity records and exact build jobs are beside `source/`.
These are retained for continuing this feature, not delivered release files.
Additional evidence: `neighbor-enemy-settled-on/` and `-off/` contain actual
32:9 captures, typed resident data and zero-native-center comparison logs.
`resident-replay-visible/` and `resident-replay-visible-playback/` retain the
populated-room recording and successful reproduction; `resident-replay/` and
`resident-replay-playback/` retain fresh-boot replay coverage.
No player save, settings, release archive or running playtest was replaced.
The two standalone prototype EXEs were removed after the CMake-built probes
superseded them. Retained build/capture directories are needed for continuing
this unfinished feature and should be cleaned after final delivery.

The build reuses the existing verified generated translation units and pinned
engine/UI checkouts under `feedback-20261103`; generation inputs are unchanged.
The clean worktree's missing `recomp/funcs.h` was generated using the pinned
engine's supported sync tool. No generated C was edited.

Run owned-ROM probes by passing explicit cartridge paths to
`sm_room_preview_test`, and cartridge/WRAM/room-header arguments to
`sm_room_runtime_test`. They use assertions even in Release builds. Their
results do not replace live gameplay qualification.
