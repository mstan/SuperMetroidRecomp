/* Shared plumbing for Super Metroid gameplay mod plugins.
 *
 * Plugins are selected by packages under mods/preloaded (snesrecomp
 * docs/MOD_PACKAGES.md) and run against the stock ROM: behaviour is
 * reimplemented here, never taken from a patched ROM.
 *
 * Configured immediates use mod_operand values outside the cartridge image.
 * The shared reset callback clears them and disarms the generated hook table
 * before every activation, so disabled features return to stock behavior.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "mod_operand.h"

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
 * docs/MOD_HOOK.md). The generated declarations own their armed flags; there
 * is no additional title-specific hook limit. Reset disarms all of them.
 * Returns how many were armed; an undeclared PC is reported. */
size_t SmModArmHooks(const char *name, const uint32_t *pcs, size_t count);

/* One word of the loaded ROM image at a LoROM bus address ($8000-$FFFF),
 * or 0 outside the image. */
uint16_t SmModReadRom16(uint32_t snes_addr);
