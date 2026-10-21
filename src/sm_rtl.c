#include "sm_rtl.h"
#include "sm_renderer.h"
#include "sm_spc_player.h"
#include "sm_pause_options.h"
#include "variables.h"
#include "common_cpu_infra.h"
#include "snes/snes.h"
#include "cpu_state.h"
#include "execution_mode.h"
#if defined(SNESRECOMP_SETUP_HOST)
/* A SETUP HOST is this host without recompiled code, so there is no funcs.h
 * to include and no I_RESET / I_NMI / I_IRQ to call. It also never runs a
 * guest: SnesInit() refuses to boot one when this is defined, and the only
 * path through that binary is the launcher's Generate & rebuild wizard, which
 * replaces it with a real build from the player's own ROM.
 *
 * These are not stubs in the sense the rules forbid -- none of them invents a
 * result to get past a gap. They exist so the link resolves, and each one is
 * unreachable; if the wiring ever changes and one IS reached, it says which
 * entry point and stops, rather than pretending the guest ran. */
static void sm_no_recompiled_code(const char *what) {
  fprintf(stderr,
          "[sm_rtl] %s called in a setup host: this binary carries no "
          "recompiled code and cannot run the guest.\n", what);
  abort();
}
#define I_RESET(cpu) ((void)(cpu), sm_no_recompiled_code("I_RESET"))
#define I_NMI(cpu)   ((void)(cpu), sm_no_recompiled_code("I_NMI"))
#define I_IRQ(cpu)   ((void)(cpu), sm_no_recompiled_code("I_IRQ"))
#else
#include "funcs.h"
#endif
#include "snes/interp_bridge.h"
#include "snes/saveload.h"
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

/* File snapshots need the active CPU and the LLE scheduler's continuation.
 * snes_saveload() saves the dormant hardware CPU, while this port executes
 * through g_cpu. A same-process load can hide the omission; a fresh launch
 * otherwise resumes reset over restored WRAM. Use the framework's versioned,
 * pointer-free execution residue, never a serialized host fiber or C stack.
 * HLE fiber snapshots retain their historical limitations: there is no
 * portable on-disk representation of a suspended native call chain.
 *
 * Version 2 appends one word after the header: whether the pause menu's
 * Options panel is up (src/sm_pause_options.h). The guest parks on its Map
 * screen under the panel, so restoring the RAM without the panel would leave
 * a Map screen running over whatever the panel was covering. */
#define SM_FILE_STATE_MAGIC 0x534D4653u  /* SMFS */
#define SM_FILE_STATE_VERSION 2u
static uint32_t g_loaded_file_state[7];
static uint32_t g_loaded_pause_options;
static bool g_loaded_file_state_valid;

void SmStateSaveExtra(struct SaveLoadInfo *sli) {
  if (g_game_fiber || sm_execution_mode() != SNESRECOMP_EXECUTION_MODE_LLE)
    return;
  uint32_t header[7] = {
    SM_FILE_STATE_MAGIC, SM_FILE_STATE_VERSION, g_lle_resume_pc,
    g_game_started ? 1u : 0u, g_game_done ? 1u : 0u,
    counter_global_frames, (uint32_t)SmSpcPlayer_StateSize()
  };
  uint32_t pause_options = SmPauseOptions_IsActive() ? 1u : 0u;
  sli->func(sli, header, sizeof(header));
  sli->func(sli, &pause_options, sizeof(pause_options));
  RtlSaveExecutionState(sli);
  if (header[6]) {
    void *spc = malloc(header[6]);
    if (!spc) abort();
    if (SmSpcPlayer_SaveState(spc, header[6]) != header[6]) abort();
    sli->func(sli, spc, header[6]);
    free(spc);
  }
}

