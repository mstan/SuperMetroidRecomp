/*
 * Super Metroid — desktop host shim.
 *
 * Identity and hooks, nothing else. The host itself is the framework's
 * (snesrecomp/runner/src/desktop/host_main.c): launcher, ROM resolution,
 * config, window, presenters, audio, gamepads, overlays, pacing, crash
 * reporting. Until 2026-09 all of that was a 2,800-line copy in this file,
 * which is why a second scaffold of this game booted to a black frame while
 * this one played: the fixes lived here, not one level up.
 *
 * What stays here is what is actually Super Metroid:
 *   - the read-only custom widescreen renderer (sm_renderer.c) and its
 *     aspect/fps policy (sm_video.c), presented through the draw_frame hook;
 *   - the SPC upload interception (sm_spc_player.c);
 *   - the door-transition pacing rule: those frames may repay wall-time debt
 *     so the guest-driven audio queue stays fed while Samus has no control;
 *   - the presentation Mods page (sm_mods.c);
 *   - SM_AUDIO_PROBE, a per-frame audio/doorway measurement.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef SM_NATIVE_RASTER
#define SM_NATIVE_RASTER 0
#endif

#ifndef __ANDROID__
#define SDL_MAIN_HANDLED 1
#endif
#include "desktop/sdl_compat.h"
#ifdef __ANDROID__
/* SDLActivity loads libmain.so and dlsyms its SDL_main. */
#define main SDL_main
#endif

#include "host_main.h"
#include "host_clock.h"
#include "common_rtl.h"
#include "common_cpu_infra.h"
#include "cpu_state.h"
#include "host_report.h"
#include "config.h"
#include "widescreen.h"
#include "audio_trace.h"
#include "snes/apu.h"
#include "snes/snes.h"
#include "snesrecomp_rom_identity.h"  /* generated from rom_identity.txt */

#include "sm_display.h"
#include "sm_renderer.h"
#include "sm_video.h"
#include "sm_pause_options.h"
#include "sm_spc_player.h"
#include "sm_replay.h"
#include "sm_startup.h"
#include "sm_activity.h"
#if defined(RECOMP_LAUNCHER)
#include "sm_mods.h"
#endif

#ifndef SNESRECOMP_BUILD_VERSION
#define SNESRECOMP_BUILD_VERSION "dev"
#endif

extern const RtlGameInfo kSuperMetroidGameInfo;
extern Snes *g_snes;
extern uint8_t g_ram[0x20000];

static const char *kSmVideoConfig = SM_VIDEO_CONFIG_FILE;
static SmViewport g_sm_viewport;
static uint32_t g_sm_output[SM_MAX_WIDTH * SM_HEIGHT];
static uint64_t g_sm_capture_generation;
#if SM_NATIVE_RASTER
static bool g_sm_composed_ready, g_sm_render_valid;
static SmViewport g_sm_composed_view;
static bool g_sm_composed_hud;
#endif

/* The shared generation changes on visible timeline jumps and is preserved
 * across speculative rollback. A rollback's on_state_loaded hook must not
 * erase the future raster that run-ahead has just captured for presentation. */
static void SmSyncCaptureGeneration(void) {
  uint64_t generation = RtlStateGeneration();
  if (generation != g_sm_capture_generation) {
    SmRendererReset();
#if SM_NATIVE_RASTER
    g_sm_composed_ready = false;
#endif
    g_sm_capture_generation = generation;
  }
}

static bool SmCustomRendererEnabled(void) {
  return g_sm_video.enhanced || g_sm_video.fps_enabled || SmActivityNetworkWide();
}

bool SmDisplay_IsWidescreenActive(void) { return g_sm_viewport.enhanced; }
int SmDisplay_GetCurrentFrameWidth(void) { return snesrecomp_desktop_frame_width(); }

