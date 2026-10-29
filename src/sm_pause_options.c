/* sm_pause_options.c — see sm_pause_options.h for why the third pause tab is
 * a host panel and not a recompiled screen. */

#include "sm_pause_options.h"

#include "sm_rom_glyphs.h"
#include <stddef.h>
#include <string.h>

#include "common_rtl.h"
#include "host_api.h"
#include "recomp_runtime_ui.h"
#include "snes_overlay_draw.h"
#include "snes_runahead.h"
#include "sm_video.h"
#include "sm_activity.h"
#include "variables.h"

/* kPauseMenuFuncs ($82:90FF) states this tab moves the guest between. Leaving
 * the panel uses the ROM's own tab change from its fade-out on: PauseMenu_2
 * fades out and continues to the Equipment Load, PauseMenu_5 to the Map Load,
 * and the FadeIn after it lands on the screen pausemenu_button_label_mode
 * names. */
enum {
  kPauseMenu_MapScreen = 0,
  kPauseMenu_EquipmentScreen = 1,
  kPauseMenu_FadeOutToEquipment = 2,
  kPauseMenu_FadeOutToMap = 5,
};
enum { kLabelMode_Map = 0, kLabelMode_Equipment = 2 };

/* Game states the panel can be up in: paused, and the unpause fade-out a
 * Start press begins ($82:9324). */
enum { kGameState_Paused = 0x0F, kGameState_UnpausingFadeOut = 0x10 };
static unsigned GameState(void) { return game_state; }
static uint8 Inidisp(void) { return reg_INIDISP; }

/* ── the ROM's own tab-change effects, reproduced verbatim ─────────────── */

/* QueueSfx1_Max6 ($80:9049/9051) with the id factored out: the ROM's own
 * SFX-1 queue write, including its three gates (debug-mute, pre-credits
 * game state, power-bomb explosion) and its full-ring replace-if-lower rule.
 * The queue is pumped once per frame by the guest, so a write here sounds on
 * this frame, exactly as a guest call to QueueSfx1_Max6(0x38) would. */
static void QueueSfx1Max6(uint8 id) {
  sfx_max_queued[0] = (uint8)((id << 8) | 6);
  if (((sfx_writepos[0] - sfx_readpos[0]) & 0xF) < 6) {
    if (!debug_disable_sounds && game_state < 0x28 &&
        (power_bomb_explosion_status & 0x8000) == 0) {
      uint8 wp = sfx_writepos[0];
      uint8 wp1 = (uint8)(wp + 1);
      if (wp1 >= 0x10) wp1 = 0;  /* the decomp's ring wrap: the pointer stays 0-15 */
      if (wp1 == sfx_readpos[0]) {
        if (id < sfx1_queue[wp])
          sfx1_queue[wp] = id;
      } else {
        sfx1_queue[wp] = id;
        sfx_writepos[0] = wp1;
        sfx1_queue[wp1] = 0;
      }
    }
  }
}

/* HandleFadeOut ($80:8924) and HandleFadeIn ($80:894D), the ROM's two INIDISP
 * fade steps, run on the guest's own words. The counter underflow is what
 * makes the step and the delay word is its reload value: delay 0 steps every
 * frame (the fast tab-change fade-out), delay 1 every other frame (the slow
 * fades). At level 1 a fade-out writes 0x80 (force-blank, black) and a
 * fade-in off 0x80 writes 1 (layer bits restored), full-byte. */
static void FadeStepOut(void) {
  if ((int16)(screen_fade_counter - 1) < 0) {
    screen_fade_counter = screen_fade_delay;
    if ((reg_INIDISP & 0xF) != 0) {
      if ((reg_INIDISP & 0xF) == 1)
        reg_INIDISP = 0x80;
      else
        reg_INIDISP = (reg_INIDISP & 0xF) - 1;
    }
  } else {
    --screen_fade_counter;
  }
}

static void FadeStepIn(void) {
  if ((int16)(screen_fade_counter - 1) < 0) {
    screen_fade_counter = screen_fade_delay;
    if (((reg_INIDISP + 1) & 0xF) != 0)
      reg_INIDISP = (reg_INIDISP + 1) & 0xF;
  } else {
    --screen_fade_counter;
  }
}

/* The host's enter fade is its own: there is no guest screen behind the third
 * tab, so none of the guest's fade states (PauseMenu_2-7) is involved. The
 * driver's only state is a phase marker that reuses the screen_fade_delay
 * word -- which is 0 at pause rest and written back to 0 by every paused
 * completion path -- so RAM snapshots and run-ahead rollback converge it with
 * the rest of the fade. The delay word is overloaded (cadence value when the
 * law runs, marker otherwise); the driver swaps it for the cadence, runs the
 * exact law, and swaps it back.
 *
 * It runs only while the guest is still on a pause tab (menu_index 0/1) with
 * a marker set. That is what keeps the host out of the guest's own fades: a
 * tab change runs on menu states 2-7 and the unpause in state 16+, where the
 * guest drives the same words itself (and whose Load states happen to write
 * the same delay value 1 the marker uses). */
