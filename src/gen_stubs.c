/* gen_stubs.c — hand-written HLE bodies the recompiler is told to call
 * in place of the original asm (via `hle_func <pc> <name>` cfg
 * directives). For Super Metroid the only boot-critical one is the
 * vblank wait.
 */
#include <stdint.h>
#include <stdio.h>
#include "cpu_state.h"
#include "common_rtl.h"
#include "variables.h"
#include "sm_rtl.h"

/* WaitForNMI ($80:8338). The original routine raises waiting_for_nmi
 * ($7E:05B4) and spins until the NMI handler ($80:9583) clears it. In
 * the recompiled build there is no mid-frame NMI to clear it, so the
 * spin would never terminate. Replace the whole routine with a host
 * yield: hand control back to RunOneFrameOfGame (which runs the NMI
 * handler and emulates the frame), then continue. On resume the wait is
 * satisfied, so leave the flag clear exactly as a completed NMI would. */
RecompReturn HleSmWaitForNmi(CpuState *cpu) {
  (void)cpu;
  sm_host_yield();
  waiting_for_nmi = 0;
  return RECOMP_RETURN_NORMAL;
}
