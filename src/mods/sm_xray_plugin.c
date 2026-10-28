/* super-metroid.balance-fixes / xray: a wider, faster X-ray scope.
 *
 * HdmaobjPreInstr_XrayFunc1_BeamWidening ($88:8754) grows the beam's
 * half-angle ($0A84) until it reaches 11 (CMP #$000B at $88:8791) and then
 * clamps it to 10 (LDA #$000A at $88:8799). MoveXrayUp/MoveXrayDown step the
 * aim angle by one per frame (SBC/ADC #$0001). Super Metroid Redux widens the
 * clamp to 26 and doubles the step; the width is an option here. The sites
 * are `mod_operand` in recomp/bank08.cfg.
 */

#include <stdio.h>
#include <string.h>

#include "mod_runtime.h"
#include "sm_mod_support.h"

#define PACKAGE "super-metroid.balance-fixes"
#define FEATURE "xray"

static const uint32_t kStepSites[] = {0x8887F7, 0x888819, 0x88884F, 0x88887A};

static void SmXrayActivate(void) {
  SmModValue width_value, speed_value;
  const char *width = SmModOption(&width_value, PACKAGE, FEATURE, "width", "wide");
  const char *speed = SmModOption(&speed_value, PACKAGE, FEATURE, "aim-speed", "2x");
  uint16_t clamp = !strcmp(width, "widest") ? 26 : 18;
  uint16_t step = !strcmp(speed, "1x") ? 1 : 2;
  size_t applied = 0, count = 2;
  applied += snes_mod_operand_set(0x888791, (uint16_t)(clamp + 1));
  applied += snes_mod_operand_set(0x888799, clamp);
  if (step != 1) {
    count += sizeof(kStepSites) / sizeof(kStepSites[0]);
    for (size_t i = 0; i < sizeof(kStepSites) / sizeof(kStepSites[0]); ++i)
      applied += snes_mod_operand_set(kStepSites[i], step);
  }
  fprintf(stderr, "[mods] xray width=%u step=%u: %zu/%zu sites\n",
          (unsigned)clamp, (unsigned)step, applied, count);
}

SNES_MOD_CONSTRUCTOR(sm_register_xray_plugin) {
  snes_mod_register_activation_plugin("super-metroid.balance-fixes.xray",
                                      SmXrayActivate);
}