static void SmAfterConfig(void) {
  if (SmReplayPlaying()) {
    g_config.autosave = false;
    /* The host writes SRAM on exit even without autosave. Playback must never
     * overwrite the player's progress, including direct CLI playback. */
    RtlSetSaveRoot("replay-saves");
    RtlSetQuickSaveRoot("replay-saves");
  }
  if (!SmVideoLoad(&g_sm_video, kSmVideoConfig))
    fprintf(stderr, "[video] Invalid settings in %s; valid entries retained.\n", kSmVideoConfig);
  SmRendererReset();
  /* Compatibility with existing capture commands: these select the custom
   * renderer, never the old guest activation/culling overrides. */
  {
    const char *ws_env = getenv("SNESRECOMP_WIDESCREEN");
    if (ws_env && *ws_env) g_sm_video.enhanced = atoi(ws_env) != 0;
    const char *aspect = getenv("SM_VIDEO_ASPECT");
    if (aspect && !SmParseAspect(aspect, &g_sm_video.aspect))
      fprintf(stderr, "[video] Ignoring invalid SM_VIDEO_ASPECT\n");
  }
  g_sm_viewport = SmCalculateViewport(&g_sm_video, 16, 9);
  host_report_breadcrumb("custom renderer: %d aspect=%s fps=%u hud=%d",
                         SmCustomRendererEnabled(), SmAspectName(g_sm_video.aspect),
                         g_sm_video.fps, g_sm_video.hud_anchored);
}

static void SmOnRomLoaded(const uint8_t *rom, size_t size) {
  SmReplayRomLoaded(rom, (unsigned)size);
  SmRendererSetRom(rom, size);
}

static void SmOnReset(void) {
  SmRendererReset();
#if SM_NATIVE_RASTER
  g_sm_composed_ready = false;
#endif
  /* The host also sends this notification after loading a snapshot. Keep
   * the panel flag restored by SmOnStateLoaded when the guest is paused;
   * ordinary gameplay and actual reset states close it through this sync. */
  SmPauseOptions_SyncToGuest();
}

/* Door loading can take longer than a frame. Keep its debt only while the
 * guest is in its non-interactive transition states: those frames refill the
 * guest-driven SPC queue. The moment state 8 gameplay returns, drop any
 * remainder so Samus can never burst forward after the doorway. */
static int SmKeepPacingDebt(void) {
  uint8_t game_state = g_ram[0x0998];
  return game_state >= 9 && game_state <= 11;
}

static void SmPrepareFrame(int drawable_w, int drawable_h, int *frame_w, int *frame_h) {
  g_sm_viewport = SmCalculateViewport(&g_sm_video, drawable_w, drawable_h);
  SmActivityViewport(&g_sm_viewport);
  SmReplayViewport(&g_sm_viewport);
  *frame_w = g_sm_viewport.width;
  *frame_h = SM_HEIGHT;
}

/* Simulation owns these: the renderer's per-frame capture runs exactly once
 * per simulated frame, never for an interpolated present. */
static void SmBeginSimFrame(unsigned number) {
  SmSyncCaptureGeneration();
  SmPauseOptions_SyncToGuest();
  if (SmCustomRendererEnabled()) SmRendererBeginFrame(g_ram, number);
}

static void SmEndSimFrame(const uint8_t *field, unsigned number) {
#if SM_NATIVE_RASTER
  g_sm_render_valid = false;
  if (SmCustomRendererEnabled())
    g_sm_render_valid = SmRendererEndFrame((const uint32_t *)field);
#else
  if (SmCustomRendererEnabled())
    SmRendererEndFrame((const uint32_t *)field);
#endif
  SmReplayRendered();
  const char *capture_frame = getenv("SM_CAPTURE_FRAME");
  const char *capture_path = getenv("SM_CAPTURE_PATH");
  if (capture_frame && capture_path && number == strtoul(capture_frame, NULL, 10))
    SmRendererSaveCapture(capture_path);
}

#if SM_NATIVE_RASTER
static void SmPublishNativeField(uint8_t *field, unsigned number) {
  (void)number;
  g_sm_composed_ready = false;
  if (!SmCustomRendererEnabled()) return;
  if (!g_sm_render_valid ||
      !SmRendererDraw(g_sm_output, g_sm_viewport, g_sm_video.hud_anchored, 1)) {
    if (SmRendererNativeLines()) abort();
    return;
  }
  g_sm_composed_view = g_sm_viewport;
  g_sm_composed_hud = g_sm_video.hud_anchored;
  g_sm_composed_ready = true;
  for (unsigned y = 0; y < 224; ++y)
    memcpy((uint32_t *)field + y * 256,
        g_sm_output + y * g_sm_viewport.width + g_sm_viewport.extra, 256 * 4);
}
#endif

