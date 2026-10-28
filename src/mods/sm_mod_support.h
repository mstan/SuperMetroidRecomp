/* Shared plumbing for Super Metroid gameplay mod plugins.
 *
 * Plugins are selected by packages under mods/preloaded (snesrecomp
 * docs/MOD_PACKAGES.md) and run against the stock ROM: behaviour is
 * reimplemented here, never taken from a patched ROM.
 *
 * ROM-image patches are byte-match guarded (snesrecomp guarded_patch.h):
 * a patch applies only over the stock bytes it names, and every patch is
 * reverted by the reset callback that runs before each activation, so a
 * disabled feature is stock on the next launch. Code compiled ahead of time
 * folds immediates, so a patched instruction must also be declared
 * `live_operand` in its bank cfg (see recomp/bank23.cfg for an example).
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Patch `size` bytes at a LoROM bus address in the loaded ROM image.
 * `stock` is what must be there now; a mismatch refuses the patch and is
 * reported on stderr and in the crash breadcrumbs. */
bool SmModRomPatch(const char *name, uint32_t snes_addr,
                   const uint8_t *stock, const uint8_t *value, size_t size);

/* Rewrite a 16-bit immediate operand: the instruction at `insn_addr` is
 * opcode + little-endian operand. */
bool SmModPatchImm16(const char *name, uint32_t insn_addr,
                     uint16_t stock, uint16_t value);

/* The committed value of a manifest option, or `fallback` when the runtime
 * has none (feature disabled, option absent). Writes into `out` and returns
 * it. */
typedef struct SmModValue { char text[128]; } SmModValue;
const char *SmModOption(SmModValue *out, const char *package, const char *feature,
                        const char *option, const char *fallback);

/* A boolean option: true when its committed value is "true". */
bool SmModOptionBool(const char *package, const char *feature,
                     const char *option, bool fallback);

/* Arm the `mod_hook` declared at each LoROM bus address (snesrecomp
 * docs/MOD_HOOK.md). Every hook armed here is disarmed by the same reset
 * callback that reverts ROM patches, so a disabled feature is stock on the
 * next launch. Returns how many were armed; a PC without a declared hook is
 * reported like a refused patch. */
size_t SmModArmHooks(const char *name, const uint32_t *pcs, size_t count);

/* One word of the loaded ROM image at a LoROM bus address ($8000-$FFFF),
 * or 0 outside the image. */
uint16_t SmModRomWord(uint32_t snes_addr);
