/* sm_mod_support.c — see sm_mod_support.h. */

#include "sm_mod_support.h"

#include <stdio.h>
#include <string.h>

#include "common_cpu_infra.h"
#include "host_report.h"
#include "mod_hook.h"
#include "mod_runtime.h"
#include "snes/snes.h"

const char *SmModOption(SmModValue *out, const char *package, const char *feature,
                        const char *option, const char *fallback) {
  if (!snes_mod_runtime_feature_option_value_c(package, feature, option,
                                               out->text, sizeof(out->text)) || !out->text[0])
    snprintf(out->text, sizeof(out->text), "%s", fallback);
  return out->text;
}

bool SmModOptionBool(const char *package, const char *feature,
                     const char *option, bool fallback) {
  return !strcmp(SmModOption(package, feature, option,
                             fallback ? "true" : "false"), "true");
}

size_t SmModArmHooks(const char *name, const uint32_t *pcs, size_t count) {
  size_t armed = 0;
  for (size_t i = 0; i < count; ++i) {
    if (snes_mod_hook_arm(pcs[i], true)) {
      armed++;
      continue;
    }
    fprintf(stderr, "[mods] %s: no mod_hook declared at $%06X\n",
            name, (unsigned)pcs[i]);
    host_report_breadcrumb("mod hook missing: %s $%06X", name, (unsigned)pcs[i]);
  }
  return armed;
}

uint16_t SmModReadRom16(uint32_t snes_addr) {
  if (snes_addr > 0xFFFFFFu || !g_snes || !g_snes->cart) return 0;
  const uint8_t *lo = cart_getRomPtr(g_snes->cart, snes_addr >> 16, (uint16_t)snes_addr);
  const uint8_t *hi = cart_getRomPtr(g_snes->cart, snes_addr >> 16, (uint16_t)(snes_addr + 1));
  return lo && hi ? (uint16_t)(*lo | *hi << 8) : 0;
}

/* Reset the generated, declaration-sized runtime before each activation. */
static void SmModSupportReset(void) {
  snes_mod_hook_disarm_all();
  snes_mod_operand_clear_all();
}

SNES_MOD_CONSTRUCTOR(sm_register_mod_support) {
  snes_mod_register_reset_callback(SmModSupportReset);
}
