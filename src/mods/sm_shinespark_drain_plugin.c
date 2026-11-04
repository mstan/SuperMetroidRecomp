/* super-metroid.combat / shinespark-drain: less or no energy cost while
 * sparking.
 *
 * The three shinespark move handlers ($90:D0AB vertical, $90:D0D7 diagonal,
 * $90:D106 horizontal) each end with
 *     LDA $09C2 ; CMP #$001E ; BMI rts ; DEC $09C2 ; BPL rts ; STZ $09C2 ; rts: RTS
 * so health drains one unit per frame while it is at least 30.
 *   - "off": the CMP threshold becomes #$7FFF, so BMI always skips the DEC
 *     (`mod_operand` in recomp/bank10.cfg).
 *   - "half": a hook on each DEC continues at the RTS on odd game-time frames
 *     ($09DA), halving the drain (`mod_hook` in recomp/bank10.cfg).
 * The spark still ends on a collision; Samus_EndSuperJump keeps its own stock
 * compare at $90:D2BD. Requested by testers; Ascent ties drain reduction to
 * items.
 */

#include <stdio.h>
#include <string.h>

#include "mod_hook.h"
#include "mod_runtime.h"
#include "sm_mod_support.h"

#define PACKAGE "super-metroid.combat"
#define FEATURE "shinespark-drain"

static const uint32_t kCompareSites[] = {0x90D0C9, 0x90D0F8, 0x90D124};
static const uint32_t kDecrementSites[] = {0x90D0CE, 0x90D0FD, 0x90D129};
enum { kSites = 3, kDecToRts = 8, kGameTimeFrames = 0x09DA };

uint32 SmModShinesparkHalfDrain(CpuState *cpu, uint32 pc24) {
  if (!(cpu->ram[kGameTimeFrames] & 1)) return 0;
  return pc24 + kDecToRts;
}

static void SmShinesparkDrainActivate(void) {
  SmModValue drain_value;
  const char *drain = SmModOption(&drain_value, PACKAGE, FEATURE, "drain", "off");
  size_t applied = 0;
  if (!strcmp(drain, "half")) {
    for (size_t i = 0; i < kSites; ++i)
      applied += snes_mod_hook_arm(kDecrementSites[i], true);
  } else {
    for (size_t i = 0; i < kSites; ++i)
      applied += snes_mod_operand_set(kCompareSites[i], 0x7FFF);
  }
  fprintf(stderr, "[mods] shinespark drain %s: %zu/%d sites\n", drain, applied, kSites);
}

static void SmShinesparkDrainReset(void) {
  for (size_t i = 0; i < kSites; ++i)
    snes_mod_hook_arm(kDecrementSites[i], false);
}

SNES_MOD_CONSTRUCTOR(sm_register_shinespark_drain_plugin) {
  snes_mod_register_reset_callback(SmShinesparkDrainReset);
  snes_mod_register_activation_plugin("super-metroid.combat.shinespark-drain",
                                      SmShinesparkDrainActivate);
}