enum { kFadeMarker_None = 0, kFadeMarker_Down = 1, kFadeMarker_Up = 2 };

static void FadeDriverStep(void) {
  const uint16 marker = screen_fade_delay;
  if (marker == kFadeMarker_None) return;
  if (menu_index != kPauseMenu_MapScreen &&
      menu_index != kPauseMenu_EquipmentScreen)
    return;
  screen_fade_delay = (marker == kFadeMarker_Up) ? 1 : 0;
  if (marker == kFadeMarker_Down)
    FadeStepOut();
  else
    FadeStepIn();
  screen_fade_delay = marker;
  const uint8 ini = reg_INIDISP;
  if (marker == kFadeMarker_Down) {
    /* Down finished at force-blank: hand off to the up phase. The first
     * up-phase frame is a no-step (counter 1 -> 0), matching the frame the
     * native Load state spends black. */
    if (ini == 0x80) {
      screen_fade_delay = kFadeMarker_Up;
      screen_fade_counter = 1;
    }
  } else if (!(ini & 0x80) && (ini & 0xF) == 0xF) {
    screen_fade_delay = kFadeMarker_None;
    screen_fade_counter = 0;
  }
}

/* The guest's pause screen acts only on its timed-held input:
 * ReleaseButtonsFilter(3) ($80:8146), which runs in the guest routine after
 * this tick, reports a button as newly held only once it has been stably
 * held through the decay window -- so the unpause Start edge lands several
 * frames after the press, and a quick Start tap produces no edge at all.
 * Mirror the filter's next result (all of its state is in guest RAM) so the
 * host hands the in-flight enter fade to the guest's own unpause on exactly
 * the frame the guest unpauses, and leaves an ignored tap's fade to run to
 * completion. Call after the pad is consumed: the guest then sees only the
 * Start bit, which is what the mask below reads. */
static bool GuestWillUnpauseThisFrame(void) {
  const uint16 newkeys = joypad1_newkeys & kButton_Start;
  const uint16 lastkeys = joypad1_lastkeys & kButton_Start;
  const uint16 held = (uint16)(~newkeys & lastkeys);
  if (held != joypad_released_keys) return false;  /* a release event resets the filter */
  if (((uint16)(timed_held_input_timer - 1) & 0x8000) == 0)
    return false;  /* still decaying: the filter clears, no edge */
  const uint16 prev = timed_held_input;
  return ((held & (uint16)(prev ^ held)) & kButton_Start) != 0;
}

/* ── model ────────────────────────────────────────────────────────────── */

static const char kSection[] = "Options";
static const char *g_aspect_choices[SM_ASPECT_COUNT];
/* The framework's choices are fixed vocabulary; the SM aspect names come from
 * sm_video at panel creation. */
static const char *g_fullscreen_choices[2] = { "Windowed", "Borderless" };
static const char *g_vsync_choices[3] = { "Off", "On", "Adaptive" };

static const RecompRuntimeUiItem kItems[] = {
  { "widescreen", kSection, "Widescreen", NULL,
    RECOMP_RUNTIME_UI_BOOL, 0, 1, 1, NULL, 0, NULL },
  /* Only meaningful with widescreen on, so disabled (and skipped) without. */
  { "hud", kSection, "HUD anchoring", NULL,
    RECOMP_RUNTIME_UI_BOOL, 0, 1, 1, NULL, 0, NULL },
  { "fps", kSection, "Presentation FPS", NULL,
    RECOMP_RUNTIME_UI_BOOL, 0, 1, 1, NULL, 0, NULL },
  { "aspect", kSection, "Aspect ratio", NULL,
    RECOMP_RUNTIME_UI_CHOICE, 0, SM_ASPECT_COUNT - 1, 1,
    g_aspect_choices, SM_ASPECT_COUNT, NULL },
  /* The framework's own display settings (host_api.h, snesrecomp PR #186).
   * Each change applies live to the running window; the host marks its
   * config dirty and writes config.ini at shutdown, so it survives the
   * restart. */
  { "fullscreen", kSection, "Fullscreen", NULL,
    RECOMP_RUNTIME_UI_CHOICE, 0, 1, 1,
    g_fullscreen_choices, 2, NULL },
  { "vsync", kSection, "VSync", NULL,
    RECOMP_RUNTIME_UI_CHOICE, 0, 2, 1,
    g_vsync_choices, 3, NULL },
  { "frame_blend", kSection, "Frame blend", NULL,
    RECOMP_RUNTIME_UI_BOOL, 0, 1, 1, NULL, 0, NULL },
  { "run_ahead", kSection, "Run ahead", NULL,
    RECOMP_RUNTIME_UI_INT, 0, SNES_RUNAHEAD_MAX, 1, NULL, 0, NULL },
  { "window_scale", kSection, "Window scale", NULL,
    RECOMP_RUNTIME_UI_INT, 1, 10 /* the host's kMaxWindowScale */, 1, NULL, 0, NULL },
  /* Graceful exit: the host ends the run on its next loop pass, through its
   * normal shutdown path, so any config the panel dirtied is written to
   * config.ini first. */
  { "exit", kSection, "Exit", NULL,
    RECOMP_RUNTIME_UI_ACTION, 0, 0, 0, NULL, 0, NULL },
  { "back", kSection, "Back to map", NULL,
    RECOMP_RUNTIME_UI_ACTION, 0, 0, 0, NULL, 0, NULL },
};

