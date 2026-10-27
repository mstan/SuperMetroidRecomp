#include "sm_rtl.h"
#include "sm_renderer.h"
#include "sm_spc_player.h"
#include "sm_pause_options.h"
#include "sm_replay.h"
#include "sm_activity.h"
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
/* Every policy uses the interruptible guest frame scheduler. Promoted
 * replacements are selected by the shared policy at their own call sites;
 * a policy name must not switch the title to a different execution model.
 * NMI shares g_cpu with main-line execution, so preserve its register file
 * and native tailcall context around the host-injected interrupt. */
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

static bool g_game_started = false;
static bool g_game_done = false;
static uint32_t g_lle_resume_pc = 0x808343u;

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
 * No host stack or pointer is part of a file snapshot. */
#define SM_FILE_STATE_MAGIC 0x534D4653u  /* SMFS */
#define SM_FILE_STATE_VERSION 4u
static uint32_t g_loaded_file_state[8];
static uint32_t g_loaded_pause_options;
static bool g_loaded_file_state_valid;

const char *SmSnapshotIdentity(void) {
#ifdef SM_SNAPSHOT_ID
  return SM_SNAPSHOT_ID;
#else
  return "sm2-development";
#endif
}

/* The framework verifies build identity and CRC first. Validate the title
 * extension before the common loader writes any guest bytes as well. The
 * canonical SM cartridge has no Cx4 execution-clock suffix. */
bool SmSnapshotPreflight(const void *data, size_t size) {
  uint32_t version, header[8], stream_version, execution_size, pause_options;
  size_t guest = RtlSnapshotGuestSize(&version);
  if (!data || size < 8 || !guest || guest > size || size - guest < sizeof(header) + 8)
    return false;
  memcpy(&stream_version, (const uint8_t *)data + 4, 4);
  memcpy(header, (const uint8_t *)data + guest, sizeof(header));
  if (stream_version != version || header[0] != SM_FILE_STATE_MAGIC || header[1] != SM_FILE_STATE_VERSION ||
      header[2] > 0xFFFFFFu || header[3] > 1 || header[4] > 1 ||
      header[6] != SmSpcPlayer_StateSize() || !SmActivityExtraValid(header[7])) return false;
  memcpy(&pause_options, (const uint8_t *)data + guest + sizeof(header), 4);
  if (pause_options > 1) return false;
  memcpy(&execution_size, (const uint8_t *)data + guest + sizeof(header) + 4, 4);
  size_t tail = size - guest - sizeof(header) - 8;
  return execution_size >= 8 && execution_size <= tail &&
      tail - execution_size == header[6];
}

void SmStateSaveExtra(struct SaveLoadInfo *sli) {
  uint32_t header[8] = {
    SM_FILE_STATE_MAGIC, SM_FILE_STATE_VERSION, g_lle_resume_pc,
    g_game_started ? 1u : 0u, g_game_done ? 1u : 0u,
    counter_global_frames, (uint32_t)SmSpcPlayer_StateSize(), SmActivityExtra()
  };
  sli->func(sli, header, sizeof(header));
  uint32_t pause_options = SmPauseOptions_IsActive() ? 1u : 0u;
  sli->func(sli, &pause_options, sizeof(pause_options));
  RtlSaveExecutionState(sli);
  if (header[6]) {
    void *spc = malloc(header[6]);
    if (!spc) { RtlStateStreamFail(sli); return; }
    if (SmSpcPlayer_SaveState(spc, header[6]) != header[6]) {
      free(spc); RtlStateStreamFail(sli); return;
    }
    sli->func(sli, spc, header[6]);
    free(spc);
  }
}

void SmStateLoadExtra(struct SaveLoadInfo *sli, uint32_t version) {
  (void)version;
  g_loaded_file_state_valid = false;
  sli->func(sli, g_loaded_file_state, sizeof(g_loaded_file_state));
  const uint32_t *h = g_loaded_file_state;
  if (h[0] != SM_FILE_STATE_MAGIC || h[1] != SM_FILE_STATE_VERSION || h[2] > 0xFFFFFFu ||
      h[3] > 1 || h[4] > 1 || h[6] != SmSpcPlayer_StateSize() || !SmActivityExtraValid(h[7])) {
    fprintf(stderr, "[sm_state] unrecognized execution-state extension\n");
    RtlStateStreamFail(sli);
    return;
  }
  sli->func(sli, &g_loaded_pause_options, sizeof(g_loaded_pause_options));
  if (g_loaded_pause_options > 1) { RtlStateStreamFail(sli); return; }
  if (!RtlLoadExecutionState(sli)) {
    fprintf(stderr, "[sm_state] execution residue does not match this runtime\n");
    RtlStateStreamFail(sli);
    return;
  }
  if (h[6]) {
    void *spc = malloc(h[6]);
    if (!spc) { RtlStateStreamFail(sli); return; }
    sli->func(sli, spc, h[6]);
    int ok = SmSpcPlayer_LoadState(spc, h[6]);
    free(spc);
    if (!ok) { RtlStateStreamFail(sli); return; }
  }
  g_loaded_file_state_valid = true;
}

