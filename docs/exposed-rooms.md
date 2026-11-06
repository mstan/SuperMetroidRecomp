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

## Continuous collision and traversal

This candidate replaces the early trigger/clamp path for regular planar
hatches. Native horizontal/vertical collision dispatch samples the neighboring
room's actual cap/wall cells when the body overlaps a doorway. Inside-body
samples use ordinary air instead of executing foreign PLMs/scripts. Ownership
changes when Samus's center crosses the door plane, preserving the exact body
coordinates. Explicit native/debug entries retain their bounded setup, and
incoming-door state changes retain the cartridge's newly selected population.

- Owned-ROM manager probes pass open/closed opposing-cap collision and the
  exact pre/post center boundary with logic On and Off. Coordinate probes cover
  both scan directions, negative cells, eight-bit row multiplication and the
  native tile-index high-bit alias. All 28 existing CTests pass.
- The supported release regeneration recipe passes strict idempotence with
  three new inside-body dispatch hooks. No generated C was edited. The candidate
  uses task-local `gen-continuous/`, rather than changing the older shared tree.
- The supported full game build passes. Frame-by-frame live right/left and
  down/up round trips pass with logic On and Off on the current candidate.
  Gameplay stays in state 8 and world body coordinates cross without an entry
  clamp. Horizontal return continues for 180 native ticks; vertical return for
  90. Native input fires the hatches; initial room access/position, health and
  the Hi-Jump prerequisite are assisted. This qualifies these two regular
  connections in all four directions, not every stock exit.
- Vanilla up-doors accept a leading-edge contact before a normal jump's center
  clears the outer plane. Continuous mode carries that final upward movement
  over actual gameplay frames through an open, aligned pair. The guard uses
  the cartridge's captured upward state 1, rejecting downward/idle states.
  Doorway presentation also follows before the center changes ownership,
  preventing the native camera limit from leaving the player offscreen.
- Player projectile slots, super-missile links, timers and emitted trails
  survive handoff, with only their room-relative positions rebased. A native
  beam remains live with continuous world coordinates across right entry, On
  and Off. This is not cross-room enemy-hit/actor-transfer acceptance.
- The rebuilt portable/native/full-copy renderer probes pass signed boundary
  positions and interpolation. The 4:3 owned-ROM probe checks that the camera
  follows a doorway in two-pixel steps while keeping the body visible.
- Image inspection found an entry-context defect despite the position/replay
  passes: DB=$7E routed absolute PPU/DMA writes into WRAM. Entering each native
  routine through its system-bank mirror fixes the unapplied transfers and
  repeated tiles. Current horizontal and vertical entry images were inspected
  after this fix; broad scene/FX fidelity remains outstanding.
- Current horizontal recording/playback matches all 308 executions including
  resident state. The retained Native option previously passed its original
  state-9 transition and 90 continuing ticks; that path remains separate.
- The current live-qualified binary is SHA-256
  `4fabf0a39d0815f6553047c14f860b866cb9d72d9be1b28394d2c5600094674d`,
  retained as `continuous-qualified.exe`. All 28 CTest binaries pass, and the
  final changed manager probe passes the actual cartridge-direction fixture.
- Three non-screen-edge stock door records and other non-planar connections
  retain their original path in this candidate. Special foreign terrain within
  a hatch also needs broader collision qualification; this is not general world
  physics acceptance or completion of seamless mode.

Current evidence: `paired-doors-continuous3-on/`, `-off/`,
`continuous-body5-on/`, `-off/`, `continuous-body5-playback/`,
`continuous-vertical7-on/`, `-off/`, `continuous-live6.log` and its passing
capacity receipt. `continuous-host-carry2.log` contains the 28-test pass;
`continuous-host-up1.log` contains the final rebuilt manager/cartridge probe.
`continuous-native-option1/` retains the native-mode check.
`continuous-build-job.json` is the completed supported generation/build job.
Earlier body3/body4 recordings, vertical setup attempts and SERVER's vertical4
failure are diagnostic history, superseded by these current traversal checks.
The standalone owned probe and the 61.8 MB local transfer ZIP were removed.

The player-shot bridge uses native block responses rather than a second weapon
simulation. Signed leading-edge/span samples defeat the cartridge's root-room
allocation bounds and unsigned row aliases. A resident borrows the actual linked
weapon pool only for its native block reaction, returns mutations in world
coordinates, and restores its private CPU/workspace. Corrected resident room size
to the cartridge's byte-index bound (512 bytes per screen, previously 256).
Uncapped native directions 0..3 now retain their geometric connections without
mirroring a bubble into placeholder arrival coordinates; the native closing-PLM
table and lower Landing/95D4 regression establish that contract.

