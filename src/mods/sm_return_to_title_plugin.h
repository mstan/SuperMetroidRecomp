#pragma once
#include <stdbool.h>
#include <stdint.h>

bool SmQuitFileSelectEnabled(void);
bool SmQuitFileSelectRequest(void);
struct CpuState;
/* Existing pause helper calls this before delegating to the original body. */
bool SmQuitPauseStep(struct CpuState *cpu);
/* Simulation-owned transition state is included in file/execution snapshots. */
uint32_t SmQuitFileSelectState(void);
bool SmQuitFileSelectStateValid(uint32_t state);
void SmQuitFileSelectRestore(uint32_t state);