static RecompRuntimeUi *g_ui;
static bool g_leave;   /* "Back to map" was accepted this frame */

static bool *BoolField(const RecompRuntimeUiItem *item) {
  if (!strcmp(item->key, "widescreen")) return &g_sm_video.enhanced;
  if (!strcmp(item->key, "hud")) return &g_sm_video.hud_anchored;
  if (!strcmp(item->key, "fps")) return &g_sm_video.fps_enabled;
  return NULL;
}

static int GetValue(void *ctx, const RecompRuntimeUiItem *item, int *out) {
  (void)ctx;
  bool *b = BoolField(item);
  if (b) { *out = *b; return 1; }
  if (!strcmp(item->key, "aspect")) { *out = (int)g_sm_video.aspect; return 1; }
  if (!strcmp(item->key, "fullscreen")) { *out = snesrecomp_get_fullscreen(); return 1; }
  if (!strcmp(item->key, "vsync")) { *out = snesrecomp_get_vsync(); return 1; }
  if (!strcmp(item->key, "frame_blend")) { *out = snesrecomp_get_frame_blend(); return 1; }
  if (!strcmp(item->key, "run_ahead")) { *out = snesrecomp_get_run_ahead(); return 1; }
  if (!strcmp(item->key, "window_scale")) { *out = snesrecomp_get_window_scale(); return 1; }
  return 0;
}

static int SetValue(void *ctx, const RecompRuntimeUiItem *item, int value) {
  (void)ctx;
  if (!snesrecomp_host_frame_is_live()) return 1;
  bool *b = BoolField(item);
  if (b) {
    /* Turning widescreen on brings HUD anchoring up on Edges, so the
     * feature it gates is active straight away. */
    if (b == &g_sm_video.enhanced && value && !g_sm_video.enhanced)
      g_sm_video.hud_anchored = true;
    *b = value != 0;
    return 1;
  }
  if (!strcmp(item->key, "aspect") && value >= 0 && value < SM_ASPECT_COUNT) {
    g_sm_video.aspect = (SmAspect)value;
    return 1;
  }
  /* The host's setters clamp, apply the change to the running window, and
   * mark its config dirty for the shutdown write; the model only proposes
   * values inside each row's range, so all of them are accepted. */
  if (!strcmp(item->key, "fullscreen")) { snesrecomp_set_fullscreen(value); return 1; }
  if (!strcmp(item->key, "vsync")) { snesrecomp_set_vsync(value); return 1; }
  if (!strcmp(item->key, "frame_blend")) { snesrecomp_set_frame_blend(value); return 1; }
  if (!strcmp(item->key, "run_ahead")) { snesrecomp_set_run_ahead(value); return 1; }
  if (!strcmp(item->key, "window_scale")) { snesrecomp_set_window_scale(value); return 1; }
  return 0;
}

static int RunAction(void *ctx, const RecompRuntimeUiItem *item) {
  (void)ctx;
  if (!strcmp(item->key, "back")) {
    g_leave = true;
    return 1;
  }
  /* The host ends the run on its next loop pass -- after this frame has
   * presented -- through its normal shutdown path. */
  if (!strcmp(item->key, "exit")) {
    snesrecomp_request_exit();
    return 1;
  }
  return 0;
}

static int IsEnabled(void *ctx, const RecompRuntimeUiItem *item) {
  (void)ctx;
  if (!strcmp(item->key, "widescreen") || !strcmp(item->key, "aspect"))
    return SmActivityGeometryMutable();
  if (!strcmp(item->key, "hud")) return g_sm_video.enhanced;
  /* The host ignores a scale change while fullscreen, so the row is too. */
  if (!strcmp(item->key, "window_scale")) return !snesrecomp_get_fullscreen();
  return 1;
}

static void Save(void *ctx) {
  (void)ctx;
  if (!snesrecomp_host_frame_is_live()) return;
  SmVideoSave(&g_sm_video, SM_VIDEO_CONFIG_FILE);
}

