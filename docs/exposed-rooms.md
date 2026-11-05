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

## Continuous handoff stage (2026-11-05)

`sm_seamless.c` now performs a planar destination load at the ordinary frame
boundary using bounded, actual cartridge instructions on the active bus. It
runs native room/door setup only on entry, without simulating transition ticks
or advancing the SPC timeline. Native/elevator transitions remain available.
A full machine backup restores the original native transition if an entry
routine cannot complete without a wait; that fallback still needs targeted
fault-path qualification.

The resident manager promotes ordinary enemy health, AI, death, animation and
extended state, retains the departing room as a resident and rebases the
presentation camera. Active collision/tilemap coordinates remain bounded.
The camera eases to the native room camera, while the compositor can show both
rooms throughout the native viewport. The HUD remains anchored. Resident scene
v2 and manager state v3 include the camera displacement in snapshots/replay.
Entry OAM is regenerated to avoid using departing-room screen coordinates.
Rain/spores/fog now sample their native BG3 graphics, palette/animation and
scroll state; the default native BG3 graphics base was corrected.

- Assisted Landing Site -> $93AA entry completes in one simulation tick.
  Samus clears the entrance, moves to x109 and receives normal enemy contact.
  Fresh Off mode also enters in one tick and repeats 180 movement frames
  identically after save/load. Fresh Native mode retains the original old-room
  transition state after one tick and settles in $93AA normally.
  The entrance plays the cartridge's blue-cap opening instead of closing on
  her. This is an entry/handoff test, **not** a natural valid-shot door test.
- Saving during camera carry and repeating 180 movement frames produces
  byte-identical complete active WRAM. Native runtime promotion tests retain
  resident enemy state while preserving active player data and continuation
  stack. Whole-manager On/Off continuation probes still pass.
- All 28 CTests pass, including new native-viewport resident ownership under a
  rebased camera, Off weather BG3 composition and prior native-mode oracles.
- First entry and continued 32:9 images were inspected. This remains a
  prototype: body placement still clamps at the destination edge, door opening
  is not yet coupled before crossing, and full temporal/FX fidelity is unproven.
- Early full-session replay attempts include unrecorded artificial debug RAM
  edits and cannot qualify the crossing. Async save/load commands did not
  create the assumed pre-entry checkpoint; the saved slot was already after
  entry. Keep these failed setups separate from runtime replay defects and
  from passing post-entry continuation. A correctly sliced post-entry replay
  restores the carried camera and matches all native/resident checkpoints for
  179 executions. Natural recorded input through the doorway is still required.

## Paired door and Ceres stage (2026-11-05)

Resident rooms now initialize and tick the cartridge's door PLMs with either
logic setting. Accepted native openings and closings propagate to the paired
face using reciprocal door definitions. Permanent unlock bits synchronize;
mirroring an opening does not grant ammunition or an unrelated unlock. Grey
kill-quota doors retain the authored quota when neighboring enemies are Off.
Non-door PLM scripts remain suspended. Door state/animation transfers on entry
without replacing active items, player data or the continuation stack. Manager
snapshot v4 also retains the paired-edge phase cache.

- Owned-ROM door probe passes all four blue orientations, wrong-ammo rejection,
  five-missile red caps, super/power-bomb caps, boss and kill-quota grey locks,
  closing/reopening, mid-hit restore and mid-animation promotion.
- Live native beam rejection and power-bomb consumption/opening synchronize
  Landing Site's yellow cap and $93AA's blue cap with logic On and Off. The
  neighboring population contains three enemies On and zero Off. Current-build
  state restoration passes. Access/equipment setup is assisted; the shots are
  actual controller input, not injected door-success flags.
- Natural right/left crossings and a second departure pass with both settings.
  The return continues for 180 gameplay frames and the second departure for 90.
  The full input replay matches 600 native/resident executions. Earlier apparent
  passes that stopped gameplay are superseded: seamless entry omitted the
  cartridge's bounded HDMA clear, and body overlap retriggered the entrance.
  Both are corrected. Body placement remains bounded by radius and still jumps
  at the edge; fully continuous body/collision coordinates remain outstanding.
- Native downward fire and gravity cross Parlor's bottom blue door, and a
  native jump returns upward from the Pit with Hi-Jump equipped. Both entries
  stay in gameplay and the return advances another 90 gameplay frames. Room
  access, starting position, health and the Hi-Jump prerequisite were assisted;
  shots, collision and crossings use normal input. This vertical case currently
  covers On mode, not both settings or all connection geometry.
- Ceres' disconnected six-room graph and full tileset definitions are now
  decoded. The owned-ROM sweep produces previews for 261/261 rooms, 651
  placements and 168,528 sampled pixels. Six Ceres default-state runtimes pass
  60 ticks, independent-context equality, exact restore and Off-mode checks.
  These are decoding/runtime probes, not Ceres gameplay acceptance.

Evidence: `paired-doors-live4-on/`, `paired-doors-live4-off/`,
`natural-door7-on/`, `natural-door7-off/` and `natural-door7-playback/`.
`doors-qualified.exe` and `doors-build-identity.json` preserve the exact
horizontal qualification binary before the subsequent Ceres rebuild.
`vertical-doors-6/` retains the passing vertical case. Earlier vertical
directories are failed/superseded input setups and do not qualify traversal.

## Outstanding implementation and qualification

**Not release ready.** Continuous handoff exists and the horizontal assisted
case works, but general seamless traversal and the complete door/combat contract
remain unqualified. Do not represent the feature as complete or release it.

1. Connect shots/projectiles reaching an inactive door face to its native hit
   handler. Qualify this separately from accepted-opening mirroring, which now
   works, including native ammunition, lock and progression rules.
2. Complete natural continuous traversal in all four directions. Eliminate
   edge-placement jumps and opening delays using the paired connection state;
   preserve motion, collision, actor/projectile handoff, native entry scripts
   and the native transition option. Qualify retained actors on return entry.
3. Implement cross-open-door combat and projectile/actor transfer without
   duplicated hits/rewards. Walls and closed doors must block interaction.
4. Complete foreground/hatch continuation and dynamic PLM/item/door visuals;
   qualify liquids/raster waves, transition motion, intermediate high-FPS frames,
   Fit resizing and both HUD anchors.
5. Qualify live Ceres scenery/traversal and dormant encounter helpers. Its graph
   and tileset decoding now cover all six rooms; live gameplay is outstanding.
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
`seamless-camera-qualified/`, `seamless-camera-oam/` and
`handoff-qualified-on-continuous/` retain assisted-entry and restore evidence.
`handoff-guarded-on-continuous/` records the failed pre-entry checkpoint setup;
its post-entry continuation is useful, but its unsliced replay is not a pass.
`handoff-camera-tail-playback/` retains the passing 179-execution post-entry
replay. `handoff-options-fresh-off-continuous/` retains a fresh Off handoff and
180-frame identical save/load continuation; `handoff-options-fresh-on-native/`
retains the native-transition option check. `handoff-qualified.exe` preserves
this exact runtime; `handoff-build-identity.json` records its hash/pins. These
artifacts remain task evidence outside delivery folders. The four
superseded standalone current-turn EXE/object probes were removed; CMake probes
are the supported replacements.
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
