/* gen_stubs.c — hand-written HLE bodies the recompiler is told to call
 * in place of the original asm (via `hle_func <pc> <name>` cfg
 * directives). For Super Metroid the only boot-critical one is the
 * vblank wait.
 */
#include <stdint.h>
#include <stdio.h>
#include "cpu_state.h"
#include "common_rtl.h"
#include "cpu_trace.h"
#include "variables.h"
#include "sm_rtl.h"

/* WaitForNMI ($80:8338). The original routine raises waiting_for_nmi
 * ($7E:05B4) and spins until the NMI handler ($80:9583) clears it. In
 * the recompiled build there is no mid-frame NMI to clear it, so the
 * spin would never terminate. Replace the whole routine with a host
 * yield: raise the same WRAM handshake flag as the ROM, hand control
 * back to RunOneFrameOfGame (which runs the NMI handler and emulates the
 * frame), then continue. The recompiled NMI handler clears the flag; the
 * HLE must not clear it itself or the next NMI observes "not waiting"
 * and skips the queue-draining work.
 *
 * The HLE replaces the WHOLE routine, including its terminating RTL. Under
 * the Option-1 cpu->S ABI the caller's `JSL WaitForNMI` pushed a 3-byte
 * return frame that the real routine's RTL would pop; returning
 * RECOMP_RETURN_NORMAL signals a host-return, which (per _emit_return)
 * only happens AFTER the frame pop. So we must pop the 3-byte frame here —
 * otherwise it is orphaned and the 65816 stack leaks 3 bytes per vblank,
 * underflowing after ~1000 frames (the HdmaObjectHandler "runaway" crash). */
RecompReturn HleSmWaitForNmi(CpuState *cpu) {
  waiting_for_nmi = 1;
  sm_host_yield();
  cpu->S = (uint16)(cpu->S + 3);  /* emulate RTL: pop the JSL return frame */
  return RECOMP_RETURN_NORMAL;
}

static RecompReturn HleSmJmlIndirect(CpuState *cpu, uint16 ptr_addr,
                                     uint32 site_pc24) {
  uint16 target_pc = cpu_read16(cpu, 0x00, ptr_addr);
  uint8 target_bank = cpu_read8(cpu, 0x00, (uint16)(ptr_addr + 2));
  uint32 target = ((uint32)target_bank << 16) | target_pc;
  uint8 saved_pb = cpu->PB;
  uint8 hrv = cpu->host_return_valid;
  uint16 miss_s = (uint16)(cpu->S + ((hrv == 2 || hrv == 3) ? hrv : 0));

  if (!cpu_dispatch_has_entry(cpu, target)) {
    (void)cpu_trace_dispatch_oob(cpu, site_pc24, target);
  }

  cpu->PB = target_bank;
  RecompReturn r = cpu_dispatch_pc_from(cpu, target, miss_s, site_pc24);
  cpu->PB = saved_pb;
  return r;
}

#define SM_JML1784_WRAPPER(name, site) \
  RecompReturn name(CpuState *cpu) { return HleSmJmlIndirect(cpu, 0x1784, site); }
#define SM_JML178C_WRAPPER(name, site) \
  RecompReturn name(CpuState *cpu) { return HleSmJmlIndirect(cpu, 0x178C, site); }

SM_JML178C_WRAPPER(HleSmJmlIndirect178C_88CB, 0x2088CBu)
SM_JML1784_WRAPPER(HleSmJmlIndirect1784_8BF0, 0x208BF0u)
SM_JML1784_WRAPPER(HleSmJmlIndirect1784_90A3, 0x2090A3u)
SM_JML1784_WRAPPER(HleSmJmlIndirect1784_9420, 0x209420u)
SM_JML1784_WRAPPER(HleSmJmlIndirect1784_9B4C, 0x209B4Cu)
SM_JML1784_WRAPPER(HleSmJmlIndirect1784_9D20, 0x209D20u)
SM_JML1784_WRAPPER(HleSmJmlIndirect1784_9E97, 0x209E97u)
SM_JML1784_WRAPPER(HleSmJmlIndirect1784_A025, 0x20A025u)
SM_JML1784_WRAPPER(HleSmJmlIndirect1784_A140, 0x20A140u)
SM_JML1784_WRAPPER(HleSmJmlIndirect1784_A233, 0x20A233u)
SM_JML1784_WRAPPER(HleSmJmlIndirect1784_A303, 0x20A303u)
SM_JML1784_WRAPPER(HleSmJmlIndirect1784_A3AC, 0x20A3ACu)
SM_JML1784_WRAPPER(HleSmJmlIndirect1784_C292, 0x20C292u)

#undef SM_JML1784_WRAPPER
#undef SM_JML178C_WRAPPER
