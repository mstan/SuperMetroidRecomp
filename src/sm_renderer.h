#pragma once
#include "sm_video.h"
#include "snes/ppu.h"
#include "snes/hd_jobs.h"
void SmRendererSetHdEnabled(bool enabled);
const SnesHdFrame *SmRendererHdFrame(void);

void SmRendererReset(void);
void SmRendererSetRom(const uint8_t *rom, size_t size);
/* Call immediately before the next guest frame/NMI publishes OAM. */
void SmRendererLatchObjectState(const uint8_t ram[0x20000]);
void SmRendererBeginFrame(const uint8_t ram[0x20000], unsigned number);
bool SmRendererClaimNativeLine(const Ppu *p, unsigned line);
unsigned SmRendererNativeLines(void);
void SmRendererCaptureLine(const Ppu *ppu, unsigned line);
void SmRendererBeginHardwareRaster(const Ppu *ppu);
void SmRendererEndHardwareRaster(void);
void SmRendererHardwareVramChanged(void);
bool SmRendererEndFrame(const uint32_t stock[256 * 224]);
bool SmRendererDraw(uint32_t *output, SmViewport viewport, bool hud_anchored, double alpha);
bool SmRendererRoomTile(const uint8_t ram[0x20000], int x, int y, uint16_t *entry);
bool SmRendererRoomLayerTile(const uint8_t ram[0x20000], unsigned layer,
                            int x, int y, uint16_t *entry);
/* Read-only $88:8CC6/8D04/8D46 profile calculation, before native clipping.
 * Index is absolute distance in the 192-row half-mask. -1 means empty.
 * Invalid profiles/radii leave output untouched. */
bool SmRendererPowerBombWidths(const uint8_t profile[160], unsigned radius,
                              int16_t widths[192]);
/* Unclipped X-ray cone from the retail Q8 tangent table (X=-256..511).
 * Empty rows have left > right. Horizontal rays use +/-32768 sentinels. */
bool SmRendererXrayBounds(const uint16_t tangent[129], int x, int y,
                          unsigned angle, unsigned half_width,
                          int32_t left[230], int32_t right[230]);
/* Host reveal block map: room width*height entries. 0x1000 marks a
 * replacement (quadrant swaps without tile-pixel flips). */
bool SmRendererXrayBlocks(const uint8_t ram[0x20000], uint16_t blocks[0x3200]);
typedef struct SmGrapplePiece { int x, y; uint16_t attr; bool endpoint; } SmGrapplePiece;
unsigned SmRendererGrapplePieces(const uint8_t ram[0x20000], SmGrapplePiece pieces[17]);
/* Versioned local diagnostics; never loaded into guest memory. */
bool SmRendererSaveCapture(const char *path);
bool SmRendererLoadCapture(const char *path);
const uint32_t *SmRendererStockFrame(void);
typedef struct SmRendererStats {
  unsigned matched_object_samples;
  unsigned matched_samus_samples;
  unsigned matched_effect_samples;
  unsigned recovered_edge_pixels;
  unsigned interpolated_scroll_lines;
  unsigned custom_lines, stock_lines, hud_lines;
  unsigned mode7_lines;
  unsigned power_bomb_lines;
  unsigned xray_lines;
  unsigned eye_beam_lines;
  unsigned xray_tiles_compared, xray_tiles_differ;
  unsigned grapple_native_matches, grapple_margin_pixels;
  unsigned grapple_interpolated_pieces;
  unsigned grapple_flare_native_matches;
  unsigned native_center_lines;
} SmRendererStats;
SmRendererStats SmRendererGetStats(void);
typedef struct SmRasterStats {
  uint64_t vram_copied_bytes, vram_capacity_bytes, metadata_bytes, vram_compared_bytes;
  unsigned vram_images;
} SmRasterStats;
SmRasterStats SmRendererGetRasterStats(void);