static RecompRuntimeUi *Ui(void) {
  if (g_ui) return g_ui;
  for (int i = 0; i < SM_ASPECT_COUNT; ++i)
    g_aspect_choices[i] = SmAspectName((SmAspect)i);
  RecompRuntimeUiConfig config;
  memset(&config, 0, sizeof(config));
  config.title = "Game options";
  config.items = kItems;
  config.item_count = sizeof(kItems) / sizeof(kItems[0]);
  config.callbacks.get_value = GetValue;
  config.callbacks.set_value = SetValue;
  config.callbacks.run_action = RunAction;
  config.callbacks.is_enabled = IsEnabled;
  config.callbacks.save = Save;
  config.theme = "snes";
  config.navigation_flags = RECOMP_RUNTIME_UI_NAVIGATION_SKIP_DISABLED;
  g_ui = recomp_runtime_ui_create(&config);
  return g_ui;
}

/* Highlight the active tab's label cells in the guest's footer tilemap copy.
 * label_mode 0 = Map label lit (footer cols 1-4), 2 = Equipment label lit
 * (footer cols 5-9). Border cells (tile 0x001) keep palette 2. The active
 * tab's cells switch to palette 3 for a lit highlight. */
static void HighlightFooterTab(void);

/* Breathe the L/R navigation buttons' gray backdrop on the cadence of the
 * ROM's pause-screen palette animation (HandlePauseScreenPaletteAnimation,
 * $82:A92B): a 14-step cycle that holds step 0 -- the unlit rest -- for 15
 * frames and every other step for 3 frames (the step-hold table at $82:C10C),
 * so one full breath takes 54 frames, the same oscillation the pause map and
 * file-select screens run. The host reproduces the breath on the pill: the
 * body (tile tone 15) follows the animated palette's tone 15 (gray -> warm
 * tan -> bright orange at the peak) and the rim (tile tone 14) follows its
 * tone 5 (gray -> white), because the animation's own tone 14 stays black in
 * every step. The step resets to 0 whenever the panel opens so the breath
 * starts at rest. */
enum { kGlowStepCount = 14 };
static const uint8_t kGlowStepHolds[kGlowStepCount] = {
  15, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3,
};
static const uint32_t kGlowBodyColors[kGlowStepCount] = {
  0xFF636363u, 0xFF736352u, 0xFF846B4Au, 0xFF8C6B42u, 0xFF9C7339u,
  0xFFB57329u, 0xFFD67B18u, 0xFFFF8C00u, 0xFFD67B18u, 0xFFB57329u,
  0xFF9C7339u, 0xFF8C6B42u, 0xFF846B4Au, 0xFF736352u,
};
static const uint32_t kGlowRimColors[kGlowStepCount] = {
  0xFF9C9C9Cu, 0xFFA5A5A5u, 0xFFADADADu, 0xFFB5B5B5u, 0xFFC6C6C6u,
  0xFFCECECEu, 0xFFE7E7E7u, 0xFFFFFFFFu, 0xFFE7E7E7u, 0xFFCECECEu,
  0xFFC6C6C6u, 0xFFB5B5B5u, 0xFFADADADu, 0xFFA5A5A5u,
};
static int s_glow_step;
static int s_glow_countdown;
static void GlowFooterLrReset(void) {
  s_glow_step = 0;
  s_glow_countdown = kGlowStepHolds[0];
}

void SmPauseOptions_SetActive(bool active) {
  RecompRuntimeUi *ui = Ui();
  if (!ui || active == (recomp_runtime_ui_is_open(ui) != 0)) return;
  g_leave = false;
  if (active) {
    recomp_runtime_ui_open_section(ui, kSection);
         /* Populate the guest's footer tilemap copy from the ROM so the tab strip
     * shows the correct labels for the screen the panel is opening over.
     * Tab highlighting and the L/R button glow are applied each frame in
     * DrawPanel, after the guest's own pause routine has run its palette
     * animations. */
    SmRomPauseFooterUpdate();
    GlowFooterLrReset();
  } else
    recomp_runtime_ui_close(ui);
}

bool SmPauseOptions_IsActive(void) {
  return g_ui && recomp_runtime_ui_is_open(g_ui);
}

unsigned SmPauseOptions_Cursor(void) {
  return SmPauseOptions_IsActive()
      ? (unsigned)recomp_runtime_ui_view_selected_row(g_ui) : 0;
}

bool SmPauseOptions_CursorValid(unsigned row) {
  return row < sizeof(kItems) / sizeof(kItems[0]);
}

void SmPauseOptions_RestoreCursor(unsigned row) {
  if (!SmPauseOptions_IsActive() || !SmPauseOptions_CursorValid(row)) return;
  recomp_runtime_ui_open_section(g_ui, kSection);
  /* Navigation skips disabled rows. A setting changed outside the snapshot
   * may disable its old row; then choose the next enabled row. */
  for (unsigned i = 0; i < sizeof(kItems) / sizeof(kItems[0]); ++i) {
    unsigned before = SmPauseOptions_Cursor();
    if (before >= row) break;
    recomp_runtime_ui_handle_input(g_ui, RECOMP_RUNTIME_UI_INPUT_DOWN, 1, 0);
    if (SmPauseOptions_Cursor() == before) break;
  }
}