void SmStateLoadExtra(struct SaveLoadInfo *sli, uint32_t version) {
  (void)version;
  g_loaded_file_state_valid = false;
  sli->func(sli, g_loaded_file_state, sizeof(g_loaded_file_state));
  const uint32_t *h = g_loaded_file_state;
  if (h[0] != SM_FILE_STATE_MAGIC || h[1] < 1 || h[1] > SM_FILE_STATE_VERSION ||
      h[2] > 0xFFFFFFu || h[3] > 1 || h[4] > 1 ||
      (h[6] && h[6] != SmSpcPlayer_StateSize())) {
    fprintf(stderr, "[sm_state] unrecognized execution-state extension\n");
    return;
  }
  g_loaded_pause_options = 0;
  if (h[1] >= 2) {
    sli->func(sli, &g_loaded_pause_options, sizeof(g_loaded_pause_options));
    if (g_loaded_pause_options > 1) {
      fprintf(stderr, "[sm_state] unrecognized execution-state extension\n");
      return;
    }
  }
  if (!RtlLoadExecutionState(sli)) {
    fprintf(stderr, "[sm_state] execution residue does not match this runtime\n");
    return;
  }
  if (h[6]) {
    void *spc = malloc(h[6]);
    if (!spc) abort();
    sli->func(sli, spc, h[6]);
    int ok = SmSpcPlayer_LoadState(spc, h[6]);
    free(spc);
    if (!ok) return;
  }
  g_loaded_file_state_valid = true;
}

