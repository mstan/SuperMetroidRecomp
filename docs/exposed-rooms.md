# Exposed widescreen rooms

## Behavior

Connected rooms retain their own terrain, graphics, palettes, items and animated
environmental effects. Suitable solid foreground continues into exposed margins;
authored air, passages and connected canvases remain visible. Door bubbles track
native passability, weapon requirements and progression locks.

Neighboring room logic defaults On. Off suppresses neighboring gameplay actors
while retaining scenery, environmental animation and door interaction. Bosses,
encounter locks and progression helpers start through actual room entry.
Continuous traversal is the default. Native transitions retain their scrolling
and body animation while complete room images remain available to presentation.

## Architecture

`sm_rooms.c` derives placements from the cartridge's reciprocal door definitions.
Resident rooms own immutable terrain and presentation resources. The private
runtime advances native enemy and environmental routines through an isolated
WRAM/MMIO bus; it does not install the active machine's bus, audio or SRAM.

`sm_seamless.c` loads the destination through native room setup and keeps native
camera coordinates separate from presentation-world coordinates. Admission
clearance derives from the actual incoming door and player bounds. It does not
rewrite locks, terrain or progression results.

`sm_renderer.c` composes room-owned canvases, actors and environmental effects.
A complete departing image survives native loader phases so partially replaced
VRAM and palettes do not become the visible world. The renderer handles the
native body and camera stages separately. Foreground continuation recognizes
solid padding by its tile definition and respects real air and portal ownership.
Overlapping unused allocations retain the active room's foreground texture;
authored connected scenery still takes precedence. Native camera placement stays
with the departing room until scrolling setup rebases it. Liquid surface texels
stop at solid banks while the liquid body keeps its native raster behavior.

Hatch promotion requests a native column redraw when copied cells change. This
keeps the visible cap synchronized after its opening PLM has completed.

## State and verification

Scene format v4 includes the optional native-transition root image. Manager
state v6 owns that image when present and rejects truncated or invalid input.
Current-build save/load, replay, rewind, run-ahead, rollback and netplay must
preserve the complete resident state and effective settings. Old snapshot
compatibility is not required; native saves remain a separate contract.

The regression suite covers hatch redraw in four orientations, door ammunition
and locks, incoming-frame clearance, exact manager restore, coherent native
transition resources and foreground continuation through equivalent solid tile
definitions. Renderer coverage includes portable, native and full-copy paths.
Owned-ROM checks supplement synthetic tests with real room geometry and native
runtime behavior. Integration validation is still in progress.

Use the supported regeneration pipeline, pinned dependencies and existing QA
interfaces. Keep ROMs, generated C and runtime captures outside the repository.
Fresh release packages use adaptive Fit and retain explicit saved settings.