/* The panel is on its way out while the screen under it fades: a tab change
 * the panel started, or the unpause Start began. It stays drawn, dimmed with
 * the screen, and takes no input. All of it is read from guest RAM. */
static bool Leaving(void) {
  return GameState() == kGameState_UnpausingFadeOut ||
         menu_index == kPauseMenu_FadeOutToEquipment ||
         menu_index == kPauseMenu_FadeOutToMap;
}

void SmPauseOptions_SyncToGuest(void) {
  if (!SmPauseOptions_IsActive()) return;
  const unsigned state = GameState();
  /* No-pin model: the guest stays on whichever pause screen it was on, so the
   * panel stays open while that screen (Map or Equipment) is up, and through
   * the fade-out a tab change or unpause starts. When the fade lands on a Load
   * state (menu_index 3/4/6) or the game leaves the paused states, close. */
  const bool guest_on_pause_screen = menu_index == kPauseMenu_MapScreen ||
                                     menu_index == kPauseMenu_EquipmentScreen;
  const bool stay_open = (state == kGameState_Paused &&
                          (guest_on_pause_screen || Leaving())) ||
                         state == kGameState_UnpausingFadeOut;
  if (!stay_open) SmPauseOptions_SetActive(false);
}

/* ── guest side ───────────────────────────────────────────────────────── */

enum {
  kPadButtons = kButton_R | kButton_L | kButton_X | kButton_A | kButton_Right |
                kButton_Left | kButton_Down | kButton_Up | kButton_Start |
                kButton_Select | kButton_Y | kButton_B,
};

/* Swallow presses so the ROM's ReleaseButtonsFilter(3) computes no newly-held
 * edge for them. A newly-pressed button is also in joypad1_lastkeys (it is
 * held this frame), so clearing both is exact. */
static void ConsumePad(uint16 mask) {
  joypad1_newkeys &= (uint16)~mask;
  joypad1_lastkeys &= (uint16)~mask;
  newly_held_down_timed_held_input &= (uint16)~mask;
}

/* Highlight the active tab's label cells in the guest's footer tilemap copy.
 * label_mode 0 = Map label lit (footer cols 1-4), 2 = Equipment label lit
 * (footer cols 5-9). Border cells (tile 0x001) keep palette 2. The active
 * tab's cells switch to palette 3 for a lit highlight. */
static void HighlightFooterTab(void) {
  uint16_t *cells = (uint16_t *)(g_ram + 0x3000);
  const unsigned base = 25u * 32u;  /* kPauseFooterRow * kPauseTilemapCols */
  const uint16_t label_mode = (uint16_t)pausemenu_button_label_mode;
  for (int r = 0; r < 2; ++r) {  /* kPauseFooterRows */
    for (int c = 0; c < 32; ++c) {  /* kPauseTilemapCols */
      unsigned idx = base + r * 32u + c;
      uint16_t entry = cells[idx];
      unsigned tile = entry & 0x3FFu;
      if (tile == 0x001u) continue;  /* border/cap cells stay palette 2 */
      bool highlight = false;
      if (label_mode == kLabelMode_Map && c >= 1 && c <= 4) highlight = true;
      if (label_mode == kLabelMode_Equipment && c >= 5 && c <= 9) highlight = true;
      if (highlight)
        entry = (uint16_t)((entry & 0xFCFFu) | (3u << 10));  /* palette 3 */
      cells[idx] = entry;
    }
  }
}

/* Draw the L/R button glow on the navigation buttons' gray backdrop.
 * The original game animates a CGRAM palette each frame (plus a highlight
 * sprite) so the L/R button breathes warm on the pause screens; the panel
 * composites over that, so the host reproduces it by repainting the pill's
 * backdrop tones (footer cols 1-4 and 27-30 -- see SmRomPauseFooterGlowDraw)
 * with the current step's body and rim colours, leaving the arrow glyph
 * itself alone.
 *
 * Drawn after SmRomPauseFooterDraw. */
static void DrawFooterGlow(uint32_t *dst, int stride, int width, int height,
                           int x, int y);
static void DrawFooterGlow(uint32_t *dst, int stride, int width, int height,
                           int x, int y) {
  SmRomPauseFooterGlowDraw(dst, stride, width, height, x, y,
                           kGlowBodyColors[s_glow_step], kGlowRimColors[s_glow_step]);
}

/* Leave the panel for a ROM screen, riding the ROM's own tab change.
 * target is the label mode of the tab to end on (kLabelMode_*). A same-tab
 * leave -- R while the guest is already on Equipment, L or B on Map -- is
 * the ROM's total no-op (its L branch early-returns on Map, its R branch
 * refuses Equipment), so it just closes the panel with no SFX and no fade;
 * an in-flight enter fade keeps running on the guest's screen and finishes,
 * so the brightness always recovers.
 *
 * The pad press that caused the leave is already consumed by the caller, so
 * the guest's own shoulder handler sees no edge and the host's writes below
 * are the only tab change. */
