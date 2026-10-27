/* super-metroid.combat / shinespark-drain: no energy cost while sparking.
 *
 * The three shinespark move handlers ($90:D0AB vertical, $90:D0D7 diagonal,
 * $90:D106 horizontal) each end with
 *     LDA $09C2 ; CMP #$001E ; BMI + ; DEC $09C2 ...
 * so health drains one unit per frame while it is at least 30. Raising the
 * threshold to #$7FFF makes BMI always skip the DEC. The spark still ends on
 * a collision; Samus_EndSuperJump keeps its own stock compare at $90:D2BD,
 * which no longer fires mid-flight because health no longer falls. Requested
 * by testers; Ascent ties drain reduction to items. The sites are
 * `live_operand` in recomp/bank10.cfg.
 */

#include <stdio.h>

#include "mod_runtime.h"
#include "sm_mod_support.h"

static const uint32_t kSites[] = {0x90D0C9, 0x90D0F8, 0x90D124};

static void SmShinesparkDrainActivate(void) {
  size_t applied = 0, count = sizeof(kSites) / sizeof(kSites[0]);
  for (size_t i = 0; i < count; ++i)
    applied += SmModPatchImm16("shinespark-drain", kSites[i], 0x001E, 0x7FFF);
  fprintf(stderr, "[mods] shinespark drain off: %zu/%zu sites\n", applied, count);
}

SNES_MOD_CONSTRUCTOR(sm_register_shinespark_drain_plugin) {
  snes_mod_register_activation_plugin("super-metroid.combat.shinespark-drain",
                                      SmShinesparkDrainActivate);
}