Owned-ROM qualification ran on SERVER using the same GCC 15.2 C sources and
the pinned native interpreter. `projectile-server3.log` and
`projectile-owned-probes.json` retain all four blue orientations, locked colored
ammunition/count rules, grey locks, inactive-face reactions with logic On/Off
and Native/Continuous, exact mid-hit restoration and unchanged active ownership.
The colored-door fixture clears only its private initial unlock bits so prior
power-bomb qualification cannot turn its supposed locked cap into a native blue
cap. Production progress remains intact. Supported strict generation/full
build and all 28 CTests now pass. The current controller gate is
`qualify_world_shots2.py`; its `world-shots3-*` and `world-shots4-*` runs pass.

Neighbor weapon/contact responses now borrow the active linked weapon slots
and player view for the cartridge's native hitbox/vulnerability/shot/touch AI.
Only weapon mutations, player effects and native super-hit shake cross back;
the private CPU, borrowed slots/player data and work indices are restored.
The world admits these responses only across current-state planar connections
whose actual cap blocks are air, with projectile centers/body overlap assigned
to the resident's clipped allocation. An opening blue PLM may remain alive after
its blocks become air, so its animation phase alone cannot reject a valid hit.
`enemy-hit-server1` and `enemy-hit-manager4` pass native nonfatal enemy hits,
masked slot isolation, consumed-shot duplicate rejection, open/closed connection
and logic Off checks, and exact whole-manager hit restore. `enemy-touch-server1`
passes native damage/knockback, invincibility, world-coordinate preservation,
bounded player output and exact restore. Live A011 -> 9FE5 shots now pass:
On spends four native super missiles, kills two armored Beetoms and opens
the far cap while Samus remains in A011; Off has no neighboring actors and
the starting beam opens that cap. Fresh-session current-build restore plus
90 continuing ticks passes both settings; On replay matches 250 checkpoints.
The rejected culling experiment is reverted: native armored Beetoms were
intercepting the starting beam. Live player contact and general actor/enemy-
projectile ownership remain unqualified.

Neighbor composition now retains active sprites with their source palette,
destination foreground priority and independent main/subscreen liquid math.
Resident sprites also render inside the native viewport during camera carry.
Portable/native/full-copy regressions and all 28 CTests pass. The rebuilt live
shot gate passes again with visible cross-door shots (`world-shots4-*`,
`renderer-shots-result.json`, exit 0); its current-build restores and all 250
replay checkpoints pass. Candidate executable SHA-256:
`0c1750b625b75638467d5a8449f514fb790b10bb0cd98fe04d93323a178b4bd0`.

Native enemy-projectile contact and shootable-projectile responses now run
after private projectile movement, through the same bounded player/weapon
loans and open-connection gates. The owned Chozo-orb probe passes all three
suit damage rules, native ordinary/plasma shot reactions, last-slot ownership,
masked-slot isolation, duplicate rejection, logic Off and exact restoration.
`projectile-contact5-result.json` passes the native runtime/manager probes.
The integrated build, all 28 CTests and the existing live shot/restore/replay
gate pass again (`projectile-integrated-result.json`, `world-shots5-*`).
This is response qualification; physical enemy-projectile crossing and live
cross-room projectile contact remain open. Current integrated executable:
`3b28c975d6f2fba5ee807f9b2a3cbca75d438012f992fdece8d6e3a98ff0c0e1`.

Enemy-projectile terrain samples now borrow resolved foreign cells for the
cartridge's own bounded collision response, restoring the original cell,
coordinates and dispatch index before RTS. Both generated active execution
and private resident execution use the same view. Native velocity, fractional
movement, slopes, wall reactions and slot ownership remain authoritative.
Owned-ROM probes pass all four signed allocation boundaries, foreign walls,
exact continuation and complete moving-pool equivalence for all 32 slope
shapes, four flips and four directions. Native open/closed/Off manager checks
also pass (`projectile-terrain1-result.json`).
Supported strict generation, the full build and all 28 CTests pass. A seeded
native Mini-Kraid spit is a physics prerequisite in the existing live fixture:
it actually moves from A011 into 9FE5, remains alive beyond x540, and dies on
foreign geometry in its original slot, with logic On and Off. Samus stays in
A011/gameplay. Its On replay matches all 16 checkpoints; the ordinary weapon
replay still matches 250 (`projectile-world-build1-result.json`, exit 0;
`world-shots6-*`, `world-eprojs1-*`). Current executable SHA-256:
`041a5afd1dfec601f3dd31402592649c960834b6403951c1e6ec45c5c15ae6c3`.
This does not establish completed actor movement, spatial ownership or
resident-owned objects crossing back onto the active canvas.

