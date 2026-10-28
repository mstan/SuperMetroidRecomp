/* sm_mod_support.c — see sm_mod_support.h. */

#include "sm_mod_support.h"

#include <stdio.h>
#include <string.h>

#include "common_cpu_infra.h"
#include "guarded_patch.h"
#include "host_report.h"
#include "mod_hook.h"
#include "mod_runtime.h"
#include "snes/snes.h"

enum { kMaxPatches = GUARDED_PATCH_MAX_REGISTERED };

static GuardedPatch g_patches[kMaxPatches];
static int g_patch_count;

static uint8_t *RomByte(uint32_t snes_addr, size_t size) {
  if (!g_snes || !g_snes->cart || !g_snes->cart->rom) return NULL;
  if ((snes_addr & 0xFFFF) < 0x8000) return NULL;
  uint32_t offset = ((snes_addr >> 16) & 0x7F) << 15 | (snes_addr & 0x7FFF);
  if (offset + size > g_snes->cart->romSize) return NULL;
  return g_snes->cart->rom + offset;
}

bool SmModRomPatch(const char *name, uint32_t snes_addr,
                   const uint8_t *stock, const uint8_t *value, size_t size) {
  uint8_t *target = RomByte(snes_addr, size);
  GuardedPatchStatus status = kGuardedPatch_Invalid;
  if (target && g_patch_count < kMaxPatches) {
    GuardedPatch *patch = &g_patches[g_patch_count];
    status = guarded_patch_init(patch, name, target, size, stock, value);
    if (status == kGuardedPatch_Ok) status = guarded_patch_apply(patch);
    if (status == kGuardedPatch_Ok) status = guarded_patch_register(patch);
    if (status == kGuardedPatch_Ok) {
      g_patch_count++;
      return true;
    }
    guarded_patch_revert(patch);
  }
  fprintf(stderr, "[mods] %s: refused patch at $%06X (status %d)\n",
          name, (unsigned)snes_addr, (int)status);
  host_report_breadcrumb("mod patch refused: %s $%06X status=%d",
                         name, (unsigned)snes_addr, (int)status);
  return false;
}

bool SmModPatchImm16(const char *name, uint32_t insn_addr,
                     uint16_t stock, uint16_t value) {
  const uint8_t before[2] = {(uint8_t)stock, (uint8_t)(stock >> 8)};
  const uint8_t after[2] = {(uint8_t)value, (uint8_t)(value >> 8)};
  return SmModRomPatch(name, insn_addr + 1, before, after, 2);
}

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

enum { kMaxHooks = 64 };
static uint32_t g_hooks[kMaxHooks];
static int g_hook_count;

size_t SmModArmHooks(const char *name, const uint32_t *pcs, size_t count) {
  size_t armed = 0;
  for (size_t i = 0; i < count; ++i) {
    if (g_hook_count < kMaxHooks && snes_mod_hook_arm(pcs[i], true)) {
      g_hooks[g_hook_count++] = pcs[i];
      armed++;
      continue;
    }
    fprintf(stderr, "[mods] %s: no mod_hook declared at $%06X\n",
            name, (unsigned)pcs[i]);
    host_report_breadcrumb("mod hook missing: %s $%06X", name, (unsigned)pcs[i]);
  }
  return armed;
}

uint16_t SmModRomWord(uint32_t snes_addr) {
  const uint8_t *p = RomByte(snes_addr, 2);
  return p ? (uint16_t)(p[0] | p[1] << 8) : 0;
}

/* Runs before every activation: whatever the previous plan patched or hooked
 * is stock again before the new plan's plugins apply theirs. */
static void SmModSupportReset(void) {
  for (int i = g_patch_count - 1; i >= 0; --i) {
    guarded_patch_revert(&g_patches[i]);
    guarded_patch_unregister(&g_patches[i]);
  }
  g_patch_count = 0;
  for (int i = 0; i < g_hook_count; ++i)
    snes_mod_hook_arm(g_hooks[i], false);
  g_hook_count = 0;
}

SNES_MOD_CONSTRUCTOR(sm_register_mod_support) {
  snes_mod_register_reset_callback(SmModSupportReset);
}
