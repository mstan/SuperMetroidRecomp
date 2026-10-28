/* gen_stubs.c — hand-written HLE bodies the recompiler is told to call
 * in place of the original asm (via `hle_func <pc> <name>` cfg
 * directives). For Super Metroid the only boot-critical one is the
 * vblank wait.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include "cpu_state.h"
#include "common_rtl.h"
#include "common_cpu_infra.h"
#include "cpu_trace.h"
#include "variables.h"
#include "sm_rtl.h"
#include "sm_pause_options.h"
#include "mods/sm_return_to_title_plugin.h"
#include "sm_activity.h"

/* Wait replacements unwind compiled calls to the interruptible scheduler.
 * The original ROM bytes own the WRAM handshake and return-frame pop; the
 * scheduler injects NMI at the polling boundary and resumes that guest PC. */
static RecompReturn sm_scheduler_yield(CpuState *cpu, uint32_t pc) {
  if (!interp_bridge_in_lle_scheduler()) {
    fprintf(stderr, "[sm_rtl] wait $%06X called outside the frame scheduler\n", (unsigned)pc);
    abort();
  }
  return interp_bridge_lle_yield_unwind(cpu, pc);
}
RecompReturn HleSmWaitForNmi(CpuState *cpu) {
  /* Preserve the JSL frame and execute the real polling bytes under the
   * interruptible scheduler; every runtime policy uses this continuation. */
  return sm_scheduler_yield(cpu, ((uint32)cpu->PB << 16) | 0x8338u);
}

/* $A0:8EB6 ends in PLB/RTL. It has no nested calls or waits: the bounded
 * interpreter call completes the original queue and architectural residue
 * before we add visible margin enemies. No guest property bits are edited. */
static void sm_prologue_cycles(CpuState *cpu, unsigned cycles) {
  cpu->coprocessor_master_cycles = cpu->master_cycles;
  cpu->cycles += cycles;
  cpu->master_cycles += cycles * ((g_memsel && (cpu->PB & 0x80)) ? 6 : 8);
}
static void sm_push16(CpuState *cpu, uint16 value) {
  cpu_write16(cpu, 0, (uint16)(cpu->S - 1), value); cpu->S -= 2;
}
static RecompReturn sm_native_tail(CpuState *cpu, uint32 body, uint32 entry,
                                  uint16 entry_s, uint8 hrv, unsigned frame) {
  cpu_tailcall_inherit_return_context(entry_s, hrv);
  return cpu_dispatch_pc_from(cpu, body, (uint16)(entry_s + frame), entry);
}
RecompReturn HleSmDetermineWhichEnemiesToProcess(CpuState *cpu) {
  uint32 entry = ((uint32)cpu->PB << 16) | 0x8eb6u;
  uint32 body = (entry & 0xff0000u) | 0x8eb7u;
  if (!SmActivityExtra() && !cpu->m_flag && !cpu->x_flag && cpu_dispatch_has_entry(cpu, body)) {
    if (interp_bridge_lle_master_deadline_reached(cpu))
      return interp_bridge_lle_yield_unwind(cpu, entry);
    uint16 entry_s = cpu->S; uint8 hrv = cpu->host_return_valid;
    /* PHB; the generated tail owns PHK/PLB and the entire original queue. */
    sm_prologue_cycles(cpu, 3);
    cpu_write8(cpu, 0, cpu->S, cpu->DB); --cpu->S;
    return sm_native_tail(cpu, body, entry, entry_s, hrv, 3);
  }
  RecompReturn result = interp_tier_run_call_frame(cpu, entry, entry, 3, NULL);
#if SM_VIEW_ACTIVITY
  if (result == RECOMP_RETURN_NORMAL && !SmActivityAugmentQueues(g_ram, SmActivityExtra())) {
    static bool warned;
    if (!warned) fprintf(stderr, "[sm-activity] invalid/full enemy queue; retained original ROM queues\n");
    warned = true;
  }
#endif
  return result;
}

/* Widen the horizontal predicate only. The ROM still owns the vertical
 * test, deletion effects, return carry/value and register restoration. */
