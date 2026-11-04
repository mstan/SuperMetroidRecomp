# Options tab: ROM glyph rendering (M2)

The pause menu's Options tab is a host overlay (see `sm_pause_options.h`), not a
recompiled screen. M1 gave it the panel model, geometry, SFX and fades. M2 makes
its text real Super Metroid text: the title and footer now draw the
cartridge's 8x8 small font, read from ROM in place, instead of the framework's
outline font. M2 also gives the panel the cartridge's own window frame (the
same BG2 edge tiles the Map and Equipment pause screens use) and the guest HUD
those screens keep along the top edge, so the Options tab reads as a third pause
screen rather than a separate host menu.

Nothing under `src/gen/` changed, no ROM patch was made, and no glyph, tile or
palette asset was copied out of the cartridge. The only new pixel data in the
repository is the synthetic font, frame and HUD patterns the unit test
generates for itself.

## What is drawn

`src/sm_rom_glyphs.c` owns the decode and the char -> tile index:

| characters | source |
| --- | --- |
| `A-Z` | small-font tiles `0x6a..0x83` (ASCII + 0x29) |
| `a-z` | same tiles, upper-cased (the font is caps-only) |
| `0-9` | small-font tiles `0x60..0x69` |
| space | blank cell, 8 px advance |
| `<` `>` | procedural 8x8 chevrons (the font has no such cells) |

Every other character advances blank. The overlay's strings only need the
first five rows of that table.

Only the panel chrome that mirrors the game's own small text uses the ROM
font: the GAME OPTIONS title and the `L < > R MOVE TABS` / `START EXIT` footer.
The dynamic settings rows (row labels and values supplied by the framework)
stay on the host font: they are host vocabulary, mixed case, and not ROM
strings.

### Font location and layout

The small font is in the pause data bank at **`$B6:$C000`**, which is ROM
offset **`0x1B4000`** (`SmRomByte` maps `bank & 0x7F`, so `$B6` is `$36`).
Tiles are **4bpp, 32 bytes each**:

```
+ 0..15   row 0..7 of planes 0/1 (2 bytes per row: plane 0 low, plane 1 high)
+16..31   row 0..7 of planes 2/3
```

A pixel is

```
bit = 7 - x
p = ((w0 >> bit) & 1) | (((w0 >> (bit+8)) & 1) << 1)
  | ((w1 >> bit) & 1) << 2 | (((w1 >> (bit+8)) & 1) << 3)
```

where `w0` is the plane-0/1 word and `w1` the plane-2/3 word for that row.
Pixel `0` is transparent. The font uses two tones: `14` (main) and `13`.
Tone `13` is a bottom-right shadow inside the tile (not a separate glyph
layer): in `A` it fills the inner right edge, in `T` the right side of the
stem. The overlay draws tone `14` in the string colour and tone `13` in a
darker shade of it (40% of each RGB channel). Painting both tones in the main
colour makes the shadow a white duplicate edge -- text that reads as doubled.
The shadow shade is host-derived, not read from CGRAM, and the shadow is
intrinsic to the glyph shape, so no duplicate draw call is involved.

The same A-Z bytes are also present in the copy the file-select screen loads
from ROM `0x70000`; the `$B6:$C000` copy was used so the pause data bank stays
the single source. Tiles `0x00..0x5f` in the `0x70000` copy are the *large*
8x16 font's top/bottom pairs (plus the kana sample used by the Japanese-text
option), not the small font. The small font is uppercase only.

The only non-ROM glyph shapes in the overlay are the `<` / `>` navigation
chevrons. They are generated in `sm_rom_glyphs.c`, not extracted from the ROM.

### Guest HUD

The Map and Equipment pause screens keep the gameplay HUD along the top edge.
The Options panel draws the same thing rather than a host-drawn tab strip: the
guest's BG3 tilemap at **`g_ram + 0xC608`** (VRAM word `0x5800`), 32x3 cells.
`HandleHudTilemap` keeps that copy current while paused, so energy, missiles,
reserves and weapon counts stay live without the panel recomputing them.

