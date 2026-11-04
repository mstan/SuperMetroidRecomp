/* super-metroid.pacing / elevators: faster elevator rides.
 *
 * The elevator (enemy AI, bank $A3) moves 1.5 px per frame: Elevator_Func_2
 * while leaving and Elevator_Func3b while arriving each add or subtract
 * subpixels #$8000 then pixels #$0001 with carry. Scaling both immediates
 * scales the ride; arrival still clamps to the destination height, so a
 * faster step stops exactly where stock does. Inspired by Super Metroid Redux
 * (doubled elevator speed). The sites are `mod_operand` in recomp/bank23.cfg.
 */

#include <stdio.h>
#include <string.h>

#include "mod_runtime.h"
#include "sm_mod_support.h"

#define PACKAGE "super-metroid.pacing"
#define FEATURE "elevators"

static const struct { uint32_t addr; bool subpixel; } kSites[] = {
  {0xA3958B, true}, {0xA39594, false},   /* Func_2, moving down  (ADC) */
  {0xA395A6, true}, {0xA395AF, false},   /* Func_2, moving up    (SBC) */
  {0xA395C8, true}, {0xA395D1, false},   /* Func3b, arriving     (ADC) */
  {0xA395E2, true}, {0xA395EB, false},   /* Func3b, arriving     (SBC) */
};

static void SmElevatorActivate(void) {
  /* Step in 1/65536 px: stock 0x18000 (1.5 px). */
  SmModValue speed_value;
  const char *speed = SmModOption(&speed_value, PACKAGE, FEATURE, "speed", "2x");
  uint32_t step = !strcmp(speed, "3x") ? 0x48000 : 0x30000;
  size_t applied = 0, count = sizeof(kSites) / sizeof(kSites[0]);
  for (size_t i = 0; i < count; ++i) {
    uint16_t value = kSites[i].subpixel ? (uint16_t)step : (uint16_t)(step >> 16);
    applied += snes_mod_operand_set(kSites[i].addr, value);
  }
  fprintf(stderr, "[mods] elevators %s: %zu/%zu sites\n", speed, applied, count);
}

SNES_MOD_CONSTRUCTOR(sm_register_elevator_plugin) {
  snes_mod_register_activation_plugin("super-metroid.pacing.elevators",
                                      SmElevatorActivate);
}
