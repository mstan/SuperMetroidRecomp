/* super-metroid.pacing / doors: faster door transitions.
 *
 * Inspired by Project Base (Begrimed), Super Metroid Redux and Ancient
 * Chozo, reimplemented on the stock routines:
 *
 * Scrolling. The IRQ-driven DoorTransition_Right/Left ($80:AE7E/$AEC2) move
 * both layers 4 px a frame for 64 frames (CPX #$40); DoorTransition_Down/Up
 * ($80:AF02/$AF89) move 4 px for 56 frames after a setup frame (CPX #$39),
 * Up skipping map updates while it covers the 16 px row FixDoorsMovingUp
 * already drew (CPX #$05). The step is raised and every count shrinks with
 * it, so the scroll ends at the identical position. These sites are
 * `mod_operand` in recomp/bank00.cfg.
 *
 * Step limit. UpdateBgGraphicsWhenScrolling uploads one block row/column
 * per call, and the setup leaves the "previous block" one behind the
 * 16-px-aligned start so the first step draws the first new row/column. A
 * first step of a full 16 px therefore skips one (measured: a stale BG1 row
 * after down/up doors and a garbage left column after right doors). Steps
 * must stay below 16 px: 2x uses 8 px everywhere (32 / 28 frames); max uses
 * 8 px horizontally (256 px has no larger divisor below 16), 14 px down
 * (16 frames) and 8 px up: at 14 px DoorTransition_Up's skipped-update
 * start misplaced the new room's rows by one block (measured), so up doors
 * stay at the verified 8 px.
 *
 * Samus. LoadDoorHeader ($82:DE12) builds her per-frame door speed as
 * distance << 8 in $12-$14 and stores it to $092B/$092D at $82:DE64; a
 * mod_hook there scales it by the scroll factor (2, or 3.5 down at max), so she covers the stock distance in the shorter scroll. A second
 * hook in DoorTransitionFunction_PlaceSamusLoadTiles ($82:E3E5) removes the
 * larger first scroll step from her placement, so she lands exactly where
 * she does in the original.
 *
 * Fades. AdvancePaletteFadeForAllPalettes_0xc ($82:D961), used only by
 * DoorTransitionFunction_FadeOutScreen and DoorTransition_FadeInScreenAndFinish,
 * fades in 12 + 3 frames (LDA #$000C); 2x uses 5, max uses 2 (`mod_operand`
 * in recomp/bank02.cfg).
 */

#include <stdio.h>
#include <string.h>

#include "mod_hook.h"
#include "mod_runtime.h"
#include "sm_mod_support.h"

#define PACKAGE "super-metroid.pacing"
#define FEATURE "doors"

/* Scroll step in px per axis for the active option. */
static unsigned g_step_h = 4, g_step_down = 4, g_step_up = 4;

enum { kDoorDirection = 0x791 };

/* LoadDoorHeader, before $12-$14 is stored as Samus' door speed: scale it
 * by step / 4 (exact: the value is a multiple of 256). */
uint32 SmDoorSamusSpeedHook(CpuState *cpu, uint32 pc24) {
  (void)pc24;
  uint8_t *r = cpu->ram;
  unsigned dir = r[kDoorDirection] & 3;
  unsigned step = dir == 2 ? g_step_down : dir == 3 ? g_step_up : g_step_h;
  uint32_t v = r[0x12] | r[0x13] << 8 | (uint32_t)r[0x14] << 16 | (uint32_t)r[0x15] << 24;
  v = v / 4 * step;
  r[0x12] = (uint8_t)v;
  r[0x13] = (uint8_t)(v >> 8);
  r[0x14] = (uint8_t)(v >> 16);
  r[0x15] = (uint8_t)(v >> 24);
  return 0;
}

/* DoorTransitionFunction_PlaceSamusLoadTiles ($82:E3C0) rebuilds Samus'
 * room position as layer1 + her screen offset. DoorTransitionScrollingSetup
 * has already run one scroll step for right, left and up doors (down's first
 * call is a setup frame), so the stock placement includes 4 px of scroll and
 * a faster one includes `step` px: Samus would end step - 4 px further
 * along. Runs after both stores ($82:E3E5) and takes that back. */
uint32 SmDoorPlaceSamusHook(CpuState *cpu, uint32 pc24) {
  (void)pc24;
  uint8_t *r = cpu->ram;
  int extra;
  uint32_t pos, prev;
  switch (r[kDoorDirection] & 3) {
    case 0: extra = 4 - (int)g_step_h; pos = 0xAF6; prev = 0xB10; break; /* right */
    case 1: extra = (int)g_step_h - 4; pos = 0xAF6; prev = 0xB10; break; /* left */
    case 3: extra = (int)g_step_up - 4; pos = 0xAFA; prev = 0xB14; break; /* up */
    default: return 0;                     /* down: nothing scrolled yet */
  }
  uint16_t v = (uint16_t)((r[pos] | r[pos + 1] << 8) + extra);
  r[pos] = r[prev] = (uint8_t)v;
  r[pos + 1] = r[prev + 1] = (uint8_t)(v >> 8);
  return 0;
}

typedef struct { uint32_t addr; uint16_t stock; uint16_t x2; uint16_t max; } Site;
static const Site kSites[] = {
  /* DoorTransition_Right: step, then calls (64 x 4 px) */
  {0x80AE9C, 0x0004, 0x0008, 0x0008}, {0x80AEA6, 0x0004, 0x0008, 0x0008},
  {0x80AEB5, 0x0040, 0x0020, 0x0020},
  /* DoorTransition_Left */
  {0x80AEE0, 0x0004, 0x0008, 0x0008}, {0x80AEEA, 0x0004, 0x0008, 0x0008},
  {0x80AEF9, 0x0040, 0x0020, 0x0020},
  /* DoorTransition_Down: moving frames 1..N (56 x 4 px), done at N+1 */
  {0x80AF44, 0x0039, 0x001D, 0x0011}, {0x80AF63, 0x0004, 0x0008, 0x000E},
  {0x80AF6D, 0x0004, 0x0008, 0x000E}, {0x80AF7C, 0x0039, 0x001D, 0x0011},
  /* DoorTransition_Up: no map update while < CPX #$05 (16 px), done at N+1 */
  {0x80AFE5, 0x0004, 0x0008, 0x0008}, {0x80AFEF, 0x0004, 0x0008, 0x0008},
  {0x80AFF5, 0x0005, 0x0003, 0x0003}, {0x80B029, 0x0039, 0x001D, 0x001D},
  /* AdvancePaletteFadeForAllPalettes_0xc */
  {0x82D961, 0x000C, 0x0005, 0x0002},
};

static void SmDoorActivate(void) {
  const char *speed = SmModOption(PACKAGE, FEATURE, "speed", "2x");
  bool max = !strcmp(speed, "max");
  g_step_h = 8;
  g_step_down = max ? 14 : 8;
  g_step_up = 8;
  size_t applied = 0, count = sizeof(kSites) / sizeof(kSites[0]);
  for (size_t i = 0; i < count; ++i)
    applied += snes_mod_operand_set(kSites[i].addr, max ? kSites[i].max : kSites[i].x2);
  static const uint32_t kHooks[] = {0x82DE64, 0x82E3E5};
  size_t hooks = SmModArmHooks("doors", kHooks, 2);
  fprintf(stderr, "[mods] doors %s: %zu/%zu sites, %zu/2 hooks\n",
          speed, applied, count, hooks);
}

SNES_MOD_CONSTRUCTOR(sm_register_door_plugin) {
  snes_mod_register_activation_plugin("super-metroid.pacing.doors",
                                      SmDoorActivate);
}