static int SmDrawFrame(uint8_t *dst, size_t pitch, const uint8_t *field,
                       int frame_w, int frame_h, double alpha) {
  const bool overlay = SmPauseOptions_Visible();
  if (!SmCustomRendererEnabled() && !overlay)
    return 0;
  SmSyncCaptureGeneration();
  int drew = 0;
#if SM_NATIVE_RASTER
  drew = g_sm_composed_ready && alpha >= 1 &&
      g_sm_composed_view.width == g_sm_viewport.width &&
      g_sm_composed_view.extra == g_sm_viewport.extra &&
      g_sm_composed_hud == g_sm_video.hud_anchored;
  g_sm_composed_ready = false;
#endif
  if (!drew && SmCustomRendererEnabled())
    drew = SmRendererDraw(g_sm_output, g_sm_viewport, g_sm_video.hud_anchored, alpha);
  if (!drew) {
    memset(g_sm_output, 0, sizeof(g_sm_output));
    for (int y = 0; y < SM_HEIGHT; ++y)
      memcpy(g_sm_output + y * frame_w + g_sm_viewport.extra,
             field + y * 256 * 4, 256 * 4);
  }
  if (overlay)
    SmPauseOptions_Render(g_sm_output, frame_w, frame_w, frame_h);
  RtlWidescreenPresent(dst, pitch, (const uint8_t *)g_sm_output, frame_w, frame_h);
  return 1;
}

/* Presentation-only iterations reuse a captured simulation frame, so they
 * are custom-renderer only; without the fps mod the picture is presented
 * in lockstep with the simulation (and vsync stays on). */
static double SmPresentationHzHook(double display_refresh) {
  if (!SmCustomRendererEnabled() || !g_sm_video.fps_enabled)
    return 0;
  return SmPresentationHz(g_sm_video.fps, display_refresh > 0 ? display_refresh : 60);
}

#if defined(RECOMP_LAUNCHER)
static const struct RecompLauncherCModProvider *SmMods(void) {
  return SmModsProvider(&g_sm_video, kSmVideoConfig);
}
#endif

/* SM_AUDIO_PROBE=<csv>: production-path measurement. Sampled after guest
 * execution so a trace proves that the saved doorway was crossed, and
 * attributes missing PCM to the transition rather than boot or loading the
 * save itself. Kept in memory until close: frequent small writes can block
 * behind filesystem/antivirus work and create the very underruns being
 * measured. */
static FILE *g_audio_probe;
static void SmCloseAudioProbe(void) {
  if (g_audio_probe) fclose(g_audio_probe);
  g_audio_probe = NULL;
}
static void SmAfterRunFrame(const SnesDesktopHostFrameStats *st) {
  if (SmReplayPlaying() && (SmReplayFinished() || SmReplayFailed())) {
    SDL_Event quit = {0}; quit.type = SDL_QUIT; SDL_PushEvent(&quit);
  }
  static int probe_checked;
  if (!probe_checked) {
    probe_checked = 1;
    const char *path = getenv("SM_AUDIO_PROBE");
    g_audio_probe = path ? fopen(path, "w") : NULL;
    if (g_audio_probe) {
      setvbuf(g_audio_probe, NULL, _IOFBF, 1024 * 1024);
      fprintf(g_audio_probe, "frame,seconds,guest_ms,state,door_step,room,master,port_clock,guest_anchor,target_anchor,last_guest,last_target,produced,consumed,underflows,occupancy,missing_frames,dropped,output_rate\n");
      atexit(SmCloseAudioProbe);
    }
  }
  if (!g_audio_probe) return;
  AudioTraceStats stats;
  audio_trace_get_stats(&stats);
  Apu *apu = g_snes->apu;
  fprintf(g_audio_probe, "%d,%.6f,%.3f,%u,%04x,%04x,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%u,%llu,%llu,%d\n",
      snes_frame_counter, st->run_seconds, st->guest_seconds * 1000,
      g_ram[0x998] | g_ram[0x999] << 8, g_ram[0x99c] | g_ram[0x99d] << 8,
      g_ram[0x79b] | g_ram[0x79c] << 8,
      (unsigned long long)g_cpu.master_cycles,
      (unsigned long long)apu->portClock, (unsigned long long)apu->portGuestAnchor,
      (unsigned long long)apu->portTargetAnchor, (unsigned long long)apu->portLastGuest,
      (unsigned long long)apu->portLastTarget, (unsigned long long)stats.produced,
      (unsigned long long)stats.consumed, (unsigned long long)stats.output_underflows,
      stats.occupancy_current,
      (unsigned long long)stats.output_missing_frames,
      (unsigned long long)stats.dropped, st->audio_output_rate);
}

