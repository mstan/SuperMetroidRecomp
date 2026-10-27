/* super-metroid.controls: controller remaps and macros, as mod input
 * transforms (snesrecomp mod_runtime.h). They rewrite the local button word
 * before netplay exchange, replay recording and the guest see it, so the
 * guest only ever receives ordinary button presses and stays stock.
 *
 * zero-mission: Zero Mission / Fusion style shoulders.
 *   - The aim shoulder aims diagonally up while held, and diagonally down
 *     while Down is held too (Down itself is withheld so Samus does not
 *     crouch). It presses whatever the player configured as angle up/down
 *     in the game's controller setup ($09BE/$09BC).
 *   - The missile shoulder arms missiles: "hold" selects them while held and
 *     returns to the beam on release; "toggle" switches on each press. It
 *     presses the configured item select ($09BA) or item cancel ($09B8) for
 *     one frame, so the game's own HUD handlers do the switching.
 *
 * radial-menu: the right stick selects a HUD item by direction (up missiles,
 * right super missiles, down power bombs, left grapple, up-left X-ray) by
 * pulsing item select until the HUD shows it. Items Samus lacks are skipped.
 * While the stick is pushed a ring of the five items is drawn over the room
 * (presentation only), the pointed-at one highlighted, missing ones dimmed.
 *
 * Both act only during gameplay (game state 8) and pass every other screen
 * through untouched.
 */

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "mod_runtime.h"
#include "sm_mod_overlay.h"
#include "sm_mod_support.h"
#include "snes_overlay_draw.h"

#define PACKAGE "super-metroid.controls"

enum {
  kGameState = 0x0998, kEquipped = 0x09A2,
  kCfgItemCancel = 0x09B8, kCfgItemSwitch = 0x09BA,
  kCfgAimDown = 0x09BC, kCfgAimUp = 0x09BE,
  kMaxMissiles = 0x09C8, kMaxSupers = 0x09CC, kMaxPowerBombs = 0x09D0,
  kHudItem = 0x09D2,
  kStickDeadzone = 16000,
};

static uint16_t Word(const uint8_t *wram, uint16_t addr) {
  return (uint16_t)(wram[addr] | wram[addr + 1] << 8);
}

/* The game stores buttons in $4218 order (B Y Sel Start U D L R A X L R in
 * bits 15..4); the host word has the same buttons in bits 0..11. */
static uint16_t HostButtons(uint16_t guest) {
  uint16_t host = 0;
  for (int i = 0; i < 12; ++i)
    if (guest & (1u << (15 - i))) host |= (uint16_t)(1u << i);
  return host;
}

static bool InGameplay(const uint8_t *wram) { return Word(wram, kGameState) == 8; }

/* One seat's macro state. A pulse is held for one frame and then released
 * for one, so every pulse is a new press to the game. */
typedef struct Seat {
  uint16_t pulse;
  int pulse_phase;      /* 0 idle, 2 = press this frame, 1 = release this frame */
  bool missile_held;
} Seat;
/* Each feature pulses independently: both may be enabled. */
static Seat g_zm_seats[2], g_radial_seats[2];

static void Pulse(Seat *seat, uint16_t buttons) {
  if (seat->pulse_phase == 0 && buttons) {
    seat->pulse = buttons;
    seat->pulse_phase = 2;
  }
}
static uint16_t PulseOutput(Seat *seat) {
  if (seat->pulse_phase == 0) return 0;
  return seat->pulse_phase-- == 2 ? seat->pulse : 0;
}

/* ── zero-mission ────────────────────────────────────────────────────── */

static uint16_t g_aim_shoulder = SNES_PAD_R, g_missile_shoulder = SNES_PAD_L;
static bool g_missile_toggle, g_missile_enabled = true;

static uint16_t SmZeroMissionControls(uint16_t buttons, const SnesModInputContext *ctx) {
  if (!ctx->wram || !InGameplay(ctx->wram)) return buttons;
  const uint8_t *wram = ctx->wram;
  Seat *seat = &g_zm_seats[ctx->player & 1];
  bool aim = (buttons & g_aim_shoulder) != 0;
  bool missile = (buttons & g_missile_shoulder) != 0;
  uint16_t out = (uint16_t)(buttons & ~(g_aim_shoulder | g_missile_shoulder));
  if (aim) {
    if (buttons & SNES_PAD_DOWN) {
      out = (uint16_t)(out & ~SNES_PAD_DOWN);
      out |= HostButtons(Word(wram, kCfgAimDown));
    } else {
      out |= HostButtons(Word(wram, kCfgAimUp));
    }
  }
  if (g_missile_enabled) {
    bool armed = Word(wram, kHudItem) != 0;
    bool pressed = missile && !seat->missile_held;
    bool released = !missile && seat->missile_held;
    if (g_missile_toggle) {
      if (pressed)
        Pulse(seat, HostButtons(Word(wram, armed ? kCfgItemCancel : kCfgItemSwitch)));
    } else {
      if (pressed && !armed) Pulse(seat, HostButtons(Word(wram, kCfgItemSwitch)));
      if (released && armed) Pulse(seat, HostButtons(Word(wram, kCfgItemCancel)));
    }
    seat->missile_held = missile;
  } else if (missile) {
    out |= g_missile_shoulder;   /* unchanged when the missile button is off */
  }
  return (uint16_t)(out | PulseOutput(seat));
}

