/* sm_rom_glyphs.h — M2 ROM glyph and chrome renderer (hand-written, NOT
 * src/gen). The small-font tiles stay in the cartridge at $B6:$C000, the
 * pause-window tiles/tilemap/palette at $B6:$8000 / $B6:$E000 / $B6:$F000;
 * this module reads them through SmRomByte at render time. No glyph, tile,
 * tilemap or palette bitmap is extracted. */
#pragma once
#include <stdint.h>

/* Draw an 8x8-per-cell string using the ROM's 4bpp small font (A-Z, 0-9).
 * '<' and '>' are host-drawn chevrons (the font has no such tiles); space and
 * unsupported characters advance blank. Pixel value 0 is transparent, 14 is
 * the main tone and 13 is the font's shadow, drawn in a darker shade of
 * `color` so the glyph does not read as doubled.
 * dst/stride/width/height: output framebuffer (ARGB8888) and dimensions.
 * x,y: top-left destination position. text: null-terminated ASCII string.
 * color: ARGB color for opaque glyph pixels. scale: pixel multiplier
 * (1 = 8×8 native, 2 = 16×16, etc.). */
void SmRomGlyphDraw(uint32_t *dst, int stride, int width, int height,
                    int x, int y, const char *text, uint32_t color, int scale);

/* Draw the ROM pause-window frame as a 9-slice of the cartridge's BG2 tiles
 * from $B6:8000 (0xbd/0xbe/0xbf/0xcf), with palette 2 read from $B6:F000 at
 * render time. No tile or palette data is copied out of the ROM.
 * dst/stride/width/height: output framebuffer (ARGB8888) and dimensions.
 * x,y,w,h: frame rectangle; w and h should be multiples of 8 (native size is
 * 256x160 at y=40 in the pause screen).
 * gap_col/gap_tiles: omit that run of top-edge cells for a label; the gap is
 * capped with tile 0xbd at each end. Pass gap_tiles = 0 for an unbroken edge. */
void SmRomPauseFrameDraw(uint32_t *dst, int stride, int width, int height,
                         int x, int y, int w, int h,
                         int gap_col, int gap_tiles);

/* Draw the pause screen's bottom tab strip: BG2 tilemap rows 25-26 (the 128-
 * byte footer UpdatePauseMenuLRStartVramTilemap DMAs to VRAM), 32 columns wide
 * and 16 px tall, read in place from $B6:E000. Each entry supplies its own
 * tile, palette (16 CGRAM entries from $B6:F000) and h/v flips, so the tab
 * labels, arrows and framing are the cartridge's art, not host-drawn glyphs.
 * dst/stride/width/height: output framebuffer (ARGB8888) and dimensions.
 * x,y: top-left destination of the strip (it spans 256 px wide, 16 px tall). */
void SmRomPauseFooterDraw(uint32_t *dst, int stride, int width, int height,
                          int x, int y);

/* Update the guest's pause footer tilemap copy at g_ram+0x3000 (rows 25-26)
 * from the ROM tilemap at $B6:E640/$B6:E680. Called when the Options panel
 * opens or changes tabs so the tab strip shows the correct labels for the
 * current tab state. The guest's own pause routine keeps it current between
 * frames, including palette animations (glow/pulse). */
void SmRomPauseFooterUpdate(void);

/* Repaint the L/R navigation buttons' gray backdrop (footer cols 1-4 and
 * 27-30 of the $B6:8000 tileset, read in place): the pill body (tile tone
 * 15) gets `body_color` and its rim (tile tone 14) gets `rim_color`, leaving
 * the arrow glyph and every other footer cell alone. The host's stand-in for
 * the game's per-frame button palette animation, which the panel composites
 * over.
 * dst/stride/width/height: output framebuffer (ARGB8888) and dimensions.
 * x,y: top-left destination of the strip, as in SmRomPauseFooterDraw. */
void SmRomPauseFooterGlowDraw(uint32_t *dst, int stride, int width, int height,
                              int x, int y, uint32_t body_color, uint32_t rim_color);

/* Draw the guest's pause-screen HUD from its BG3 tilemap (32 columns x 3 rows;
 * `cells` points at the guest copy in WRAM). The Map and Equipment pause
 * screens show this same HUD along the top edge. BG3 is 2bpp in Mode 1, so
 * tile graphics are the cartridge's 16-byte HUD BG tileset at $9A:$B200 and
 * colours are the pause palette set at $B6:$F000, both read in place at render
 * time. Tilemap entries supply the tile index, palette (four CGRAM entries at
 * palette*4), priority (ignored here; the panel has one layer), and h/v flips;
 * tone 0 is transparent.
 * dst/stride/width/height: output framebuffer (ARGB8888) and dimensions.
 * x,y: top-left destination position. cols/rows: tilemap dimensions. */
void SmRomHudDraw(uint32_t *dst, int stride, int width, int height,
                  int x, int y, const uint16_t *cells, int cols, int rows);
void SmRomHudTopRowDraw(uint32_t *dst, int stride, int width, int height,
                        int x, int y);