## Outstanding implementation and qualification

**Not release ready.** Ordinary right/left and down/up continuous round trips
are qualified with neighboring logic On and Off. Native inactive-room shots
and ordinary enemy weapon hits now pass the built live controller gate,
current-build restore and replay. Six acceptance areas remain open:

1. Extend the qualified regular-hatch traversal to exceptional geometry and
   broader foreign terrain. Three stock non-screen-edge records still use the
   original path. Qualify retained actors and entry scripts beyond the two
   current round trips. Player/beam coordinate continuity is now qualified for
   those cases; it does not establish general actor/projectile world physics.
2. Finish and live-qualify cross-open-door combat and projectile/actor transfer
   without duplicated hits/rewards. Native weapon, actor-contact and enemy-
   projectile responses and ordinary open/closed/Off routing now pass probes.
   Physical actor/enemy-projectile
   transfer, pickups/rewards, area weapons and complete ownership still need
   implementation. Walls and closed doors must block interaction.
3. Complete foreground/hatch continuation and dynamic PLM/item/door visuals;
   qualify liquids/raster waves, transition motion, intermediate high-FPS frames,
   Fit resizing and both HUD anchors.
4. Qualify live Ceres scenery/traversal and dormant encounter helpers. Its graph
   and tileset decoding now cover all six rooms; live gameplay is outstanding.
5. Qualify rewind, run-ahead, rollback, replay with transitions, two-peer
   determinism, representative native gameplay and matched runtime performance.
6. Prepare a compact owner review with normal audio in a normal window, then
   release via the established channel only after required acceptance. Verify
   the replacement before removing the superseded delivery ZIP.

## Local evidence and retained work

Workspace: `G:/Temp/Codex/supermetroid/exposed-rooms-20261105`.
`world-objects-source/` is a small separate worktree for prepared renderer
changes while the primary candidate stayed frozen. Its renderer checks use
`world-objects-build/`; these changes require ownership integration and live
qualification before acceptance. Remove the clean worktree/build after that
integration and retain only the needed evidence.
`source/` contains this isolated checkout; `build/` contains candidate tools
and the candidate game build. `first-preview.bmp` is the inspected offline
32:9 raster replay. `settled-hall/hall.capture`, four aspect images/logs and
`render-results.json` retain current-build visual evidence. `live-scenery` and
`baseline-live-scenery` retain matched gameplay state/diagnostic evidence.
Capacity records and exact build jobs are beside `source/`.
These are retained for continuing this feature, not delivered release files.
SERVER also retains the temporary
`G:/Temp/Codex/supermetroid/exposed-rooms-20261105-validation` workspace:
an exact pre-fix runtime, an owned-ROM copy and failed vertical diagnostics.
Its useful vertical4 evidence was copied back as `server-vertical4-on/`.
Remove that temporary remote workspace after retaining any remaining evidence
needed at final cleanup; it contains no player saves or owner playtest.
It also now contains `owned-shot-probe/`: a bounded 68 MB portable C compiler,
the exact probe sources, owned-ROM fixture and two passing native probes.
Clean that disposable compiler/source copy and its input ZIP at final cleanup;
the local 31 MB `owned-server-input.zip` can be removed once inputs are retained
in the worktree and source-hash record. The redundant local probe-only queue was
cancelled after the SERVER qualification; the full build has its own live queue.
The NES Metroid reference was checked in
`F:/Projects/nesrecomp/MetroidNESRecomp/metroid_ws_actors.c`: its
`guest_room_pair`, `collide_actor` and `commit_effects` provide the existing
pattern for world-space collision filtering followed by native responses and
bounded gameplay-effect transfer. Its retained native transitions do not meet
this task's additional continuous-traversal requirement. `ceres-live-1/`
retains a fresh combined-build Ceres boot and 180 advancing gameplay ticks;
its assisted hatch approach did not cross and is not traversal acceptance.
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

The build uses pinned engine/UI checkouts under `feedback-20261103` and the
supported, strictly regenerated task-local `gen-continuous/`. The shared older
generation tree was preserved; the continuous inside-body hooks changed the
bank-14 generation input.
The clean worktree's missing `recomp/funcs.h` was generated using the pinned
engine's supported sync tool. No generated C was edited.

Run owned-ROM probes by passing explicit cartridge paths to
`sm_room_preview_test`, and cartridge/WRAM/room-header arguments to
`sm_room_runtime_test`. They use assertions even in Release builds. Their
results do not replace live gameplay qualification.