static void SmZeroMissionActivate(void) {
  SmModValue aim_value, missile_value;
  const char *aim = SmModOption(&aim_value, PACKAGE, "zero-mission", "aim-shoulder", "R");
  const char *missile = SmModOption(&missile_value, PACKAGE, "zero-mission", "missile", "hold");
  g_aim_shoulder = !strcmp(aim, "L") ? SNES_PAD_L : SNES_PAD_R;
  g_missile_shoulder = g_aim_shoulder == SNES_PAD_R ? SNES_PAD_L : SNES_PAD_R;
  g_missile_enabled = strcmp(missile, "off") != 0;
  g_missile_toggle = !strcmp(missile, "toggle");
  memset(g_zm_seats, 0, sizeof(g_zm_seats));
  snes_mod_register_input_transform(SmZeroMissionControls);
  fprintf(stderr, "[mods] zero mission controls: aim=%s missile=%s\n", aim, missile);
}

/* ── radial-menu ─────────────────────────────────────────────────────── */

static int RadialTarget(const SnesModInputContext *ctx) {
  int x = ctx->right_x, y = ctx->right_y;   /* SDL: +y is down */
  if (x * x + y * y < kStickDeadzone * kStickDeadzone) return -1;
  bool up = y < -kStickDeadzone / 2, down = y > kStickDeadzone / 2;
  bool left = x < -kStickDeadzone / 2, right = x > kStickDeadzone / 2;
  if (up && left) return 5;                 /* X-ray */
  if (up && !left && !right) return 1;      /* missiles */
  if (right && !up && !down) return 2;      /* super missiles */
  if (down && !left && !right) return 3;    /* power bombs */
  if (left && !up && !down) return 4;       /* grapple */
  return -1;
}

static bool Owned(const uint8_t *wram, int item) {
  switch (item) {
    case 1: return Word(wram, kMaxMissiles) != 0;
    case 2: return Word(wram, kMaxSupers) != 0;
    case 3: return Word(wram, kMaxPowerBombs) != 0;
    case 4: return (Word(wram, kEquipped) & 0x4000) != 0;
    case 5: return (Word(wram, kEquipped) & 0x8000) != 0;
    default: return false;
  }
}

/* Seat 0's pointed-at item for the ring; -1 when the stick is centred. */
static int g_ring_target = -1;

static uint16_t SmRadialMenu(uint16_t buttons, const SnesModInputContext *ctx) {
  if (!ctx->wram || !InGameplay(ctx->wram)) {
    if (ctx->player == 0) g_ring_target = -1;
    return buttons;
  }
  Seat *seat = &g_radial_seats[ctx->player & 1];
  int target = RadialTarget(ctx);
  if (ctx->player == 0) g_ring_target = target;
  if (target > 0 && Owned(ctx->wram, target) && Word(ctx->wram, kHudItem) != target)
    Pulse(seat, HostButtons(Word(ctx->wram, kCfgItemSwitch)));
  return (uint16_t)(buttons | PulseOutput(seat));
}

static void SmRadialRing(uint32_t *pixels, int stride, int width, int height,
                         int stage_x, const uint8_t *wram) {
  (void)width;
  if (g_ring_target < 0 || !InGameplay(wram)) return;
  static const struct { int item, dx, dy; const char *label; } kSlots[] = {
    {1, 0, -40, "MSL"}, {2, 44, 0, "SUP"}, {3, 0, 40, "PB"},
    {4, -44, 0, "GRP"}, {5, -32, -30, "XRY"},
  };
  int cx = stage_x + 128, cy = 112;
  for (size_t i = 0; i < sizeof(kSlots) / sizeof(kSlots[0]); ++i) {
    int item = kSlots[i].item;
    bool owned = Owned(wram, item), selected = item == g_ring_target;
    int x = cx + kSlots[i].dx - 14, y = cy + kSlots[i].dy - 6;
    uint32_t fill = selected ? 0xFF2C6E2Cu : 0xFF101820u;
    uint32_t ink = !owned ? 0xFF5A5A5Au : Word(wram, kHudItem) == item ? 0xFFFFE070u : 0xFFF0F0F0u;
    snes_ovl_fill_rect(pixels, stride, height, x, y, 28, 12, fill);
    snes_ovl_stroke_rect(pixels, stride, height, x, y, 28, 12, selected ? 0xFF8CF08Cu : 0xFF405060u);
    snes_ovl_draw_text(pixels, stride, height, x + 2, y + 2, kSlots[i].label, ink, 1);
  }
}

static void SmRadialMenuActivate(void) {
  memset(g_radial_seats, 0, sizeof(g_radial_seats));
  g_ring_target = -1;
  snes_mod_register_input_transform(SmRadialMenu);
  SmModOverlayRegister(SmRadialRing);
  fprintf(stderr, "[mods] radial weapon menu on the right stick\n");
}

SNES_MOD_CONSTRUCTOR(sm_register_controls_plugins) {
  snes_mod_register_activation_plugin("super-metroid.controls.zero-mission",
                                      SmZeroMissionActivate);
  snes_mod_register_activation_plugin("super-metroid.controls.radial-menu",
                                      SmRadialMenuActivate);
}
