# Super Metroid renderer validation, 2026-10-28

The renderer corrections at this checkpoint address three independently tested
behaviors:

- Continue a uniform authored wall through an adjoining unused screen while
  retaining conservative handling for mixed or conflicting terrain.
- Decode valid saturated offscreen HDMA window runs and use their unclipped
  geometry in widened margins. Preserve exact 4:3 windows and real cone pixels.
- Permit the liquid surface row preceding VRAM page `$5C00` when the native FX
  tilemap is active, while retaining opaque HUD margins.

Commit `2511720` also removes a false vertical stripe at the native x255 edge
when the eye-cone HDMA walker saturates beyond the visible screen.

The checkpoint passed 18 tests, including portable, native and full-copy renderer
paths. Focused cases cover mirrored saturated window tails at four widths,
active/inactive liquid rows, uniform wall continuation and mixed-screen
preservation. These results describe the tested rendering behavior and do not
establish complete game-content coverage.