void SmOnStateLoaded(uint32_t version) {
  SmReplayStateLoaded();
  if (!g_loaded_file_state_valid) {
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
  SmActivityRestore(g_loaded_file_state[7]);
  SmPauseOptions_SetActive(false);
  SmPauseOptions_SetActive(g_loaded_pause_options != 0);
  g_loaded_file_state_valid = false;
}

/* Rollback carries the scheduler continuation and SPC execution position.
 * The shared execution residue already owns the architectural CPU registers.
 * Build-specific snapshots intentionally have no legacy fiber conversion. */
typedef struct SmExecHeader {
  uint32_t magic, version, resume_pc, spc_bytes;
  uint16_t frames;
  uint8_t started, done, pause_options, selected_row;
  uint16_t activity_extra;
} SmExecHeader;
#define SM_EXEC_MAGIC 0x534D4558u
#define SM_EXEC_VERSION 5u

size_t SmExecStateBound(void) {
  return sizeof(SmExecHeader) + SmSpcPlayer_StateSize();
}

size_t SmExecStateSave(void *out, size_t capacity) {
  SmExecHeader hdr = {0};
  if (!out || capacity < SmExecStateBound()) return 0;
  hdr.magic = SM_EXEC_MAGIC;
  hdr.version = SM_EXEC_VERSION;
  hdr.resume_pc = g_lle_resume_pc;
  hdr.frames = counter_global_frames;
  hdr.started = g_game_started;
  hdr.done = g_game_done;
  hdr.pause_options = SmPauseOptions_IsActive();
  hdr.selected_row = (uint8_t)SmPauseOptions_Cursor();
  hdr.activity_extra = (uint16_t)SmActivityExtra();
  hdr.spc_bytes = (uint32_t)SmSpcPlayer_SaveState(
      (uint8_t *)out + sizeof(hdr), capacity - sizeof(hdr));
  if (hdr.spc_bytes != SmSpcPlayer_StateSize()) return 0;
  memcpy(out, &hdr, sizeof(hdr));
  return sizeof(hdr) + hdr.spc_bytes;
}

int SmExecStateLoad(const void *in, size_t size) {
  SmExecHeader hdr;
  if (!in || size < sizeof(hdr)) return 0;
  memcpy(&hdr, in, sizeof(hdr));
  if (hdr.magic != SM_EXEC_MAGIC || hdr.version != SM_EXEC_VERSION ||
      hdr.resume_pc > 0xFFFFFFu || hdr.started > 1 || hdr.done > 1 || hdr.pause_options > 1 ||
      !SmPauseOptions_CursorValid(hdr.selected_row) ||
      (!hdr.pause_options && hdr.selected_row) ||
      !SmActivityExtraValid(hdr.activity_extra) ||
      hdr.spc_bytes != SmSpcPlayer_StateSize() ||
      size - sizeof(hdr) < hdr.spc_bytes) return 0;
  if (hdr.spc_bytes && !SmSpcPlayer_LoadState(
          (const uint8_t *)in + sizeof(hdr), hdr.spc_bytes)) return 0;
  g_lle_resume_pc = hdr.resume_pc;
  counter_global_frames = hdr.frames;
  SmActivityRestore(hdr.activity_extra);
  SmPauseOptions_SetActive(hdr.pause_options != 0);
  SmPauseOptions_RestoreCursor(hdr.selected_row);
  g_game_started = hdr.started != 0;
  g_game_done = hdr.done != 0;
  return 1;
}

void RunOneFrameOfGame(void) {
  (void)snesrecomp_execution_policy();
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
   * could reach save RAM on the way. Stay stopped after a contained bail. */
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
     * state. Restore the interrupted main-line register file before resuming;
     * the generated RTI has no live guest PC to return to and therefore
     * cannot by itself preserve this continuation. */
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

  SmRendererBeginHardwareRaster(g_ppu);
  for (int i = 0; i <= 224; i++) {
    /* HDMA runs during the H-blank preceding each visible scanline. The
     * raster IRQ then selects the register set used for that line (the
     * vTimer=0 IRQ establishes the HUD before line 0 is drawn). Rendering
     * first leaves one scanline in the previous frame's state, which is
     * visible as a strip of the cleared mode-1 tilemap above the Ceres HUD. */
    for (int ch = 0; ch < 8; ch++)
      SimpleHdma_DoLine(&hdma_chans[ch]);
    if (i == trigger) {
      SmRendererHardwareVramChanged();
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
    if ((g_sm_video.enhanced || g_sm_video.fps_enabled) &&
        SmRendererClaimNativeLine(g_ppu, (unsigned)i))
      ppu_runLineState(g_ppu, i);
    else
      ppu_runLine(g_ppu, i);
  }
  SmRendererEndHardwareRaster();
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