static void LeaveTo(uint16 target_label_mode) {
  const uint16 current = (menu_index == kPauseMenu_EquipmentScreen)
      ? kLabelMode_Equipment : kLabelMode_Map;
  if (current == target_label_mode) {
    SmPauseOptions_SetActive(false);
    return;
  }
  /* The two writes HandlePauseScreenLrInput makes for this tab change ($82:
   * A50C): the fade-out state and the lit label it will land on. */
  menu_index = (current == kLabelMode_Map)
      ? kPauseMenu_FadeOutToEquipment : kPauseMenu_FadeOutToMap;
  pausemenu_button_label_mode = target_label_mode;
  pausemenu_shoulder_button_highlight =
      (target_label_mode == kLabelMode_Equipment) ? 2 : 1;
  pausemenu_start_lr_pressed_highlight_timer = 5;
  QueueSfx1Max6(0x38);
  /* The footer tilemap copy needs refreshing for the target tab's labels.
   * Tab highlighting and the L/R button glow are applied each frame in
   * DrawPanel, after the guest's own pause routine has run its palette
   * animations. */
  SmRomPauseFooterUpdate();
  /* The guest dispatches kPauseMenuFuncs[menu_index] at the top of its
   * routine, on this same frame -- whereas the native shoulder press changes
   * menu_index inside PauseMenu_0/1, after the dispatch, so the press frame
   * never steps the fade. A counter of 1 makes the guest's first
   * HandleFadeOut a no-step (1 -> 0) to keep the press frame's brightness,
   * and from the next frame the 0/0 fast fade runs exactly on the native
   * cadence. delay 0 doubles as the "no host fade" marker, taking the
   * host's enter driver out of the guest's own fade. */
  screen_fade_counter = 1;
  screen_fade_delay = kFadeMarker_None;
}

void SmPauseOptions_GuestTick(void) {
  static const struct { uint16 button; RecompRuntimeUiInput input; } kMenuButtons[] = {
    { kButton_Up,    RECOMP_RUNTIME_UI_INPUT_UP },
    { kButton_Down,  RECOMP_RUNTIME_UI_INPUT_DOWN },
    { kButton_Left,  RECOMP_RUNTIME_UI_INPUT_LEFT },
    { kButton_Right, RECOMP_RUNTIME_UI_INPUT_RIGHT },
    { kButton_A,     RECOMP_RUNTIME_UI_INPUT_ACCEPT },
  };
  /* While the panel is up it owns the pad. Only Start reaches the ROM's pause
   * screen beneath it, which unpauses on it as usual. Held buttons go too:
   * ReleaseButtonsFilter turns a button held for a few frames into the pause
   * screens' input, so a held D-pad would scroll the hidden map and a held R
   * would change tab again as soon as the panel had gone. */
  const uint16 kPanelOwned = (uint16)(kPadButtons & ~kButton_Start);
  const uint16 pressed = joypad1_newkeys;

  SmPauseOptions_SyncToGuest();

  /* The host's own enter fade, if in flight: step it first, before anything
   * else the host does this frame. It is deliberately not gated on the panel
   * being active: if a same-tab close cancelled the panel mid-fade, the fade
   * keeps running on the guest's screen and finishes, so the brightness
   * always recovers. The press frame that opened the panel is not a step
   * frame (the marker was still 0 when the driver ran), matching the native
   * tab change, whose press frame runs the old screen state: no step on the
   * press frame, 15 fast steps to force-blank, 1 no-step frame, 15 slow
   * steps back to full -- full again at press+45, 2 black frames; the
   * native tab change's Load frame makes it 46 frames and 3 blacks. */
  FadeDriverStep();

  if (SmPauseOptions_IsActive()) {
    /* Advance the pill breath once per game frame, mirroring the guest's
     * decrement-then-advance palette timer. Drawn later in DrawPanel, after
     * the guest's pause routine has run its animations. */
    if (--s_glow_countdown == 0) {
      s_glow_step = (s_glow_step + 1) % kGlowStepCount;
      s_glow_countdown = kGlowStepHolds[s_glow_step];
    }
    if (Leaving()) {
      ConsumePad(kPanelOwned);
      return;
    }
    ConsumePad(kPanelOwned);
    /* Start handoff. The panel never consumes Start, so the guest's own
     * HandlePauseScreenStart does the unpause: it queues the 0x38 SFX itself
     * (queueing one more here would double it) and writes the 1/1 slow fade
     * cadence itself. The only thing the host must do is take its enter
     * driver off the words -- and only on the frame the guest actually takes
     * the timed-held Start edge, which is a hold frame several frames after
     * the press, not the press frame. The prediction is exact (it mirrors
     * the guest's filter), so it is checked every frame and is the only
     * guard: a Start tap the filter will never turn into an edge leaves the
     * enter fade to run to completion. */
    if (GuestWillUnpauseThisFrame()) {
      screen_fade_delay = kFadeMarker_None;
      screen_fade_counter = 0;
    }
    /* The guest's own pause routine runs after this tick and keeps the footer
     * tilemap current between frames, including palette animations. The panel
     * applies tab highlighting and the L/R button glow right before drawing,
     * in DrawPanel. */
    if (pressed & kButton_R) {
      /* R leaves toward Equipment, the ROM's Map-screen shoulder direction. */
      LeaveTo(kLabelMode_Equipment);
      return;
    }
    if (pressed & kButton_L) {
      /* L leaves toward Map, the ROM's Equipment-screen shoulder direction. */
      LeaveTo(kLabelMode_Map);
      return;
    }
    for (size_t i = 0; i < sizeof(kMenuButtons) / sizeof(kMenuButtons[0]); ++i)
      if (pressed & kMenuButtons[i].button) {
        recomp_runtime_ui_handle_input(g_ui, kMenuButtons[i].input, 1, 0);
        QueueSfx1Max6(0x37);  /* row nav SFX */
      }
    if (g_leave || (pressed & kButton_B)) {
      g_leave = false;
      LeaveTo(kLabelMode_Map);
      return;
    }
  } else {
    /* The tab ring is Map -> Equipment -> Options -> Map (R forward, L back).
     * The ROM ignores L on Map and R on Equipment, so these are free. No-pin
     * model: do NOT write menu_index=MapScreen here; let the ROM's own L
     * handler run under the panel so the panel opens over whatever screen the
     * guest is on (its real tab), avoiding the hop+beep of forcing the Map
     * screen. The enter rides the ROM's tab-change effects: the 0x38 SFX and
     * a host-driven fast fade-out / slow fade-in on the screen it opens over,
     * the guest staying on its own tab underneath. */
    if ((menu_index == kPauseMenu_EquipmentScreen && (pressed & kButton_R)) ||
        (menu_index == kPauseMenu_MapScreen && (pressed & kButton_L))) {
      QueueSfx1Max6(0x38);  /* tab change SFX */
      SmPauseOptions_SetActive(true);
      screen_fade_delay = kFadeMarker_Down;
      screen_fade_counter = 0;
      ConsumePad(kPanelOwned);
    }
  }
}

