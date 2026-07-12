#include "sm_rtl.h"
#include "variables.h"
#include "common_cpu_infra.h"
#include "snes/snes.h"
#include "cpu_state.h"
#include "execution_mode.h"
#include "funcs.h"
#include "snes/interp_bridge.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include "fiber_compat.h"   /* Win32 Fibers on Windows, ucontext shim on POSIX */

/* ── Super Metroid host frame driver ─────────────────────────────────
 *
 * Unlike MMX's 7-slot cooperative scheduler, Super Metroid runs as a
 * single linear program: reset ($80:841C, Vector_RESET) falls into the
 * main game loop ($82:8948, RunOneFrameOfGame in the decomp), which
 * dispatches the current game state and then waits for vblank via
 * WaitForNMI ($80:8338). WaitForNMI is called from arbitrary call depth
 * (game-state handlers, door transitions, cutscenes — see the
 * COROUTINE_AWAIT sites the snesrev/sm decomp threads through it), and
 * reset itself waits on NMI during its boot delays. So the whole game
 * runs on ONE host fiber; WaitForNMI yields it back to the host, the
 * host emulates the frame (NMI + PPU), then resumes the fiber exactly
 * after the yield — preserving the full C call stack the way a fiber
 * does and a longjmp cannot.
 *
 * WaitForNMI ($80:8338) is HLE-replaced (bank00.cfg `hle_func 8338
 * HleSmWaitForNmi`); the HLE body in gen_stubs.c calls sm_host_yield(). */

/* Saved architectural register file across a fiber switch. g_cpu is a
 * global; the host's I_NMI run and the game's main-line code share it,
 * so each side's 65816 register state must be saved/restored around the
 * SwitchToFiber (same rationale as mmx_rtl.c's per-slot CpuState save —
 * runtime-flag-gated codegen reads m_flag/x_flag widths). */
typedef struct SmCpuSave {
  uint16_t A, X, Y, S, D;
  uint8_t  DB, PB, P;
  uint8_t  host_return_valid;
  uint8_t  m_flag, x_flag, emulation;
  uint8_t  _flag_N, _flag_V, _flag_Z, _flag_C, _flag_I, _flag_D;
  CpuTailcallContextSave tailcall_context;
} SmCpuSave;

static void sm_save_cpu(SmCpuSave *s, const CpuState *c) {
  s->A = c->A; s->X = c->X; s->Y = c->Y; s->S = c->S; s->D = c->D;
  s->DB = c->DB; s->PB = c->PB; s->P = c->P;
  s->host_return_valid = c->host_return_valid;
  s->m_flag = c->m_flag; s->x_flag = c->x_flag; s->emulation = c->emulation;
  s->_flag_N = c->_flag_N; s->_flag_V = c->_flag_V; s->_flag_Z = c->_flag_Z;
  s->_flag_C = c->_flag_C; s->_flag_I = c->_flag_I; s->_flag_D = c->_flag_D;
  cpu_tailcall_context_save(&s->tailcall_context);
}
static void sm_restore_cpu(CpuState *c, const SmCpuSave *s) {
  c->A = s->A; c->X = s->X; c->Y = s->Y; c->S = s->S; c->D = s->D;
  c->DB = s->DB; c->PB = s->PB; c->P = s->P;
  c->host_return_valid = s->host_return_valid;
  c->m_flag = s->m_flag; c->x_flag = s->x_flag; c->emulation = s->emulation;
  c->_flag_N = s->_flag_N; c->_flag_V = s->_flag_V; c->_flag_Z = s->_flag_Z;
  c->_flag_C = s->_flag_C; c->_flag_I = s->_flag_I; c->_flag_D = s->_flag_D;
  cpu_tailcall_context_restore(&s->tailcall_context);
}

uint16 counter_global_frames;

static void *g_host_fiber = NULL;   /* main thread, promoted to a fiber  */
static void *g_game_fiber = NULL;   /* the game's fiber (entry = I_RESET)*/
static SmCpuSave g_game_saved;      /* game CPU state at its last yield  */
static bool g_game_started = false;
static bool g_game_done = false;
static uint32_t g_lle_resume_pc = 0x808343u;