static bool sm_margin_projectile(int16 x, int left, int right) {
#if SM_VIEW_ACTIVITY
  int extra = (int)SmActivityExtra();
  return extra && (x < left || x >= right) && x >= left - extra && x < right + extra;
#else
  return false;
#endif
}
RecompReturn HleSmDeleteProjectileIfFarOffScreen(CpuState *cpu) {
  uint32 entry = ((uint32)cpu->PB << 16) | 0xb16au;
  if (!cpu->m_flag && !cpu->x_flag && !cpu->_flag_D) {
    uint16 index = cpu_read16(cpu, cpu->DB, 0x0dde);
    int16 x = (int16)(cpu_read16(cpu, cpu->DB, (uint16)(0x0b64 + index)) -
                      cpu_read16(cpu, cpu->DB, 0x0911));
    if (index < 20 && !(index & 1) && sm_margin_projectile(x, -64, 320)) {
      cpu->X = index; /* Original LDX $0DDE; Y path overwrites A/N/Z/C/V. */
      sm_prologue_cycles(cpu, 5);
      return interp_tier_run_call_frame(cpu, (entry & 0xff0000u) | 0xb184u, entry, 2, NULL);
    }
  }
  uint32 body = (entry & 0xff0000u) | 0xb16du;
  if (!cpu->m_flag && !cpu->x_flag && cpu_dispatch_has_entry(cpu, body)) {
    if (interp_bridge_lle_master_deadline_reached(cpu))
      return interp_bridge_lle_yield_unwind(cpu, entry);
    uint16 entry_s = cpu->S; uint8 hrv = cpu->host_return_valid;
    sm_prologue_cycles(cpu, 5); /* LDX $0DDE */
    cpu->X = cpu_read16(cpu, cpu->DB, 0x0dde);
    cpu->_flag_Z = cpu->X == 0; cpu->_flag_N = (cpu->X >> 15) != 0;
    cpu->P = (cpu->P & ~0x82) | (cpu->_flag_Z ? 2 : 0) | (cpu->_flag_N ? 0x80 : 0);
    return sm_native_tail(cpu, body, entry, entry_s, hrv, 2);
  }
  return interp_tier_run_call_frame(cpu, entry, entry, 2, NULL);
}
RecompReturn HleSmCheckIfEprojIsOffScreen(CpuState *cpu) {
  uint32 entry = ((uint32)cpu->PB << 16) | 0xe6e0u;
  if (!cpu->m_flag && !cpu->x_flag && !cpu->_flag_D && cpu->X < 36 && !(cpu->X & 1)) {
    int16 x = (int16)(cpu_read16(cpu, cpu->DB, (uint16)(0x1a4b + cpu->X)) -
                      cpu_read16(cpu, cpu->DB, 0x0911));
    if (sm_margin_projectile(x, 0, 256)) {
      uint16 entry_s = cpu->S;
      /* PHX/PHY from $E6E0, before the original Y predicate at $E6FC.
       * Pass the caller watermark, so PLY/PLX do not end the bridge early. */
      sm_prologue_cycles(cpu, 8); sm_push16(cpu, cpu->X); sm_push16(cpu, cpu->Y);
      return interp_tier_dispatch_balanced(cpu, (entry & 0xff0000u) | 0xe6fcu, entry, entry_s, 2, false);
    }
  }
  uint32 body = (entry & 0xff0000u) | 0xe6e2u;
  if (!cpu->m_flag && !cpu->x_flag && cpu_dispatch_has_entry(cpu, body)) {
    if (interp_bridge_lle_master_deadline_reached(cpu))
      return interp_bridge_lle_yield_unwind(cpu, entry);
    uint16 entry_s = cpu->S; uint8 hrv = cpu->host_return_valid;
    sm_prologue_cycles(cpu, 8); sm_push16(cpu, cpu->X); sm_push16(cpu, cpu->Y);
    return sm_native_tail(cpu, body, entry, entry_s, hrv, 2);
  }
  return interp_tier_run_call_frame(cpu, entry, entry, 2, NULL);
}

/* $82:E039: inline-parameter VRAM update coroutine. */
RecompReturn HleSmDoorTransitionVramWait(CpuState *cpu) {
  return sm_scheduler_yield(cpu, 0x82E039u);
}

