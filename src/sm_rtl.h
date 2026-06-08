#ifndef SM_SM_RTL_H_
#define SM_SM_RTL_H_
#include "common_rtl.h"
#include "common_cpu_infra.h"
#include "snes/snes_regs.h"

/* Per-frame host orchestration for Super Metroid (see sm_rtl.c). */
void SmDrawPpuFrame(void);
void RunOneFrameOfGame(void);

/* Yield the game fiber back to the host (called from the WaitForNMI
 * HLE in gen_stubs.c). */
void sm_host_yield(void);

#endif  /* SM_SM_RTL_H_ */
