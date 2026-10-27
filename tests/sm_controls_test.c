/* Drives the super-metroid.controls input transforms with a synthetic WRAM:
 * shoulder remaps, one-frame item select/cancel pulses, and the right-stick
 * item picker. The plugin is included directly; the mod runtime calls it
 * makes are stubbed here. */
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "mod_runtime.h"
#include "sm_mod_support.h"

static const char *g_options[4][2];
static SNESModInputTransform g_transform;
int snes_mod_register_activation_plugin(const char *id, SNESModActivationCallback cb) {
  (void)id; (void)cb; return 1;
}
int snes_mod_register_input_transform(SNESModInputTransform t) { g_transform = t; return 1; }
const char *SmModOption(SmModValue *out, const char *package, const char *feature,
                        const char *option, const char *fallback) {
  (void)package; (void)feature;
  for (int i = 0; i < 4 && g_options[i][0]; ++i)
    if (!strcmp(g_options[i][0], option)) fallback = g_options[i][1];
  snprintf(out->text, sizeof(out->text), "%s", fallback);
  return out->text;
}

#include "../src/mods/sm_controls_plugin.c"

static uint8_t g_wram[0x20000];
static void SetWord(uint16_t addr, uint16_t v) { g_wram[addr] = (uint8_t)v; g_wram[addr + 1] = (uint8_t)(v >> 8); }
static uint16_t Step(uint16_t buttons, int16_t rx, int16_t ry) {
  SnesModInputContext ctx = {0, g_wram, rx, ry, 0, 0};
  return g_transform(buttons, &ctx);
}

int main(void) {
  /* Stock controller setup ($4218 order): cancel Y, select Select, L down, R up. */
  SetWord(kGameState, 8);
  SetWord(kCfgItemCancel, 0x4000);
  SetWord(kCfgItemSwitch, 0x2000);
  SetWord(kCfgAimDown, 0x0020);
  SetWord(kCfgAimUp, 0x0010);
  assert(HostButtons(0x4000) == SNES_PAD_Y && HostButtons(0x0010) == SNES_PAD_R &&
         HostButtons(0x8000) == SNES_PAD_B && HostButtons(0x0080) == SNES_PAD_A);

  /* zero-mission, defaults: R aims, L arms missiles (hold). */
  SmZeroMissionActivate();
  assert(Step(SNES_PAD_R, 0, 0) == SNES_PAD_R);                          /* aim up */
  assert(Step(SNES_PAD_R | SNES_PAD_DOWN | SNES_PAD_X, 0, 0) == (SNES_PAD_L | SNES_PAD_X));
  assert(Step(SNES_PAD_DOWN, 0, 0) == SNES_PAD_DOWN);                    /* crouch kept */
  assert(Step(SNES_PAD_L, 0, 0) == SNES_PAD_SELECT);                     /* press: select */
  SetWord(kHudItem, 1);                                                  /* game armed missiles */
  assert(Step(SNES_PAD_L, 0, 0) == 0);                                   /* release frame */
  assert(Step(SNES_PAD_L, 0, 0) == 0);                                   /* held: nothing */
  assert(Step(0, 0, 0) == SNES_PAD_Y);                                   /* release: cancel */
  SetWord(kHudItem, 0);
  assert(Step(0, 0, 0) == 0);
  /* Outside gameplay everything passes through. */
  SetWord(kGameState, 0x0F);
  assert(Step(SNES_PAD_L | SNES_PAD_R, 0, 0) == (SNES_PAD_L | SNES_PAD_R));
  SetWord(kGameState, 8);

  /* toggle on L-as-aim: R arms/disarms on each press. */
  g_options[0][0] = "aim-shoulder"; g_options[0][1] = "L";
  g_options[1][0] = "missile"; g_options[1][1] = "toggle";
  SmZeroMissionActivate();
  assert(Step(SNES_PAD_L, 0, 0) == SNES_PAD_R);                          /* L aims up */
  assert(Step(SNES_PAD_R, 0, 0) == SNES_PAD_SELECT);
  SetWord(kHudItem, 1);
  assert(Step(0, 0, 0) == 0);
  assert(Step(SNES_PAD_R, 0, 0) == SNES_PAD_Y);
  SetWord(kHudItem, 0);
  assert(Step(0, 0, 0) == 0);

  /* radial-menu: stick down picks power bombs only once owned. */
  SmRadialMenuActivate();
  assert(Step(0, 0, 30000) == 0);                                        /* none owned */
  SetWord(kMaxPowerBombs, 5);
  assert(Step(0, 0, 30000) == SNES_PAD_SELECT);
  assert(Step(0, 0, 30000) == 0);                                        /* release frame */
  SetWord(kHudItem, 3);
  assert(Step(0, 0, 30000) == 0);                                        /* reached */
  assert(Step(SNES_PAD_A, 0, 1000) == SNES_PAD_A);                       /* deadzone */

  puts("sm_controls_test: PASS");
  return 0;
}