RecompReturn HleSmWaitUntilEndOfVblank(CpuState *cpu) {
  /* $80:82C5 only polls HVBJOY, then restores A/P and RTLs. The host
   * scheduler has already crossed the frame boundary, matching the official
   * decomp port's removal of this wait. */
  cpu->S = (uint16)(cpu->S + 3);
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
  uint32 dispatch_target = target;

  /* The standard enemy-instruction header is byte-identical in A2/A3.
   * A3's decomp map currently swallows $80ED inside a coarse function, so
   * reuse the correctly bounded A2 AOT body while preserving PB/DB=A3. */
  if (target == 0xA380EDu)
    dispatch_target = 0xA280EDu;

  if (!cpu_dispatch_has_entry(cpu, dispatch_target)) {
    (void)cpu_trace_dispatch_oob(cpu, site_pc24, target);

    /* These helpers implement tail JMLs reached through an enclosing JSL.
     * A missing AOT body must still execute the live ROM handler: its RTL
     * consumes that inherited frame and returns to the compiled caller.
     * The old prototype called cpu_dispatch_pc_from(), whose miss path only
     * restored S and silently skipped all handler side effects. */
    cpu->PB = target_bank;
    /* The pointer has already been resolved; interpreter entry is the
     * handler, not the indirect-jump instruction. */
    RecompReturn r = interp_tier_dispatch_balanced(
        cpu, target, site_pc24, cpu->S, hrv, false);
    cpu->PB = saved_pb;
    return r;
  }

  cpu->PB = target_bank;
  RecompReturn r = cpu_dispatch_pc_from(cpu, dispatch_target, miss_s, site_pc24);
  cpu->PB = saved_pb;
  return r;
}

static RecompReturn HleSmJmpIndirectIndexedTail(CpuState *cpu,
                                                uint16 ptr_base,
                                                uint32 site_pc24) {
  uint16 ptr_addr = (uint16)(ptr_base + cpu->X);
  uint16 target_pc = cpu_read16(cpu, cpu->PB, ptr_addr);
  uint32 target = ((uint32)cpu->PB << 16) | target_pc;
  uint8 hrv = cpu->host_return_valid;
  uint16 miss_s = (uint16)(cpu->S + ((hrv == 2 || hrv == 3) ? hrv : 0));

  if (!cpu_dispatch_has_entry(cpu, target)) {
    (void)cpu_trace_dispatch_oob(cpu, site_pc24, target);
    return interp_tier_dispatch_balanced(
        cpu, target, site_pc24, cpu->S, hrv, false);
  }
  return cpu_dispatch_pc_from(cpu, target, miss_s, site_pc24);
}

RecompReturn HleSmJmpIndirect0FA8X_F25F(CpuState *cpu) {
  return HleSmJmpIndirectIndexedTail(cpu, 0x0FA8, 0x28F25Fu);
}

RecompReturn HleSmJmpIndirect0FA8X_F265(CpuState *cpu) {
  return HleSmJmpIndirectIndexedTail(cpu, 0x0FA8, 0x28F265u);
}

RecompReturn HleSmJmpIndirect0FB2X_E6C5(CpuState *cpu) {
  return HleSmJmpIndirectIndexedTail(cpu, 0x0FB2, 0x23E6C5u);
}