The HUD tiles are the 0x2000 bytes the pause loader DMAs from **`$9A:$B200`**
(ROM offset **`0xD3200`**) to VRAM byte `0x8000` (word `0x4000`, the BG3 tile
base selected by `BG34NBA = 4`). `BGMODE 9` is Mode 1, so **BG3 is 2bpp**:
16 bytes per tile, one word per row, planes 0/1:

```
p = ((row >> (7-x)) & 1) | (((row >> (8+(7-x))) & 1) << 1)
```

Tilemap entries decode as tile `entry & 0x3ff`, palette `(entry >> 10) & 7`,
priority `(entry >> 13) & 1` (ignored: the panel has one layer), hflip
`(entry >> 14) & 1` and vflip `(entry >> 15) & 1`. Tone 0 is transparent. A
2bpp palette is four CGRAM entries at `palette*4`:

* palette 3 (CGRAM 12-15): backdrop and digits;
* palette 5 (CGRAM 20-23): energy tanks and weapon icons.

The backdrop tile (`0x0f`) is solid tone 3, which palette 3 maps to black, so
the HUD paints its own opaque band over the panel background; the panel does
not add a host rectangle behind it. Every HUD byte (tile data, tilemap and
palette) is read in place at render time; nothing is extracted.

### Pause-window frame

The pause screens' window edge is in the same cartridge data bank. The base
BG2 tilemap is **`$B6:$E000`** (loaded to VRAM `0x3800` by
`LoadPauseScreenBaseTilemaps`); rows 5..24 hold the frame, and every frame cell
selects **palette 2**. The four shapes are in the BG2 tileset at **`$B6:$8000`**
(loaded to VRAM `0x0000`):

| tile | role | pixels |
| --- | --- | --- |
| `0xbe` | horizontal bar | a 4-line bevel across rows 2..5: gray, light gray, white, gray |
| `0xcf` | vertical bar | the same bevel in columns 2..5 |
| `0xbf` | diagonal corner | joins a bar and a side; flipped for the other three corners |
| `0xbd` | double-line cap | terminates the top edge either side of the area-label gap |

Palette 2 at **`$B6:$F000`** uses entries `4` = black, `13` = white,
`14` = light gray, `15` = gray. The renderer reads those sixteen BGR555 words
through `SmRomByte` at draw time and expands them to ARGB8888; no palette or
tile is stored in the repository.

`SmRomPauseFrameDraw` reproduces the native `256x160` frame at `y=40` (tilemap
rows 5..24):

* top row: corner `0xbf` (hflip), eight `0xbe`, cap `0xbd` (hflip), a 12-tile
  gap, cap `0xbd`, eight `0xbe`, corner `0xbf`;
* bottom row: the same cells with the corners/bars vflipped;
* sides: `0xcf` mirrored on the left, plain on the right, rows 6..23.

The gap is 12 cells (`x=80..175`), which is exactly `GAME OPTIONS` in the small
font, so the title sits in the top edge like the ROM's `BRINSTAR` area label.
The frame replaces the host-drawn rectangle that used to surround the panel.
The interior background is still the host `COL_BG`; the ROM's interior fill
tile (tilemap entry `0x0812`, a sparse dot pattern) is not drawn because the
panel's background is host-owned.

## Ground truth and how it was verified

The font layout was derived statically from the decompiled loaders
(`LoadPauseScreenBaseTilemaps`, the file-select text paths) and the ROM, then
checked against captured VRAM. The captures are a verification oracle only.

The HUD layout comes from the same decompiled pause path: `BGMODE 9` makes BG3
2bpp, the pause loader DMAs `$9A:B200` to word `0x4000`, and `BG3SC = 0x58`
puts the tilemap at word `0x5800`, matching the guest copy at `g_ram+0xC608`.
The published HUD palette layout (palette 0 entries 8-15, palette 1 entries
4-7) confirms that the 2bpp palettes select CGRAM `palette*4` and that palette
3 holds the black backdrop and digit colours while palette 5 holds the tank and
weapon-icon colours.