static const SnesDesktopHostGame kSuperMetroidHost = {
  .display_name = "Super Metroid",
  .window_title = "Super Metroid (Recompiled)",
  .region = SNESRECOMP_ROM_REGION,
  .rom_file = SNESRECOMP_ROM_FILE,
  .expected_sha256_hex = SNESRECOMP_ROM_EXPECTED_SHA256,
  .expected_crc32_hex = SNESRECOMP_ROM_EXPECTED_CRC32,
  .game_id = SNESRECOMP_ROM_GAME_ID,
  .build_version = SNESRECOMP_BUILD_VERSION,
  .env_prefix = "SM",
  .sram_path = "saves/save.srm",   /* battery SRAM: the launcher shows SAVES */
  .game_info = &kSuperMetroidGameInfo,
  .num_players = 1,
  /* Per-game debug server port: 4377 SMW, 4378 Zelda LttP, 4379 MMX, 4380 SM. */
  .debug_port = 4380,
  .widescreen_supported = 0,       /* widescreen is this title's Mods page */
  .msu1_supported = 0,
  .shader_supported = 1,
  .simulation_hz = SM_SIMULATION_HZ,
  .frame_width = 256,
  .frame_height = SM_HEIGHT,

  .create_spc_player = &SmSpcPlayer_Create,
#if defined(RECOMP_LAUNCHER)
  .mods_provider = &SmMods,
#endif
  .after_config = &SmAfterConfig,
  .on_rom_loaded = &SmOnRomLoaded,
  .on_reset = &SmOnReset,
  .after_run_frame = &SmAfterRunFrame,
  .on_shutdown = &SmReplayShutdown,
  .keep_pacing_debt = &SmKeepPacingDebt,

  .prepare_frame = &SmPrepareFrame,
  .begin_sim_frame = &SmBeginSimFrame,
  .end_sim_frame = &SmEndSimFrame,
#if SM_NATIVE_RASTER
  .publish_native_field = &SmPublishNativeField,
#endif
  .draw_frame = &SmDrawFrame,
  .presentation_hz = &SmPresentationHzHook,
  .window_base_width = &SmDisplay_GetWindowBaseWidth,
  .window_base_height = &SmDisplay_GetWindowBaseHeight,
};

#ifndef __ANDROID__
#undef main   /* desktop: keep plain main() even if SDL_main.h remapped it */
#endif
int main(int argc, char **argv) {
  SmStartupLogging();
  for (int i = 1; i < argc; ++i) {
    if (!strcmp(argv[i], "--config") && i + 1 < argc)
      SmReplaySetConfigPath(argv[i + 1]);
    if (strcmp(argv[i], "--replay-diagnostic")) continue;
    if (i + 1 >= argc || !SmReplayPlayback(argv[i + 1])) return 2;
    memmove(argv + i, argv + i + 2, (size_t)(argc - i - 1) * sizeof(*argv));
    argc -= 2; --i;
  }
  if (SmReplayPlaying()) {
    for (int i = 1; i < argc; ++i) {
      if (!strcmp(argv[i], "--resume-state") || !strcmp(argv[i], "--script")) {
        fprintf(stderr, "Replay playback cannot be combined with --resume-state or --script.\n");
        SmReplayShutdown();
        return 2;
      }
    }
  }
  int result = snesrecomp_desktop_main(&kSuperMetroidHost, argc, argv);
  SmReplayShutdown();
  return SmReplayPlaying() && (SmReplayFailed() || !SmReplayFinished()) ? 2 : result;
}
