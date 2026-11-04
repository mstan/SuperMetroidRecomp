/* super-metroid.map-hud: host-drawn readouts (presentation only; they read
 * guest WRAM and never write it).
 *
 * pause-stats: the pause screens show play time and item percentage, as in
 * Super Metroid Redux (counters by PHOSPHOTiDYL). The percentage is the
 * game's own ending formula, CalcItemPercentageCount ($8B:E627): energy and
 * reserve capacity / 100, missile/super/power-bomb capacity / 5, plus one per
 * collected item and beam bit in its tables ($8B:E721, $8B:E737).
 *
 * ammo-totals: maximum missile, super missile and power bomb capacity,
 * on the pause screens. Gameplay already has native ammo counters.
 */

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "mod_runtime.h"
#include "sm_mod_overlay.h"
#include "sm_mod_support.h"
#include "sm_map_plugin.h"
#include "snes_overlay_draw.h"

#define PACKAGE "super-metroid.map-hud"

enum {
  kInidisp = 0x0051, kGameState = 0x0998,
  kCollectedItems = 0x09A4, kCollectedBeams = 0x09A8,
  kEnergyMax = 0x09C4, kMissiles = 0x09C6, kMissilesMax = 0x09C8,
  kSupers = 0x09CA, kSupersMax = 0x09CC, kPowerBombs = 0x09CE, kPowerBombsMax = 0x09D0,
  kReserveMax = 0x09D4,
  kTimeSeconds = 0x09DC, kTimeMinutes = 0x09DE, kTimeHours = 0x09E0,
  kStatePaused = 0x0F,
};

static uint16_t Word(const uint8_t *wram, uint16_t addr) {
  return (uint16_t)(wram[addr] | wram[addr + 1] << 8);
}

/* Fully faded in: not forced blank and at full brightness. */
static bool ScreenUp(const uint8_t *wram) {
  return !(wram[kInidisp] & 0x80) && (wram[kInidisp] & 0x0F) == 0x0F;
}
static bool Paused(const uint8_t *wram) {
  return Word(wram, kGameState) == kStatePaused && ScreenUp(wram);
}

static int PopCount(uint16_t v) {
  int n = 0;
  for (; v; v &= (uint16_t)(v - 1)) ++n;
  return n;
}

int SmModItemPercent(const uint8_t *wram) {
  static const uint16_t kCapacity[5][2] = {
    {kEnergyMax, 100}, {kReserveMax, 100}, {kMissilesMax, 5}, {kSupersMax, 5}, {kPowerBombsMax, 5},
  };
  static const uint16_t kItemBits = 0x1 | 0x20 | 0x4 | 0x1000 | 0x2 | 0x8 | 0x100 | 0x200 |
                                    0x2000 | 0x4000 | 0x8000;
  static const uint16_t kBeamBits = 0x1 | 0x2 | 0x4 | 0x8 | 0x1000;
  int count = 0;
  for (int i = 0; i < 5; ++i) count += Word(wram, kCapacity[i][0]) / kCapacity[i][1];
  count += PopCount(Word(wram, kCollectedItems) & kItemBits);
  count += PopCount(Word(wram, kCollectedBeams) & kBeamBits);
  return count;
}

static void Panel(uint32_t *pixels, int stride, int height, int x, int y, const char *text) {
  int w = (int)strlen(text) * 8 + 6;
  snes_ovl_fill_rect(pixels, stride, height, x, y, w, 12, 0xFF000818u);
  snes_ovl_stroke_rect(pixels, stride, height, x, y, w, 12, 0xFF3060A0u);
  snes_ovl_draw_text(pixels, stride, height, x + 3, y + 2, text, 0xFFE8F0FFu, 1);
}

/* ── pause-stats ─────────────────────────────────────────────────────── */

static void SmPauseStatsDraw(uint32_t *pixels, int stride, int width, int height,
                             int stage_x, const uint8_t *wram) {
  (void)width;
  if (!Paused(wram) || SmMapFullScreen(wram)) return;
  char text[48];
  snprintf(text, sizeof(text), "TIME %02u:%02u:%02u ITEMS %d%%",
           (unsigned)Word(wram, kTimeHours), (unsigned)Word(wram, kTimeMinutes),
           (unsigned)Word(wram, kTimeSeconds), SmModItemPercent(wram));
  /* The bottom rows, below the pause screens' L/R button bar. */
  Panel(pixels, stride, height, stage_x + 8, height - 12, text);
}

static void SmPauseStatsActivate(void) {
  SmModOverlayRegisterPriority(SmPauseStatsDraw, 20);
  fprintf(stderr, "[mods] pause time and item percentage\n");
}

/* ── ammo-totals ─────────────────────────────────────────────────────── */

static void SmAmmoTotalsDraw(uint32_t *pixels, int stride, int width, int height,
                             int stage_x, const uint8_t *wram) {
  (void)width;
  if (!Paused(wram) || SmMapFullScreen(wram)) return;
  char text[64];
  snprintf(text, sizeof(text), "MAX M %u S %u P %u",
           (unsigned)Word(wram, kMissilesMax), (unsigned)Word(wram, kSupersMax),
           (unsigned)Word(wram, kPowerBombsMax));
  /* Above the pause screen's L/R button bar. The native gameplay HUD is kept. */
  Panel(pixels, stride, height, stage_x + 8, height - 42, text);
}

static void SmAmmoTotalsActivate(void) {
  SmModOverlayRegisterPriority(SmAmmoTotalsDraw, 20);
  fprintf(stderr, "[mods] pause ammo capacity\n");
}

SNES_MOD_CONSTRUCTOR(sm_register_hud_plugins) {
  snes_mod_register_presentation_plugin("super-metroid.map-hud.pause-stats",
                                        SmPauseStatsActivate);
  snes_mod_register_presentation_plugin("super-metroid.map-hud.ammo-totals",
                                        SmAmmoTotalsActivate);
}