* `A-Z = tiles 0x6a..0x83` was confirmed byte-for-byte: the 32-byte tiles in
  captured VRAM equal the ROM bytes, and the tilemap cells spell the expected
  words (`DATA`, `SAMUS`, `EXIT`, `NODATA`, ...).
* `0-9 = tiles 0x60..0x69` was confirmed by the glyph shapes (0 has the closed
  loop, 1 the single stem, 7 the diagonal, ...).
* End-to-end check: the file-select screen's `NO DATA` row (BG1, base
  `0x5000`, nametable row 6, columns 15-21, framebuffer rows 47-54) was
  compared with the renderer's 4bpp mask. All 270 glyph pixels matched their
  CGRAM colours exactly: main -> `0x7fff` white, shadow -> `0x44e5`
  (expanded RGB `0x29398c`). Zero mismatches.

### Validated PPU model

The overlaid screen is plain tiled Mode 1 (`bgmode 0x01`), no Mode 7 or
affine work. The simulator that reproduced the captures uses:

* Nametable and pattern bases are **word** indices:
  `mapadr = (bgsc & 0xFC) << 8`, `tileadr = ((bgta >> (layer*4)) & 0xF) << 12`.
* Alternate-screen offsets are word units: `+0x800` for the higher half when
  the map is wide (`bgsc & 1`), else `+0x400`; wide second horizontal half
  `+0x400`.
* 4bpp decode as above; 2bpp uses one word per row, bits `7-px` and `15-px`.
* Background source line is `fb_row + 1 + vscroll` (line offset **1**);
  sprite line is `fb_row - y` with the wrap kept in `uint8`.
* Sprite size is square from `spriteSizes[size][bit]`,
  `bit = (highOam[idx>>3] >> ((idx&7)+1)) & 1`.
  Tile bases: `objTileAdr1 = (obsel & 7) << 13`,
  `objTileAdr2 = objTileAdr1 + (((obsel & 0x18) + 8) << 9)`.
  Sprite z is
  `0x80 + 16*((o1&0xE00)>>9) + (prio*4+2)*16 + 4 + (level6?2:0)` shifted into
  the priority byte; higher z wins.
* Composition: backdrop `0x0500` -> `cgram[0]`; Mode 1 precedence sprites,
  then BG3 (2bpp), BG2 (4bpp), BG1 (4bpp).
* `.bgrx` output is `B,G,R,X` with `expand8(v)=(v<<3)|(v>>2)` scaled by
  `inidisp & 0xF`. SNES CGRAM is `bbbbbgg gggrrrrr`: **low 5 bits are red**,
  not blue.

Captures reproduced exactly after removing a simulator bug that treated tile
`0x0000` as blank (it is a valid tile; the blank background tile is `0x000f`):

```
option     0/57344 (0.000%)
samusdata  0/57344 (0.000%)
opt_a1     0/57344 (0.000%)
opt_a2     0/57344 (0.000%)
opt_s1     0/57344 (0.000%)
opt_s2     0/57344 (0.000%)
opt_s3     0/57344 (0.000%)
```

The scratch simulator used for that check lives at `/tmp/opencode/simmodes.py`
in the working environment; it is not part of the build. Run
`python3 simmodes.py <tag>` for any dump tag under `/tmp/opencode/dump`.

## Capture-phase caveat

The reference captures were sampled mid-vblank (vpos 257). At that point the
framebuffer is the **completed raster of the previous frame**, while
VRAM/CGRAM/OAM and the register snapshot may already include the vblank DMA
writes the guest's NMI issued for the next frame. In other words, the pixels
and the accompanying tile/register state are not guaranteed to be the exact
state that produced those pixels.

That is why the font layout and decode above are derived from the decompiled
loader and the ROM, not inferred from a single capture. When using a capture as
a cross-check, compare **glyph pixels against the palette colours** (as the
`NO DATA` check does) rather than assuming the captured VRAM is the frame's
source state. Also do not mix up the two screens:

