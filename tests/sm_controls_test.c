/* Drives the super-metroid.controls input transforms with a synthetic WRAM:
 * shoulder remaps, one-frame item select/cancel pulses, and the right-stick
 * item picker. The plugin is included directly; the mod runtime calls it
 * makes are stubbed here. */
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "mod_runtime.h"
#include "sm_mod_overlay.h"
#include "sm_mod_support.h"

static const char *g_options[4][2];
static SNESModInputTransform g_transform;
bool SmQuickMorphAirInput(uint16_t *buttons,const SnesModInputContext *ctx) {
  (void)buttons; (void)ctx; return false;
}
int snes_mod_register_activation_plugin(const char *id, SNESModActivationCallback cb) {
  (void)id; (void)cb; return 1;
}
int snes_mod_register_input_transform(SNESModInputTransform t) { g_transform = t; return 1; }
static int g_overlay_registered;
bool SmModOverlayRegister(SmModOverlayDraw draw) { (void)draw; g_overlay_registered = 1; return true; }
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
  for (unsigned i = 0; i < 30; ++i)
    assert(Step(SNES_PAD_R | SNES_PAD_X, 0, 0) == (SNES_PAD_L | SNES_PAD_X));
  assert(Step(0, 0, 0) == 0);
  assert(Step(SNES_PAD_R, 0, 0) == SNES_PAD_L); /* release/repress remembers down */
  SnesModInputContext other = {1, g_wram, 0, 0, 0, 0};
  assert(g_transform(SNES_PAD_R, &other) == SNES_PAD_R); /* independent seat */
  assert(Step(SNES_PAD_R | SNES_PAD_UP, 0, 0) == SNES_PAD_R);
  assert(Step(SNES_PAD_R, 0, 0) == SNES_PAD_R); /* up is also retained */
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
  assert(Step(SNES_PAD_L | SNES_PAD_DOWN, 0, 0) == SNES_PAD_L);
  assert(Step(SNES_PAD_L, 0, 0) == SNES_PAD_L); /* mirrored shoulder also remembers */
  assert(Step(SNES_PAD_L | SNES_PAD_UP, 0, 0) == SNES_PAD_R);
  assert(Step(SNES_PAD_R, 0, 0) == SNES_PAD_SELECT);
  SetWord(kHudItem, 1);
  assert(Step(0, 0, 0) == 0);
  assert(Step(SNES_PAD_R, 0, 0) == SNES_PAD_Y);
  SetWord(kHudItem, 0);
  assert(Step(0, 0, 0) == 0);

  /* Morph and Spring Ball forms select real PBs even if missiles were armed. */
  memset(g_options,0,sizeof(g_options));
  const unsigned balls[]={4,8,0x11,0x12,0x13};
  SetWord(kMaxPowerBombs,5);SetWord(kPowerBombs,5);
  for (unsigned i=0;i<sizeof(balls)/sizeof(*balls);++i) {
    g_wram[kMovementType]=(uint8_t)balls[i];SetWord(kHudItem,1);SmZeroMissionActivate();
    assert(Step(SNES_PAD_L,0,0)==SNES_PAD_SELECT);
    SetWord(kHudItem,2);assert(!Step(SNES_PAD_L,0,0));
    assert(Step(SNES_PAD_L,0,0)==SNES_PAD_SELECT);
    SetWord(kHudItem,3);assert(!Step(SNES_PAD_L,0,0));assert(!Step(SNES_PAD_L,0,0));
    assert(Step(0,0,0)==SNES_PAD_Y);SetWord(kHudItem,0);assert(!Step(0,0,0));
  }
  SetWord(kPowerBombs,0);SmZeroMissionActivate();assert(!Step(SNES_PAD_L,0,0));
  assert(!Step(0,0,0));SetWord(kPowerBombs,5);
  g_options[0][0]="aim-shoulder";g_options[0][1]="L";
  g_options[1][0]="missile";g_options[1][1]="toggle";
  SetWord(kCfgItemSwitch,0x0040);SetWord(kCfgItemCancel,0x8000); /* X/B remaps */
  SmZeroMissionActivate();SetWord(kHudItem,1);
  assert(Step(SNES_PAD_R,0,0)==SNES_PAD_X);
  SetWord(kHudItem,3);assert(!Step(0,0,0));
  assert(Step(SNES_PAD_R,0,0)==SNES_PAD_B);
  assert(!Step(0,0,0));SetWord(kHudItem,0);g_wram[kMovementType]=0;
  SetWord(kCfgItemSwitch,0x2000);SetWord(kCfgItemCancel,0x4000);

  /* radial-menu: stick down picks power bombs only once owned. */
  SetWord(kMaxPowerBombs,0);
  SmRadialMenuActivate();
  assert(g_overlay_registered);
  assert(Step(0, 0, 30000) == 0);                                        /* none owned */
  SetWord(kMaxPowerBombs, 5);
  assert(Step(0, 0, 30000) == SNES_PAD_SELECT);
  assert(Step(0, 0, 30000) == 0);                                        /* release frame */
  SetWord(kHudItem, 3);
  assert(Step(0, 0, 30000) == 0);                                        /* reached */
  assert(Step(SNES_PAD_A, 0, 1000) == SNES_PAD_A);                       /* deadzone */
  assert(g_ring_target == -1);
  /* The ring draws while the stick points somewhere, and only then. */
  static uint32_t frame[256 * 224];
  Step(0, 0, 30000);
  assert(g_ring_target == 3);
  SmRadialRing(frame, 256, 256, 224, 0, g_wram);
  int drawn = 0;
  for (int i = 0; i < 256 * 224; ++i) drawn += frame[i] != 0;
  assert(drawn > 0);

  /* Run inversion follows the native binding and never changes menu input. */
  SmRunByDefaultActivate();
  SetWord(kCfgRun, 0x0080); /* A */
  assert(Step(SNES_PAD_RIGHT, 0, 0) == (SNES_PAD_RIGHT | SNES_PAD_A));
  assert(Step(SNES_PAD_RIGHT | SNES_PAD_A, 0, 0) == SNES_PAD_RIGHT);
  assert(Step(SNES_PAD_LEFT | SNES_PAD_X | SNES_PAD_B, 0, 0) ==
         (SNES_PAD_LEFT | SNES_PAD_X | SNES_PAD_B | SNES_PAD_A));
  SetWord(kCfgRun, 0x4000); /* remapped to Y */
  assert(Step(SNES_PAD_LEFT, 0, 0) == (SNES_PAD_LEFT | SNES_PAD_Y));
  assert(Step(SNES_PAD_LEFT | SNES_PAD_Y | SNES_PAD_A, 0, 0) ==
         (SNES_PAD_LEFT | SNES_PAD_A));
  assert(g_transform(SNES_PAD_RIGHT, &other) == (SNES_PAD_RIGHT | SNES_PAD_Y));
  const unsigned native_screens[] = {0, 1, 9, 11, 12, 15, 42};
  for (unsigned i = 0; i < sizeof(native_screens) / sizeof(native_screens[0]); ++i) {
    SetWord(kGameState, native_screens[i]);
    assert(Step(SNES_PAD_Y | SNES_PAD_START, 0, 0) ==
           (SNES_PAD_Y | SNES_PAD_START));
  }
  SnesModInputContext no_game = {.player = 0};
  assert(g_transform(SNES_PAD_A, &no_game) == SNES_PAD_A);

  puts("sm_controls_test: PASS");
  return 0;
}
