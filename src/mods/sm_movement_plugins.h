#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "mod_runtime.h"

/* Consume the optional physical L+Down air gesture before shoulder remapping.
 * The resulting ordinary configured pad word is recorded/exchanged normally. */
bool SmQuickMorphAirInput(uint16_t *buttons, const SnesModInputContext *context);