RecompReturn HleSmEnemyFuncC8ADPreserveMx(CpuState *cpu) {
  /* Decomp-backed implementation of $A0:C8AD (EnemyFunc_C8AD).  The AOT
   * body for this routine has leaked processor width state and corrupted
   * KiHunter's enemy index.  Keep this helper local to the proven failing
   * call site until it has been checked against the full attract sequence.
   *
   * The original saves X/Y/P, clears carry in the saved P, and changes that
   * saved carry to one if either sampled block is a non-square slope.  Its
   * final PLX/PLY leave N/Z describing the restored Y value. */
  const uint16 k = cpu->x_flag ? (uint8)cpu->X : cpu->X;
  const uint8 data_bank = cpu->DB;
  const uint8 saved_p = cpu->P;
  const uint16 saved_x = cpu->X;
  const uint16 saved_y = cpu->Y;
  const uint16 x_pos = cpu_read16(cpu, data_bank, (uint16)(0x0F7A + k));
  uint16 y_pos = cpu_read16(cpu, data_bank, (uint16)(0x0F7E + k));
  const uint16 y_height =
      cpu_read16(cpu, data_bank, (uint16)(0x0F84 + k));
  const uint16 room_width = cpu_read16(cpu, data_bank, 0x07A5);
  uint8 result = 0;

  uint16 block = (uint16)((uint8)((uint16)(y_pos + y_height - 1) >> 4) *
                              (uint8)room_width +
                          (x_pos >> 4));
  cpu_write16(cpu, data_bank, 0x0DC4, block);
  uint16 level = cpu_read16(cpu, 0x7F, (uint16)(0x0002 + block * 2));
  uint8 bts = cpu_read8(cpu, 0x7F, (uint16)(0x6402 + block));
  if ((level & 0xF000u) == 0x1000u && (bts & 0x1Fu) >= 5) {
    uint16 temp_dd4 = (uint16)(y_height + y_pos - 1) & 0x000Fu;
    uint16 temp_dd6 = (uint16)(16 * (bts & 0x1Fu));
    result = 1;
    cpu_write16(cpu, data_bank, 0x0DD4, temp_dd4);
    cpu_write16(cpu, data_bank, 0x0DD6, temp_dd6);
    if (!(bts & 0x80u)) {
      uint16 sample_x = (bts & 0x40u) ? (x_pos ^ 0x000Fu) : x_pos;
      uint8 align = cpu_read8(
          cpu, 0x94,
          (uint16)(0x8B2B + temp_dd6 + (sample_x & 0x000Fu)));
      int16_t correction =
          (int16_t)((align & 0x1Fu) - temp_dd4 - 1);
      if (correction < 0) {
        y_pos = (uint16)(y_pos + correction);
        cpu_write16(cpu, data_bank, (uint16)(0x0F7E + k), y_pos);
      }
    }
  }

  block = (uint16)((uint8)((uint16)(y_pos - y_height) >> 4) *
                         (uint8)room_width +
                     (x_pos >> 4));
  cpu_write16(cpu, data_bank, 0x0DC4, block);
  level = cpu_read16(cpu, 0x7F, (uint16)(0x0002 + block * 2));
  bts = cpu_read8(cpu, 0x7F, (uint16)(0x6402 + block));
  if ((level & 0xF000u) == 0x1000u && (bts & 0x1Fu) >= 5) {
    uint16 temp_dd4 =
        (((uint16)(y_pos - y_height) & 0x000Fu) ^ 0x000Fu);
    uint16 temp_dd6 = (uint16)(16 * (bts & 0x1Fu));
    result = 1;
    cpu_write16(cpu, data_bank, 0x0DD4, temp_dd4);
    cpu_write16(cpu, data_bank, 0x0DD6, temp_dd6);
    if (bts & 0x80u) {
      uint16 sample_x = (bts & 0x40u) ? (x_pos ^ 0x000Fu) : x_pos;
      uint8 align = cpu_read8(
          cpu, 0x94,
          (uint16)(0x8B2B + temp_dd6 + (sample_x & 0x000Fu)));
      int16_t correction =
          (int16_t)((align & 0x1Fu) - temp_dd4 - 1);
      if (correction < 0) {
        y_pos = (uint16)(y_pos - correction);
        cpu_write16(cpu, data_bank, (uint16)(0x0F7E + k), y_pos);
      }
    }
  }

  cpu->X = saved_x;
  cpu->Y = saved_y;
  cpu->P = (uint8)((saved_p & ~0x83u) | (result ? 0x01u : 0u));
  if ((cpu->x_flag ? (uint8)saved_y : saved_y) == 0)
    cpu->P |= 0x02u;
  if (cpu->x_flag ? ((saved_y & 0x0080u) != 0)
                  : ((saved_y & 0x8000u) != 0))
    cpu->P |= 0x80u;
  cpu_p_to_mirrors(cpu);
  cpu->S = (uint16)(cpu->S + 3);  /* emulate the routine's RTL */
  return RECOMP_RETURN_NORMAL;
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

#undef SM_JML1784_WRAPPER
#undef SM_JML178C_WRAPPER

/* GameState_15_Paused_Async ($82:90E8), the pause screen's per-frame state.
 * Reached through Game_RunOneFrameOfGame's JSR ($8981,X); ends in RTS.
 *
 * The Options pause tab (src/sm_pause_options.c) reads and edits this
 * frame's pad and pause-menu state first; then the ROM's own routine runs,
 * unmodified, in both execution modes. Under the LLE scheduler that is a
 * yield back to the interpreter at the routine's entry. Otherwise the ROM
 * bytes run in the interpreter tier, which consumes the caller's 2-byte
 * frame and still bounces nested calls into compiled code
 * (docs/HLE_FUNC.md, "Delegating to the original"). */
RecompReturn HleSmGameState_15_Paused_Async(CpuState *cpu) {
  SmPauseOptions_GuestTick();
  /* The optional quit action fades the existing picture and leaves through
   * the routine's bare RTS. Its PHB prefix has not run, so PLB is skipped. */
  const uint32 entry = ((uint32)cpu->PB << 16) | (SmQuitPauseStep(cpu) ? 0x90FEu : 0x90E8u);
  if (interp_bridge_in_lle_scheduler())
    return interp_bridge_lle_yield_unwind(cpu, entry);
  return interp_tier_run_call_frame(cpu, entry, entry, 2, NULL);
}
