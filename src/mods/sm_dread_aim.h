#pragma once
#include <stdbool.h>
#include "mod_hook.h"
void SmDreadAimConfigure(bool enabled,unsigned native_shoulder);
uint32 SmDreadAimTransition(CpuState *cpu,uint32 pc);
uint32 SmDreadMoveXHook(CpuState *cpu,uint32 pc);
uint32 SmDreadMoveXEndHook(CpuState *cpu,uint32 pc);
