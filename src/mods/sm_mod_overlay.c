/* sm_mod_overlay.c — see sm_mod_overlay.h. */

#include "sm_mod_overlay.h"

#include "mod_runtime.h"

extern uint8_t g_ram[0x20000];

enum { kMaxOverlays = 16 };
static SmModOverlayDraw g_overlays[kMaxOverlays];
static int g_count;

bool SmModOverlayRegister(SmModOverlayDraw draw) {
  if (!draw) return false;
  for (int i = 0; i < g_count; ++i)
    if (g_overlays[i] == draw) return true;
  if (g_count == kMaxOverlays) return false;
  g_overlays[g_count++] = draw;
  return true;
}

bool SmModOverlayActive(void) { return g_count != 0; }

void SmModOverlayRender(uint32_t *pixels, int stride, int width, int height, int stage_x) {
  for (int i = 0; i < g_count; ++i)
    g_overlays[i](pixels, stride, width, height, stage_x, g_ram);
}

static void SmModOverlayReset(void) { g_count = 0; }

SNES_MOD_CONSTRUCTOR(sm_register_mod_overlay) {
  snes_mod_register_reset_callback(SmModOverlayReset);
}
