/* sm_pause_options.c — see sm_pause_options.h for why the third pause tab is
 * a host panel and not a recompiled screen. */

#include "sm_pause_options.h"

#include <stddef.h>
#include <string.h>

#include "common_rtl.h"
#include "host_api.h"
#include "recomp_runtime_ui.h"
#include "snes_overlay_draw.h"
#include "snes_runahead.h"
#include "sm_video.h"
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

static unsigned GameState(void) { return g_ram[0x0998]; }
static uint8 Inidisp(void) { return g_ram[0x0051]; }  /* reg_INIDISP shadow */

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
  if (!strcmp(item->key, "hud")) return g_sm_video.enhanced;
  /* The host ignores a scale change while fullscreen, so the row is too. */
  if (!strcmp(item->key, "window_scale")) return !snesrecomp_get_fullscreen();
  return 1;
}

static void Save(void *ctx) {
  (void)ctx;
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

void SmPauseOptions_SetActive(bool active) {
  RecompRuntimeUi *ui = Ui();
  if (!ui || active == (recomp_runtime_ui_is_open(ui) != 0)) return;
  g_leave = false;
  if (active) recomp_runtime_ui_open_section(ui, kSection);
  else recomp_runtime_ui_close(ui);
}

bool SmPauseOptions_IsActive(void) {
  return g_ui && recomp_runtime_ui_is_open(g_ui);
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
  /* Closed once the fade it was waiting on is over: the guest is in a Load
   * state, or the game has moved past the paused states. While open, the
   * tick holds menu_index on the Map screen, so nothing else is reachable. */
  if (state == kGameState_Paused ? !(Leaving() || menu_index == kPauseMenu_MapScreen)
                                 : state != kGameState_UnpausingFadeOut)
    SmPauseOptions_SetActive(false);
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

/* Leave the panel for a ROM screen, starting from the fade-out. */
static void LeaveTo(uint16 fade_out_state, uint16 label_mode) {
  menu_index = fade_out_state;
  pausemenu_button_label_mode = label_mode;
}

void SmPauseOptions_GuestTick(void) {
  static const struct { uint16 button; RecompRuntimeUiInput input; } kMenuButtons[] = {
    { kButton_Up,    RECOMP_RUNTIME_UI_INPUT_UP },
    { kButton_Down,  RECOMP_RUNTIME_UI_INPUT_DOWN },
    { kButton_Left,  RECOMP_RUNTIME_UI_INPUT_LEFT },
    { kButton_Right, RECOMP_RUNTIME_UI_INPUT_RIGHT },
    { kButton_A,     RECOMP_RUNTIME_UI_INPUT_ACCEPT },
  };
  /* While the panel is up it owns the pad. Only Start reaches the ROM's Map
   * screen beneath it, which unpauses on it as usual. Held buttons go too:
   * ReleaseButtonsFilter turns a button held for a few frames into the pause
   * screens' input, so a held D-pad would scroll the hidden map and a held R
   * would change tab again as soon as the panel had gone. */
  const uint16 kPanelOwned = (uint16)(kPadButtons & ~kButton_Start);
  const uint16 pressed = joypad1_newkeys;

  SmPauseOptions_SyncToGuest();
  if (SmPauseOptions_IsActive() && Leaving()) {
    ConsumePad(kPanelOwned);
    return;
  }
  if (!SmPauseOptions_IsActive()) {
    /* The tab ring is Map -> Equipment -> Options -> Map (R forward, L back).
     * The ROM ignores L on Map and R on Equipment, so these are free. */
    if ((menu_index == kPauseMenu_EquipmentScreen && (pressed & kButton_R)) ||
        (menu_index == kPauseMenu_MapScreen && (pressed & kButton_L))) {
      SmPauseOptions_SetActive(true);
      ConsumePad(kPanelOwned);
      menu_index = kPauseMenu_MapScreen;  /* the ROM runs its Map under the panel */
    }
    return;
  }

  ConsumePad(kPanelOwned);
  if (pressed & kButton_R) {
    LeaveTo(kPauseMenu_FadeOutToMap, kLabelMode_Map);
    return;
  }
  if (pressed & kButton_L) {
    LeaveTo(kPauseMenu_FadeOutToEquipment, kLabelMode_Equipment);
    return;
  }
  for (size_t i = 0; i < sizeof(kMenuButtons) / sizeof(kMenuButtons[0]); ++i)
    if (pressed & kMenuButtons[i].button)
      recomp_runtime_ui_handle_input(g_ui, kMenuButtons[i].input, 1, 0);
  if (g_leave || (pressed & kButton_B)) {
    g_leave = false;
    LeaveTo(kPauseMenu_FadeOutToMap, kLabelMode_Map);
    return;
  }
  menu_index = kPauseMenu_MapScreen;
}

/* ── rendering ────────────────────────────────────────────────────────── */

/* Panel colours (ARGB8888). */
#define COL_BG     0xFF0A0E18u
#define COL_BORDER 0xFF3A4A6Au
#define COL_TITLE  0xFFFFFFFFu
#define COL_TEXT   0xFFC2CCE0u
#define COL_SELBG  0xFF2B4A8Cu
#define COL_SEL    0xFFFFFFFFu
#define COL_DIM    0xFF8A98B4u
#define COL_ACCENT 0xFF6BE06Bu
#define COL_TABON  0xFFFFD24Du

static int TextLen(const char *s) { return (int)strlen(s); }

static void DrawCentered(uint32_t *dst, int stride, int h_max, int width,
                         int y, const char *s, uint32_t col, int scale) {
  int x = (width - TextLen(s) * 8 * scale) / 2;
  if (x < 0) x = 0;
  snes_ovl_draw_text(dst, stride, h_max, x, y, s, col, scale);
}

bool SmPauseOptions_Visible(void) {
  const unsigned state = GameState();
  return SmPauseOptions_IsActive() &&
         (state == kGameState_Paused || state == kGameState_UnpausingFadeOut);
}

/* While leaving, the panel fades with the screen under it: the PPU's master
 * brightness (INIDISP 0-15, forced blank = black) applied to the panel. */
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
  if (Leaving())
    DimToScreenBrightness(dst, stride, width, height);
}

static void DrawPanel(uint32_t *dst, int stride, int width, int height) {

  /* Opaque backdrop: the guest draws nothing meaningful on this tab (it is
   * parked on its Map screen), so cover the whole game rect. */
  snes_ovl_fill_rect(dst, stride, height, 0, 0, width, height, COL_BG);
  snes_ovl_stroke_rect(dst, stride, height, 5, 5, width - 10, height - 10, COL_BORDER);

  /* Tab strip: MAP / EQUIP / OPTIONS, with the current tab lit. */
  {
    int y = 10;
    snes_ovl_draw_text(dst, stride, height, 18, y, "MAP", COL_DIM, 1);
    snes_ovl_draw_text(dst, stride, height, 18 + 4 * 8, y, "EQUIP", COL_DIM, 1);
    int ox = 18 + (4 + 6) * 8;
    snes_ovl_fill_rect(dst, stride, height, ox - 3, y - 3, 8 * 8 + 6, 14, COL_SELBG);
    snes_ovl_draw_text(dst, stride, height, ox, y, "OPTIONS", COL_TABON, 1);
  }

  snes_ovl_fill_rect(dst, stride, height, 10, 24, width - 20, 1, COL_BORDER);
  DrawCentered(dst, stride, height, width, 30, "GAME OPTIONS", COL_TITLE, 1);
  snes_ovl_fill_rect(dst, stride, height, 10, 42, width - 20, 1, COL_BORDER);

  /* Rows, as the model reports them. The 8x8 font is upper-case only, so
   * "On" and "Fit" draw as ON and FIT. Eleven rows at 13px pitch from y=50
   * reach 180, leaving room for the hint before the frame's bottom edge. */
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
        snes_ovl_fill_rect(dst, stride, height, 14 + indent, row_y - 2,
                           width - 28 - indent, 13, COL_SELBG);
      snes_ovl_draw_text(dst, stride, height, 22 + indent, row_y,
                         row.label, label_col, 1);
      if (row.item->type != RECOMP_RUNTIME_UI_ACTION && row.value[0])
        snes_ovl_draw_text(dst, stride, height,
                           width - 22 - indent - TextLen(row.value) * 8, row_y,
                           row.value, value_col, 1);
    }
  }

  /* Hint row. */
  {
    int y = height - 20;
    snes_ovl_fill_rect(dst, stride, height, 10, y - 6, width - 20, 1, COL_BORDER);
    snes_ovl_draw_text(dst, stride, height, 14, y, "A SELECT", COL_ACCENT, 1);
    snes_ovl_draw_text(dst, stride, height, 14 + 10 * 8, y, "B BACK", COL_ACCENT, 1);
    snes_ovl_draw_text(dst, stride, height, 14 + 17 * 8, y, "L/R", COL_ACCENT, 1);
    snes_ovl_draw_text(dst, stride, height, 14 + 21 * 8, y, "TABS", COL_DIM, 1);
  }
}