static SnesrecompExecutionMode sm_execution_mode(void) {
  /* LLE is the correctness floor; HLE remains an explicit optimization mode
   * selected through the shared runtime option. */
  return snesrecomp_execution_mode(SNESRECOMP_EXECUTION_MODE_LLE);
}

static int sm_rtl_diag_enabled(void) {
  static int s_init = 0, s_enabled = 0;
  if (!s_init) {
    const char *v = getenv("SM_RTL_DIAG");
    s_enabled = (v && v[0] && v[0] != '0');
    s_init = 1;
  }
  return s_enabled;
}

static void CALLBACK sm_game_fiber_entry(void *param) {
  (void)param;
  /* Enter at the reset vector and run the whole game. Every vblank
   * wait inside yields via sm_host_yield (SwitchToFiber back to the
   * host); control only reaches the line after I_RESET if the game's
   * top-level program actually returns, which Super Metroid's main
   * loop never does. */
  I_RESET(&g_cpu);
  if (sm_rtl_diag_enabled())
    fprintf(stderr, "[sm_rtl] game fiber returned from I_RESET — unexpected\n");
  g_game_done = true;
  for (;;)
    SwitchToFiber(g_host_fiber);
}

void sm_host_yield(void) {
  /* Called from the WaitForNMI HLE, inside the game fiber. Save the
   * game's full register file, hand control to the host, and restore on
   * resume so the game continues with exactly its pre-yield state. */
  sm_save_cpu(&g_game_saved, &g_cpu);
  SwitchToFiber(g_host_fiber);
  sm_restore_cpu(&g_cpu, &g_game_saved);
}

void RunOneFrameOfGame(void) {
  if (sm_execution_mode() == SNESRECOMP_EXECUTION_MODE_LLE) {
    /* The real $80:8338 WaitForNMI asserts $05B4, then spins at $80:8343
     * until NMI clears it.  Run reset only on the first host frame; every
     * later frame injects NMI and resumes at that exact guest PC.  The guest
     * stack retains arbitrary-depth coroutine continuations, while compiled
     * bodies bounce through the paired ABI without a host fiber. */
    uint32_t entry_pc = g_lle_resume_pc;
    if (!g_game_started) {
      cpu_state_init(&g_cpu, g_ram);
      g_game_started = true;
      entry_pc = 0x80841Cu;
    } else {
      SmCpuSave interrupted;
      sm_save_cpu(&interrupted, &g_cpu);
      g_snes->inNmi = true;
      cpu_push_interrupt_frame(&g_cpu);
      I_NMI(&g_cpu);
      /* The host-injected NMI communicates through SNES memory and hardware
       * state.  Restore the interrupted main-line register file exactly as
       * the HLE fiber path does; the generated RTI has no live guest PC to
       * return to and therefore cannot by itself preserve this continuation. */
      sm_restore_cpu(&g_cpu, &interrupted);

      /* The synthetic continuation is $80:8343, immediately after
       * WaitForNMI's `SEP #$30`.  CpuState has no guest PC field, so the NMI
       * save/restore alone cannot reconstruct this PC-derived width contract
       * if a compiled coroutine returned with M0/X0 just before yielding.
       * Enforce the state hardware necessarily has at this exact resume PC;
       * otherwise LDA $05B4 becomes a 16-bit read, observes adjacent $05B5,
       * and spins to the interpreter step cap during door transitions. */
      if ((entry_pc & 0xFFFFu) == 0x8343u) {
        g_cpu.P |= 0x30u;
        cpu_p_to_mirrors(&g_cpu);
        g_cpu.X &= 0x00FFu;
        g_cpu.Y &= 0x00FFu;
        g_cpu.DB = (uint8_t)(entry_pc >> 16);
      }
    }

    if (!interp_bridge_run_loop(&g_cpu, entry_pc, 0x808343u, 0x05B4u, 1)) {
      fprintf(stderr, "[sm_rtl] LLE loop bailed at entry $%06X\n",
              (unsigned)entry_pc);
      g_game_done = true;
    } else {
      uint32_t next_pc = interp_bridge_lle_resume_pc();
      if (next_pc)
        g_lle_resume_pc = next_pc;
    }
    ++counter_global_frames;
    return;
  }

  if (g_host_fiber == NULL) {
    g_host_fiber = ConvertThreadToFiber(NULL);
    if (g_host_fiber == NULL) {
      fprintf(stderr, "[sm_rtl] ConvertThreadToFiber failed\n");
      abort();
    }
  }

  if (!g_game_started) {
    /* Frame 0: boot. Initialize CPU state and run reset up to the first
     * WaitForNMI yield. No NMI has fired yet (matches hardware: the
     * first NMI arrives after reset has set up the PPU and enabled
     * NMI). */
    cpu_state_init(&g_cpu, g_ram);
    /* 8 MiB fiber stack — recompiled call chains (deep object/AI
     * handlers under the game-state dispatch) can nest far. */
    g_game_fiber = CreateFiber(8 * 1024 * 1024, sm_game_fiber_entry, NULL);
    if (g_game_fiber == NULL) {
      fprintf(stderr, "[sm_rtl] CreateFiber failed\n");
      abort();
    }
    g_game_started = true;
    SwitchToFiber(g_game_fiber);
    ++counter_global_frames;
    return;
  }

  if (g_game_done)
    return;

  /* Steady state: a vblank just began. Run the recompiled NMI handler
   * (per-frame VRAM/OAM/CGRAM DMA, music queue, joypad latch), then
   * resume the game fiber after its WaitForNMI. g_cpu currently holds
   * the game's yielded register file, so I_NMI runs as if it interrupted
   * the main line — its RTI-balanced interrupt frame and any register
   * churn are discarded when sm_host_yield restores g_game_saved on
   * resume (NMI talks to the main line through RAM, not registers). */
  g_snes->inNmi = true;
  cpu_push_interrupt_frame(&g_cpu);
  I_NMI(&g_cpu);

  SwitchToFiber(g_game_fiber);
  ++counter_global_frames;
}