/* ── rendering ────────────────────────────────────────────────────────── */

/* Panel colours (ARGB8888). */
#define COL_BG     0xFF0A0E18u
#define COL_TITLE  0xFFFFFFFFu
#define COL_TEXT   0xFFC2CCE0u
#define COL_SELBG  0xFF2B4A8Cu
#define COL_SEL    0xFFFFFFFFu
#define COL_DIM    0xFF8A98B4u
#define COL_ACCENT 0xFF6BE06Bu

static int TextLen(const char *s) { return (int)strlen(s); }

static void DrawCentered(uint32_t *dst, int stride, int h_max, int width,
                         int y, const char *s, uint32_t col, int scale) {
  int x = (width - TextLen(s) * 8 * scale) / 2;
  if (x < 0) x = 0;
  snes_ovl_draw_text(dst, stride, h_max, x, y, s, col, scale);
}

bool SmPauseOptions_Visible(void) {
  const unsigned state = GameState();
  if (!SmPauseOptions_IsActive() ||
      (state != kGameState_Paused && state != kGameState_UnpausingFadeOut))
    return false;
  /* Hidden through the host's own enter fade-out: the guest screen underneath
   * is what dims to black there, exactly as in a native tab change, and the
   * panel reappears (dimmed) on the fade-in. The paused-state clause keeps
   * the guest's unpause fade (state 16, which also runs with the delay word
   * at 1) out of this: there the panel stays up, dimmed with the screen. */
  if (state == kGameState_Paused &&
      (menu_index == kPauseMenu_MapScreen || menu_index == kPauseMenu_EquipmentScreen) &&
      screen_fade_delay == kFadeMarker_Down)
    return false;
  return true;
}

/* The panel fades with the screen under it: the PPU's master brightness
 * (INIDISP 0-15, force-blank = black) applied to it. The level comes from
 * INIDISP itself rather than a host fade flag, because INIDISP is where every
 * fade lands -- the host's enter fade, the guest's tab change, the guest's
 * unpause -- so the panel tracks whichever one is running. Self-gating at
 * full brightness. */
static void DimToScreenBrightness(uint32_t *dst, int stride, int width, int height) {
  const uint8 inidisp = Inidisp();
  const unsigned level = (inidisp & 0x80) ? 0 : (inidisp & 0x0F);
  if (level >= 15) return;
  for (int y = 0; y < height; ++y) {
    uint32_t *row = dst + (size_t)y * (size_t)stride;
    for (int x = 0; x < width; ++x) {
      const uint32_t p = row[x];
      const uint32_t r = ((p >> 16) & 0xFF) * level / 15;
      const uint32_t g = ((p >> 8) & 0xFF) * level / 15;
      const uint32_t b = (p & 0xFF) * level / 15;
      row[x] = (p & 0xFF000000u) | (r << 16) | (g << 8) | b;
    }
  }
}

