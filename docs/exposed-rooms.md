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
native room transitions and native saves. Current-build save/load, rewind,
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
  excluded from visual acceptance despite zero native-pixel mismatches. A
  settled current-build capture is required for that visual check.

## Outstanding implementation and qualification

The runtime prototype is **not connected to live gameplay**. There is no
shipping neighbor-logic toggle yet. Do not describe this branch as complete or
release it as the requested feature.

1. Connect resident lifetime/update/publication to simulation boundaries,
   preserving immutable renderer inputs. Publish native actor graphics and
   native animated FX into each room's own rendering resources. Initialize
   palette/graphics, collision and PLMs from the destination room; do not
   inherit active-room visual or script residue.
2. Implement paired door ownership, native shot requirements, both-face hit
   detection, shared animation/unlock state and native transition handoff.
3. Implement cross-open-door combat, projectile/actor state handoff and native
   respawn rules without duplicated actors, hits or rewards. Qualify ordinary
   AI populations and preserve entry-only encounters/scripts.
4. Add the real default-On persisted option to the built-in Widescreen
   provider. Apply on room load. Off must preserve working FX and doors.
5. Include resident lifetime/state and effective setting in the game's
   current-build snapshot/rollback/replay/netplay contracts. The prototype's
   pointer-free resident serializer is only a component of that work.
6. Complete foreground/hatch continuation, dynamic PLM/item/door visuals,
   transition motion, liquids, intermediate high-FPS frames, all aspects/Fit
   resizing and both HUD anchors. The former no-preview room `$A641` is
   resolved: its Kraid-room destination overlaps authored allocation, requiring
   clipping at the door plane rather than rejection of the destination.
7. Complete built runtime, meaningful regression, persistence and matched
   performance gates. Prepare a compact owner review with normal audio in
   a normal window. After acceptance, release through the established channel,
   verify packages, and replace the superseded delivery ZIP only then.

## Local evidence and retained work

Workspace: `G:/Temp/Codex/supermetroid/exposed-rooms-20261105`.
`source/` contains this isolated checkout; `build/` contains candidate tools
and the candidate game build. `first-preview.bmp` is the inspected offline
32:9 raster replay. Capacity records and exact build jobs are beside `source/`.
These are retained for continuing this feature, not delivered release files.
No player save, settings, release archive or running playtest was replaced.

The build reuses the existing verified generated translation units and pinned
engine/UI checkouts under `feedback-20261103`; generation inputs are unchanged.
The clean worktree's missing `recomp/funcs.h` was generated using the pinned
engine's supported sync tool. No generated C was edited.

Run owned-ROM probes by passing explicit cartridge paths to
`sm_room_preview_test`, and cartridge/WRAM/room-header arguments to
`sm_room_runtime_test`. They use assertions even in Release builds. Their
results do not replace live gameplay qualification.
