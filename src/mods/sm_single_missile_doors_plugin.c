/* One standard missile opens a red door. The original PLM still accepts the
 * projectile, advances its hit counter and performs opening/save-bit updates. */
#include <stdio.h>

#include "mod_hook.h"
#include "mod_runtime.h"
#include "sm_mod_support.h"

uint32 SmSingleMissileDoorHook(CpuState *cpu, uint32 pc24) {
  (void)pc24;
  unsigned slot = cpu->X;
  if (slot >= 0x50 || (slot & 1)) return 0;
  const uint8_t *ram = cpu->ram;
  unsigned header = ram[0x1c37 + slot] | ram[0x1c38 + slot] << 8;
  unsigned shot = ram[0x1d77 + slot] | ram[0x1d78 + slot] << 8;
  /* Four native red-cap orientations; red gates and other PLMs are untouched. */
  if (header >= 0xc88a && header <= 0xc89c && (header - 0xc88a) % 6 == 0 &&
      (shot & 0x0f00) == 0x0100 && ram[0xdf0c + slot] < 4)
    cpu->ram[0xdf0c + slot] = 4;
  return 0;
}

static void SmSingleMissileDoorsActivate(void) {
  static const uint32_t hooks[] = {0x84bd50};
  size_t armed = SmModArmHooks("one-missile red doors", hooks, 1);
  fprintf(stderr, "[mods] one-missile red doors: %zu/1 hooks\n", armed);
}

SNES_MOD_CONSTRUCTOR(sm_register_single_missile_doors) {
  snes_mod_register_activation_plugin("super-metroid.single-missile-doors",
                                      SmSingleMissileDoorsActivate);
}
