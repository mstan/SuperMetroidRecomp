/* super-metroid.pacing / screen-shake: reduce earthquake shaking.
 *
 * Earthquakes (earthquake_type $183E, earthquake_timer $1840) offset the
 * screen in two stock tables, indexed by type:
 *   HandleRoomShaking ($A0:8687) adds kRoomShakes ($A0:872D, 36 x 4 words:
 *     BG1 X/Y, BG2 X/Y) to the BG scroll registers, flipping sign every
 *     other frame;
 *   GetValuesForScreenShaking ($86:8427) reads kScreenShakeOffsets
 *     ($86:846B, 36 x 2 words) so enemy projectiles drawn on the background
 *     move with it.
 * Both tables hold 0..3 px. "half" rewrites each entry to ceil(v / 2)
 * (3 -> 2, 2 -> 1, 1 -> 1); "off" zeroes them and also raises the
 * CMP #$0012 at $A0:8705 to #$7FFF, so strong quakes (type >= $12) no longer
 * jitter every enemy sprite 1 px through SetAllEnemiesToShakeFor2Frames
 * (`live_operand` in recomp/bank20.cfg). Timers and types are untouched, so
 * every quake still lasts and triggers exactly what it does in the original;
 * only what is drawn changes. The tables are data, read through the bus from
 * the patched ROM image by compiled and interpreted code alike.
 */

#include <stdio.h>
#include <string.h>

#include "mod_runtime.h"
#include "sm_mod_support.h"

#define PACKAGE "super-metroid.pacing"
#define FEATURE "screen-shake"

static const uint8_t kRoomShakesStock[144] = {
  1, 0, 0, 0, 0, 1, 0, 0, 1, 1, 0, 0, 2, 0, 0, 0,
  0, 2, 0, 0, 2, 2, 0, 0, 3, 0, 0, 0, 0, 3, 0, 0,
  3, 3, 0, 0, 1, 0, 1, 0, 0, 1, 0, 1, 1, 1, 1, 1,
  2, 0, 2, 0, 0, 2, 0, 2, 2, 2, 2, 2, 3, 0, 3, 0,
  0, 3, 0, 3, 3, 3, 3, 3, 1, 0, 1, 0, 0, 1, 0, 1,
  1, 1, 1, 1, 2, 0, 2, 0, 0, 2, 0, 2, 2, 2, 2, 2,
  3, 0, 3, 0, 0, 3, 0, 3, 3, 3, 3, 3, 0, 0, 1, 0,
  0, 0, 0, 1, 0, 0, 1, 1, 0, 0, 2, 0, 0, 0, 0, 2,
  0, 0, 2, 2, 0, 0, 3, 0, 0, 0, 0, 3, 0, 0, 3, 3,
};
static const uint8_t kEprojShakesStock[72] = {
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 1, 0, 0, 1, 1, 1, 2, 0, 0, 2, 2, 2,
  3, 0, 0, 3, 3, 3, 1, 0, 0, 1, 1, 1, 2, 0, 0, 2,
  2, 2, 3, 0, 0, 3, 3, 3,
};

/* Patch `count` little-endian words at `addr`, in pieces that fit one
 * guarded patch. */
static size_t PatchWords(uint32_t addr, const uint8_t *stock_words,
                         size_t count, bool off, size_t *pieces) {
  enum { kWordsPerPiece = 72 };
  size_t applied = 0;
  for (size_t first = 0; first < count; first += kWordsPerPiece) {
    size_t n = count - first < kWordsPerPiece ? count - first : kWordsPerPiece;
    uint8_t stock[2 * kWordsPerPiece], value[2 * kWordsPerPiece];
    for (size_t i = 0; i < n; ++i) {
      uint8_t v = stock_words[first + i];
      stock[2 * i] = v;
      stock[2 * i + 1] = 0;
      value[2 * i] = off ? 0 : (uint8_t)((v + 1) >> 1);
      value[2 * i + 1] = 0;
    }
    applied += SmModRomPatch("screen-shake", addr + 2 * (uint32_t)first,
                             stock, value, 2 * n);
    ++*pieces;
  }
  return applied;
}

static void SmScreenShakeActivate(void) {
  const char *level = SmModOption(PACKAGE, FEATURE, "shake", "half");
  bool off = !strcmp(level, "off");
  size_t pieces = 0, applied = 0;
  applied += PatchWords(0xA0872D, kRoomShakesStock, 144, off, &pieces);
  applied += PatchWords(0x86846B, kEprojShakesStock, 72, off, &pieces);
  if (off) {
    applied += SmModPatchImm16("screen-shake", 0xA08705, 0x0012, 0x7FFF);
    ++pieces;
  }
  fprintf(stderr, "[mods] screen shake %s: %zu/%zu patches\n", level, applied, pieces);
}

SNES_MOD_CONSTRUCTOR(sm_register_screen_shake_plugin) {
  snes_mod_register_activation_plugin("super-metroid.pacing.screen-shake",
                                      SmScreenShakeActivate);
}