void SmDrawPpuFrame(void) {
  SimpleHdma hdma_chans[8];
  Dma *dma = g_dma;

  /* Reinitialize HDMA from the last $420C (HDMAEN) value written during
   * NMI. Super Metroid drives the HUD/status split and various color/
   * window effects through HDMA; the framework records the last HDMAEN
   * write in g_snesrecomp_last_hdmaen. */
  dma_startDma(dma, g_snesrecomp_last_hdmaen, true);

  /* HDMA objects are not confined to channels 5-7. In particular, the
   * Ceres elevator uses channel 3 to switch BGMODE from the mode-1 HUD to
   * the mode-7 shaft below it, and channel 2 for its color-math effect.
   * Process every enabled hardware channel in priority order. */
  for (int ch = 0; ch < 8; ch++)
    SimpleHdma_Init(&hdma_chans[ch], &dma->channel[ch]);

  /* Super Metroid programs the H/V IRQ for the HUD/minimap raster split
   * (Vector_IRQ at $80:986A dispatches IrqHandler_*_BeginHud/EndHud).
   * Latch the timer-IRQ at the programmed scanline so I_IRQ runs the
   * split mid-frame, matching MMX/SMW's draw path. */
  int trigger = g_snes->vIrqEnabled ? g_snes->vTimer : -1;

  for (int i = 0; i <= 224; i++) {
    /* HDMA runs during the H-blank preceding each visible scanline. The
     * raster IRQ then selects the register set used for that line (the
     * vTimer=0 IRQ establishes the HUD before line 0 is drawn). Rendering
     * first leaves one scanline in the previous frame's state, which is
     * visible as a strip of the cleared mode-1 tilemap above the Ceres HUD. */
    for (int ch = 0; ch < 8; ch++)
      SimpleHdma_DoLine(&hdma_chans[ch]);
    if (i == trigger) {
      g_snes->inIrq = true;
      cpu_push_interrupt_frame(&g_cpu);
      I_IRQ(&g_cpu);
      trigger = g_snes->vIrqEnabled ? g_snes->vTimer : -1;
    }
    ppu_runLine(g_ppu, i);
  }
}