void SmOnStateLoaded(uint32_t version) {
  SmRendererReset();
  /* Host panel state from before the load never describes the loaded RAM. */
  SmPauseOptions_SetActive(false);
  if (!g_loaded_file_state_valid || g_game_fiber ||
      sm_execution_mode() != SNESRECOMP_EXECUTION_MODE_LLE) {
    fprintf(stderr, "[sm_state] loaded legacy/incompatible v%u snapshot; "
            "native continuation not restored\n", (unsigned)version);
    g_loaded_file_state_valid = false;
    return;
  }
  RtlApplyExecutionState();
  g_lle_resume_pc = g_loaded_file_state[2];
  g_game_started = g_loaded_file_state[3] != 0;
  g_game_done = g_loaded_file_state[4] != 0;
  counter_global_frames = (uint16_t)g_loaded_file_state[5];
  SmPauseOptions_SetActive(g_loaded_pause_options != 0);
  g_loaded_file_state_valid = false;
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

/* ── Rewindable execution position (rollback snapshots) ──────────────────
 *
 * This port's guest is one linear program on ONE host fiber: WaitForNMI
 * yields from arbitrary call depth, so the game's position in its own code IS
 * that fiber's C call chain, and no guest snapshot contains it. Rewinding the
 * machine without it leaves the two out of step -- measured before these
 * hooks existed, run-ahead's rewind left 21 frames in 1,200 in a state the
 * game would not otherwise have been in, in bursts around scene changes where
 * the call depth differs from frame to frame.
 *
 * So a rollback snapshot carries the fiber's live stack and context, plus the
 * register file sm_host_yield parked beside it. In-process, in-memory, this
 * build only -- exactly the promise RtlRollbackSaveToMemory makes. File
 * savestates do NOT use these: a stack image is not a thing to write to disk.
 */
typedef struct SmExecHeader {
  uint32_t   magic;
  uint32_t   version;
  SmCpuSave  game_saved;
  uint8_t    game_started;
  uint8_t    game_done;
  uint8_t    pause_options;  /* Options pause panel up (see SmStateSaveExtra) */
  uint8_t    pad;
  uint32_t   lle_resume_pc;
  uint32_t   fiber_bytes;
  uint32_t   spc_bytes;
} SmExecHeader;

#define SM_EXEC_MAGIC   0x534D4558u  /* 'SMEX' */
#define SM_EXEC_VERSION 2u

size_t SmExecStateBound(void) {
  /* No fiber: this frame ran on the interpreter bridge, whose own position is
   * already in the rollback residue. All that is left is the small header --
   * and g_lle_resume_pc in it is the thing that actually bit, because it is
   * where the bridge resumes next frame and speculation moves it. */
  if (!g_game_fiber) return sizeof(SmExecHeader) + SmSpcPlayer_StateSize();
  /* A fiber exists, so the call chain in it IS the guest's position. If this
   * backend cannot copy one, say so with a zero bound rather than let a
   * rewind put the machine back and leave the fiber where it was. */
  if (!FiberSnapshotSupported()) return 0;
  {
    size_t fiber = FiberSnapshotBound(g_game_fiber);
    /* A fiber that exists but has never been suspended has nothing live to
     * copy; the header alone is the whole position. */
    return sizeof(SmExecHeader) + fiber + SmSpcPlayer_StateSize();
  }
}

size_t SmExecStateSave(void *out, size_t capacity) {
  SmExecHeader hdr;
  size_t fiber = 0;
  if (!out || capacity < sizeof(hdr)) return 0;
  if (g_game_fiber) {
    if (!FiberSnapshotSupported()) return 0;
    fiber = FiberSnapshotSave(g_game_fiber, (uint8_t *)out + sizeof(hdr),
                              capacity - sizeof(hdr));
    if (!fiber && FiberSnapshotBound(g_game_fiber)) return 0;  /* had one, lost it */
  }
  hdr.magic         = SM_EXEC_MAGIC;
  hdr.version       = SM_EXEC_VERSION;
  hdr.game_saved    = g_game_saved;
  hdr.game_started  = g_game_started ? 1u : 0u;
  hdr.game_done     = g_game_done ? 1u : 0u;
  hdr.pause_options = SmPauseOptions_IsActive() ? 1u : 0u;
  hdr.pad = 0;
  {
    size_t spc = SmSpcPlayer_SaveState((uint8_t *)out + sizeof(hdr) + fiber,
                                       capacity - sizeof(hdr) - fiber);
    if (SmSpcPlayer_StateSize() && !spc) return 0;
    hdr.spc_bytes = (uint32_t)spc;
    hdr.lle_resume_pc = g_lle_resume_pc;
    hdr.fiber_bytes   = (uint32_t)fiber;
    memcpy(out, &hdr, sizeof(hdr));
    return sizeof(hdr) + fiber + spc;
  }
}

int SmExecStateLoad(const void *in, size_t size) {
  SmExecHeader hdr;
  if (!in || size < sizeof(hdr)) return 0;
  memcpy(&hdr, in, sizeof(hdr));
  if (hdr.magic != SM_EXEC_MAGIC || hdr.version != SM_EXEC_VERSION) return 0;
  if (size < sizeof(hdr) + hdr.fiber_bytes + hdr.spc_bytes) return 0;
  if (hdr.spc_bytes &&
      !SmSpcPlayer_LoadState((const uint8_t *)in + sizeof(hdr) + hdr.fiber_bytes,
                             hdr.spc_bytes))
    return 0;
  if (hdr.fiber_bytes) {
    if (!g_game_fiber) return 0;
    if (!FiberSnapshotLoad(g_game_fiber, (const uint8_t *)in + sizeof(hdr),
                           hdr.fiber_bytes))
      return 0;
  }
  g_game_saved    = hdr.game_saved;
  g_game_started  = hdr.game_started != 0;
  g_game_done     = hdr.game_done != 0;
  g_lle_resume_pc = hdr.lle_resume_pc;
  SmPauseOptions_SetActive(hdr.pause_options != 0);
  return 1;
}

void RunOneFrameOfGame(void) {
  if (sm_execution_mode() == SNESRECOMP_EXECUTION_MODE_LLE) {
    /* These door-loader routines wait for NMI from inside their bodies.
     * Execute their authoritative ROM bytes under the LLE scheduler instead
     * of bouncing into an atomic compiled body that cannot be interrupted. */
    static const uint32_t k_lle_door_loader_targets[] = {
      0x828948u,  /* Game_RunOneFrameOfGame: LLE-authoritative gameplay */
      0x828B44u,  /* GameState_8_MainGameplay */
      0x82E169u,  /* GameState_9_HitDoorBlock */
      0x82E1B7u,  /* GameState_10_LoadingNextRoom_Async */
      0x82E288u,  /* GameState_11_LoadingNextRoom_Async */
      0x82DFD1u,  /* LoadEnemyGfxToVram */
      0x82E039u,  /* inline-parameter VRAM transfer */
      0x82E4A9u,  /* DoorTransitionFunction_LoadMoreThings_Async */
      0x82E5D9u,  /* door-dependent background transfer dispatcher */
      0x82E5EBu,  /* UpdateBackgroundCommand_2_TransferToVram */
    };
    interp_bridge_set_lle_bounce_exclusions(
        k_lle_door_loader_targets,
        sizeof(k_lle_door_loader_targets) /
            sizeof(k_lle_door_loader_targets[0]));

    /* The real $80:8338 WaitForNMI asserts $05B4, then spins at $80:8343
     * until NMI clears it.  Run reset only on the first host frame; every
     * later frame injects NMI and resumes at that exact guest PC.  The guest
     * stack retains arbitrary-depth coroutine continuations, while compiled
     * bodies bounce through the paired ABI without a host fiber. */
    /* A contained bridge bail (see interp_bridge_run_loop returning 0) leaves
     * the guest stack half-unwound; re-entering every frame from the stale
     * resume PC executed garbage until it hit InvalidInterrupt_Crash and
     * could reach save RAM on the way.  Stay stopped, like the fiber path. */
    if (g_game_done)
      return;
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

    /* Diagnostic (SM_LLE_RESUME_DIAG): g_lle_resume_pc is the host's ONLY
     * continuation handle into the guest. A healthy frame always comes back
     * to WaitForNMI's $80:8343 spin, so a resume PC that stops advancing means
     * the guest is parked and every subsequent frame is re-entering the same
     * place with the same state. That is invisible from WRAM state alone. */
    {
      static uint32_t s_prev_entry = 0xFFFFFFFFu;
      static unsigned s_same = 0;
      static int s_diag = -1;
      if (s_diag < 0) {
        const char *v = getenv("SM_LLE_RESUME_DIAG");
        s_diag = (v && v[0] && v[0] != '0');
      }
      if (s_diag) {
        if (entry_pc == s_prev_entry) {
          if (++s_same == 8u || (s_same % 300u) == 0u)
            fprintf(stderr,
                    "[sm_lle] resume PC STUCK at $%06X for %u frames "
                    "(S=%04X DB=%02X P=%02X gs=%02X)\n",
                    (unsigned)entry_pc, s_same, (unsigned)g_cpu.S,
                    (unsigned)g_cpu.DB, (unsigned)g_cpu.P,
                    (unsigned)g_ram[0x0998]);
        } else {
          if (s_same >= 8u)
            fprintf(stderr,
                    "[sm_lle] resume PC advanced $%06X -> $%06X after %u "
                    "frames\n",
                    (unsigned)s_prev_entry, (unsigned)entry_pc, s_same);
          s_same = 0;
          s_prev_entry = entry_pc;
        }
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
  /* Optional host-only trace of the DMA registers actually consumed by the
   * raster pass. WRAM object table pointers may have changed since NMI. */
  static bool trace_checked;
  static FILE *dma_trace;
  static unsigned trace_frame;
  ++trace_frame;
  if (!trace_checked) {
    const char *path = getenv("SM_DMA_TRACE");
    trace_checked = true;
    if (path) dma_trace = fopen(path, "w");
  }
  if (dma_trace) {
    for (unsigned ch = 0; ch < 8; ++ch) {
      const DmaChannel *c = &dma->channel[ch];
      fprintf(dma_trace, "%u,%u,%02x,%04x,%02x,%u,%u,%02x,%02x\n", trace_frame,
          ch, c->aBank, c->aAdr, c->bAdr, c->mode, c->indirect, c->indBank,
          g_snesrecomp_last_hdmaen);
    }
  }

  /* This loop IS the HDMA engine for the frame: it walks the real tables per
   * line below. The framework's own beam-timeline HDMA must therefore stay
   * off, or every table is consumed twice -- once here and once from
   * snes_advance_beam, which runs inside any guest register write that syncs
   * the master clock. Set every frame rather than once at boot: a save-state
   * load restores the Snes struct this flag lives in. */
  snes_set_hdma_beam_enabled(g_snes, false);

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
  unsigned probe_armed = 0;
  for (int ch = 0; ch < 8; ch++)
    if (hdma_chans[ch].table) probe_armed |= 1u << ch;

  /* Super Metroid programs the H/V IRQ for the HUD/minimap raster split
   * (Vector_IRQ at $80:986A dispatches IrqHandler_*_BeginHud/EndHud).
   * Latch the timer-IRQ at the programmed scanline so I_IRQ runs the
   * split mid-frame, matching MMX/SMW's draw path. */
  int trigger = g_snes->vIrqEnabled ? g_snes->vTimer : -1;

  /* SM_RASTER_PROBE=<path>: the shape of each rendered frame -- the raster
   * split's IRQ schedule, the layers and BG3 tilemap it left enabled, the
   * HDMA mask it armed from, and the BG mode above and below the split.
   * Written when the shape changes, plus a line for any frame whose shape
   * differs from BOTH its neighbours.
   *
   * Why: the HUD and the room are two register sets separated by a mid-frame
   * IRQ (IrqHandler_4_Main_BeginHudDraw sets TM=4 + BG3SC=0x5A at line 0,
   * IrqHandler_6_Main_EndHudDraw restores the room's at line 31). A frame that
   * keeps the HUD's set past line 31 draws the whole room from the HUD's
   * tilemap: one frame of full-screen garbage under an intact HUD, which is
   * what a player reports as a flicker. Nothing about it survives into the
   * next frame, so only a per-frame record catches it. */
  static int probe_checked;
  static FILE *probe;
  static char probe_last[256];
  char probe_now[256];
  int probe_len = 0;
  int probe_mode_hud = -1, probe_mode_room = -1;
  static uint8 probe_modes[225];
  if (!probe_checked) {
    const char *path = getenv("SM_RASTER_PROBE");
    probe_checked = 1;
    if (path) probe = fopen(path, "w");
  }
  if (probe)
    probe_len = snprintf(probe_now, sizeof(probe_now), "vIrq=%d vTimer=%d hIrq=%d armed=%02x irqs=",
                         g_snes->vIrqEnabled ? 1 : 0, g_snes->vTimer,
                         g_snes->hIrqEnabled ? 1 : 0, probe_armed);

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
      if (probe && probe_len < (int)sizeof(probe_now) - 8)
        probe_len += snprintf(probe_now + probe_len, sizeof(probe_now) - probe_len,
                              "%d,", i);
    }
    if (g_sm_video.enhanced || g_sm_video.fps_enabled)
      SmRendererCaptureLine(g_ppu, i);
    if (probe) {
      if (i == 10) probe_mode_hud = g_ppu->bgmode;
      if (i == 100) probe_mode_room = g_ppu->bgmode;
      if (i < 225) probe_modes[i] = g_ppu->bgmode;
    }
    ppu_runLine(g_ppu, i);
  }
  if (probe) {
    /* The frame's whole raster shape in one line: which registers the HUD/room
     * split left behind, the HDMA mask the presentation pass armed from, and
     * the mode the room was drawn in. A flicker is a frame whose shape differs
     * from BOTH its neighbours -- one frame out of an otherwise steady run --
     * so the probe keeps a one-frame window and reports exactly those. */
    if (probe_len < (int)sizeof(probe_now) - 64)
      snprintf(probe_now + probe_len, sizeof(probe_now) - probe_len,
               " tm=%02x irqh=%04x hdmaen=%02x mode=%02x/%02x bg3sc=%02x",
               g_ppu->screenEnabled[0], *(const uint16 *)(g_ram + 0xAB),
               g_snesrecomp_last_hdmaen, probe_mode_hud, probe_mode_room,
               g_ppu->bgXsc[2]);
    /* The BG mode the HDMA left on every line, run-length encoded: the
     * Ceres shaft is mode 7 below the mode-1 HUD, switched by HDMA channel 3
     * writing $2105, so the split shows up here as "0:09 32:07 ...". */
    char modes[256];
    int mlen = 0;
    for (int i = 0; i <= 224 && mlen < (int)sizeof(modes) - 12; i++)
      if (i == 0 || probe_modes[i] != probe_modes[i - 1])
        mlen += snprintf(modes + mlen, sizeof(modes) - mlen, "%d:%02x ", i,
                         probe_modes[i]);
    static char probe_prev[256], probe_prev2[256];
    static char modes_prev[256], modes_prev2[256];
    static unsigned probe_prev_frame;
    if (probe_prev[0] && strcmp(probe_prev2, probe_now) == 0 &&
        strcmp(probe_prev, probe_now) != 0)
      fprintf(probe, "%u ONE-FRAME ANOMALY %s\n    modes  %s\n"
                     "    steady %s\n    modes  %s\n",
              probe_prev_frame, probe_prev, modes_prev, probe_now, modes);
    snprintf(probe_prev2, sizeof(probe_prev2), "%s", probe_prev);
    snprintf(probe_prev, sizeof(probe_prev), "%s", probe_now);
    snprintf(modes_prev2, sizeof(modes_prev2), "%s", modes_prev);
    snprintf(modes_prev, sizeof(modes_prev), "%s", modes);
    probe_prev_frame = trace_frame;
    if (strcmp(probe_now, probe_last) != 0) {
      fprintf(probe, "%u %s\n", trace_frame, probe_now);
      fflush(probe);
      snprintf(probe_last, sizeof(probe_last), "%s", probe_now);
    }
  }
}