| capture | use | bgmode | bgsc | tm | obsel |
| --- | --- | --- | --- | --- | --- |
| `option`, `samusdata` | file-select options / file data | `0x01` | `0x51,0x58,0x5C` | `0x13` | 3 |
| `opt_a1..` | controller-settings screen | `0x09` | `0x50,0x48,0x4C` | `0x04` (BG3 only) | 1 |

The in-game pause screen is a different screen again (`bgmode 9`, bgsc
`0x31/0x38/0x58`, tm `0x17`).

## Turbo rendering

`host_main.c:4342`:

```c
g_snes->disableRender = g_turbo && (frameCtr & 0xf) != 0;
```

During turbo only every 16th frame renders (frames where `frameCtr % 16 == 0`);
the other 15 skip the SNES PPU render. Any capture or scene dump taken while
turbo is on can therefore show a stale framebuffer or skipped PPU updates. The
host overlay is drawn after the guest frame and is not itself gated by
`disableRender`, but capture-based comparisons must be taken with turbo off.
The unit test does not use turbo; the caveat is documentation for future
runtime capture work.

## Tests

`tests/sm_pause_options_test.c` supplies a synthetic 4bpp font at
`$B6:C000 + tile*32` (offset `0x4000` in its `$B6:8000..FFFF` window). It also
generates the four frame tiles (`0xbd/0xbe/0xbf/0xcf` at offset `0`) and
palette 2 (at offset `0x7000 + 64`), so the frame's tile decode, flips and
palette lookup are covered by the goldens rather than silently transparent.
A synthetic `$9A` window provides all 512 HUD tiles at 16-byte 2bpp stride
(offset `0x3200`), and the full 16x16-entry palette set at `$B6:F000` covers
both the frame's 16-colour palette and the HUD's 4-colour palettes. The test
also seeds the guest BG3 tilemap (`g_ram+0xC608`) with a backdrop, energy
tanks, digits, weapon counts and flipped cells, and asserts that the HUD band
paints pixels above `y=24` and stops there. The patterns are generated, not
extracted, and differ per tile so a wrong char -> tile map, HUD tile stride,
flip or plane/row mix-up changes the golden hash.

The panel's state model was also corrected for the no-pin design: while the
guest is paused it stays open on whichever pause screen it is on (Map or
Equipment) and through a tab-change or unpause fade; it closes when the fade
lands on a Load state or the game leaves the paused states. Opening on
Equipment now leaves `menu_index` on Equipment (the panel no longer pins the
guest to Map first).

Build and test:

```sh
cmake -S . -B build -DPAUSE_OPTIONS_TEST_CALIBRATE=OFF
cmake --build build --target sm_pause_options_test -j
./build/sm_pause_options_test
ctest --test-dir build --output-on-failure
```

The panel has 26 golden hashes (at 256, 342, 456 and 684 px widths). After a
glyph, layout or colour change, regenerate them:

```sh
cmake -S . -B build -DPAUSE_OPTIONS_TEST_CALIBRATE=ON
cmake --build build --target sm_pause_options_test -j
./build/sm_pause_options_test          # prints hash N ... actual=0x...
```

Replace the 26 `HashCheck` constants with the printed `actual` values in
order, then reconfigure with the option `OFF` and rerun. The test asserts
`g_frame[0] == 0xFF04060B` while leaving at `inidisp 7` (the panel background
`0x0A0E18` scaled by 7/15) and all-black at `inidisp 0x80`.

Current result: all 10 CTest tests pass, including `sm_pause_options`.

## Files

* `src/sm_rom_glyphs.c` / `.h` - 4bpp font/frame decode, 2bpp HUD decode,
  char -> tile map, chevrons, pause-window 9-slice, flat pause CGRAM.
* `src/sm_pause_options.c` - calls the renderer; HUD band, frame layout;
  stay-open model; `COL_BG` background.
* `tests/sm_pause_options_test.c` - synthetic 4bpp font and frame tiles,
  synthetic 2bpp HUD tiles and tilemap, no-pin expectations, 26 recalibrated
  hashes.
* `CMakeLists.txt` - builds `sm_rom_glyphs.c` into the test and the
  `PAUSE_OPTIONS_TEST_CALIBRATE` switch.
