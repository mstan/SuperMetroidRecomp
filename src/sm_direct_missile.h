#pragma once
#include <stdint.h>
#include "cpu_state.h"
typedef struct SmDirectMissileState {
  uint16_t held, pending, active, selection, cancel, new_keys, filtered;
} SmDirectMissileState;
extern SmDirectMissileState g_sm_direct_missile;
void SmDirectMissileReset(void);
void SmDirectMissileFrame(uint32_t *inputs);
int SmDirectMissileInputsValid(uint32_t inputs);
int SmDirectMissileStateValid(const SmDirectMissileState *state);