static void DrawPanel(uint32_t *dst, int stride, int width, int height);

void SmPauseOptions_Render(uint32_t *dst, int stride, int width, int height) {
  if (!dst || stride <= 0 || height <= 0 || !SmPauseOptions_IsActive()) return;
  if (width > stride) width = stride;
  DrawPanel(dst, stride, width, height);
  const uint8 inidisp = Inidisp();
  if ((inidisp & 0x80) || (inidisp & 0x0F) < 0x0F)
    DimToScreenBrightness(dst, stride, width, height);
}

static void DrawPanel(uint32_t *dst, int stride, int width, int height) {
  /* Clip to the ROM game rect (256x224) with black margins. Width is the window
   * width, which may be larger due to widescreen; extra margin is half the
   * difference. */
  const int game_width = 256;
  const int game_height = 224;
  const int extra = (width - game_width) / 2;

  /* Fill the game rect with the panel background. */
  snes_ovl_fill_rect(dst, stride, height, extra, 0, game_width, game_height, COL_BG);

  /* Black margins: left of extra, right of extra+game_width to width. */
  if (extra > 0) {
    snes_ovl_fill_rect(dst, stride, height, 0, 0, extra, height, 0xFF000000u);
    snes_ovl_fill_rect(dst, stride, height, extra + game_width, 0, width - extra - game_width, height, 0xFF000000u);
  }

  /* Top edge: the guest's own HUD, exactly what the Map and Equipment pause
   * screens show there. The tilemap at g_ram+0xC608 is the guest's BG3 copy,
   * kept current by HandleHudTilemap while paused; its tiles and palettes are
   * read from the cartridge in place. */
  SmRomHudTopRowDraw(dst, stride, width, height, extra, 0);
  SmRomHudDraw(dst, stride, width, height, extra, 7,
               (const uint16_t *)(g_ram + 0xC608), 32, 3);

  /* The window itself: the pause screen's BG2 frame at its native 256x160
   * footprint (rows 5..24 of the ROM tilemap), with the 12-tile label gap in
   * the top edge. GAME OPTIONS is exactly 12 small-font cells wide. */
  SmRomPauseFrameDraw(dst, stride, width, height,
                      extra, 40, game_width, 160, 10, 12);
  SmRomGlyphDraw(dst, stride, width, height,
                 extra + 10 * 8, 40, "GAME OPTIONS", COL_TITLE, 1);

  /* Rows, as the model reports them. The 8x8 font is upper-case only, so
   * "On" and "Fit" draw as ON and FIT. Eleven rows at 13px pitch from y=50
   * reach 187, inside the frame's y=48..191 interior. */
  {
    const size_t count = recomp_runtime_ui_view_row_count(g_ui);
    const size_t selected = recomp_runtime_ui_view_selected_row(g_ui);
    int row_y = 50;
    const int row_h = 13;
    for (size_t i = 0; i < count; ++i, row_y += row_h) {
      RecompRuntimeUiRowView row;
      if (!recomp_runtime_ui_view_row(g_ui, i, &row) || !row.item) continue;
      const bool sel = i == selected;
      /* HUD anchoring sits indented under the Widescreen row it depends on. */
      const int indent = !strcmp(row.item->key, "hud") ? 4 * 8 : 0;
      uint32_t label_col = sel ? COL_SEL : (row.enabled ? COL_TEXT : COL_DIM);
      uint32_t value_col = sel ? COL_ACCENT : (row.enabled ? COL_TEXT : COL_DIM);
      if (sel)
        snes_ovl_fill_rect(dst, stride, height, extra + 14 + indent, row_y - 2,
                           game_width - 28 - indent, 13, COL_SELBG);
      snes_ovl_draw_text(dst, stride, height, extra + 22 + indent, row_y,
                         row.label, label_col, 1);
      if (row.item->type != RECOMP_RUNTIME_UI_ACTION && row.value[0])
        snes_ovl_draw_text(dst, stride, height,
                           extra + game_width - 22 - indent - TextLen(row.value) * 8, row_y,
                           row.value, value_col, 1);
    }
  }

  /* Bottom tab strip: the pause screen's own footer (BG2 tilemap rows 25-26)
   * at its native position, flush below the frame. Its tab labels, the L/R
   * navigation arrows and the START prompt are all cartridge art read in
   * place -- nothing host-drawn. The footer tilemap copy at g_ram+0x3000 is
   * refreshed when the panel opens or changes tabs (see SmPauseOptions_SetActive
   * and LeaveTo); the guest's own pause routine keeps it current between frames,
   * including palette animations. Apply tab highlighting and the L/R button
   * glow here, right before drawing, after the guest's routine has run. The 8px
   * row below it (tilemap row 27) stays the panel background. */
  HighlightFooterTab();
  SmRomPauseFooterDraw(dst, stride, width, height, extra, game_height - 24);
  DrawFooterGlow(dst, stride, width, height, extra, game_height - 24);
}
