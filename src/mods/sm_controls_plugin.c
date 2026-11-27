/* super-metroid.controls: controller remaps and macros, as mod input
 * transforms (snesrecomp mod_runtime.h). They rewrite the local button word
 * before netplay exchange, replay recording and the guest see it, so the
 * guest only ever receives ordinary button presses and stays stock.
 *
 * zero-mission: Zero Mission / Fusion style shoulders.
 *   - The aim shoulder retains the last diagonal angle. Up/Down select its
 *     angle while aiming, without moving/crouching; releasing those inputs
 *     or the shoulder keeps that choice. Activation starts with up. It
 *     presses whatever the player configured as angle up/down
 *     in the game's controller setup ($09BE/$09BC).
 *   - The missile shoulder arms missiles: "hold" selects them while held and
 *     returns to the beam on release; "toggle" switches on each press. It
 *     presses the configured item select ($09BA) or item cancel ($09B8) for
 *     one frame, so the game's own HUD handlers do the switching.
 *
 * radial-menu: the right stick selects a HUD item by direction (up missiles,
 * right super missiles, down power bombs, left grapple, up-left X-ray,
 * up-right beam) by pulsing the native select/cancel bindings until the HUD
 * shows it. Items Samus lacks are skipped. While the stick is pushed a ring
 * of the six items is drawn over the room
 * (presentation only), the pointed-at one highlighted, missing ones dimmed.
 *
 * Gameplay also runs during the initial and unpause fade-ins (states 7 and
 * 18). Menus and room-loading states keep their native input.
 */

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "mod_runtime.h"
#include "sm_mod_overlay.h"
#include "sm_mod_support.h"
#include "sm_movement_plugins.h"
#include "sm_radial_layout.h"
#include "sm_dread_aim.h"
#include "snes_overlay_draw.h"

#define PACKAGE "super-metroid.controls"

enum {
  kGameState = 0x0998, kEquipped = 0x09A2, kMovementType = 0x0A1F,
  kCfgShoot = 0x09B2, kCfgRun = 0x09B6,
  kCfgItemCancel = 0x09B8, kCfgItemSwitch = 0x09BA,
  kCfgAimDown = 0x09BC, kCfgAimUp = 0x09BE,
  kMissiles = 0x09C6, kSupers = 0x09CA,
  kMaxMissiles = 0x09C8, kMaxSupers = 0x09CC, kMaxPowerBombs = 0x09D0, kPowerBombs = 0x09CE,
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

static bool InGameplay(const uint8_t *wram) {
  unsigned state = Word(wram, kGameState);
  return state == 7 || state == 8 || state == 18;
}

/* Invert the configured Run button before recording/exchange, so the guest
 * receives ordinary deterministic input. Menus and demos retain native input. */
static uint16_t SmRunByDefault(uint16_t buttons, const SnesModInputContext *ctx) {
  if (!ctx->wram || !InGameplay(ctx->wram)) return buttons;
  return (uint16_t)(buttons ^ HostButtons(Word(ctx->wram, kCfgRun)));
}

static void SmRunByDefaultActivate(void) {
  snes_mod_register_input_transform(SmRunByDefault);
  fprintf(stderr, "[mods] run by default: hold the configured Run button to walk\n");
}

/* One seat's macro state. A pulse is held for one frame and then released
 * for one, so every pulse is a new press to the game. */
typedef struct Seat {
  uint16_t pulse;
  int pulse_phase;      /* 0 idle, 2 = press this frame, 1 = release this frame */
  bool missile_held;
  bool aim_down;        /* local input preference, independent for each seat */
  bool power_bomb_selecting;
  bool power_bomb_shot; /* retain a quick fire press until native selection is ready */
  bool power_bomb_release;
  int radial_target;    /* finish a quick flick using ordinary HUD pulses */
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
static bool g_missile_toggle, g_missile_enabled = true, g_dread_mode;

static bool MorphBall(const uint8_t *wram) {
  unsigned movement=wram[kMovementType];
  return movement==4 || movement==8 || movement==0x11 || movement==0x12 || movement==0x13;
}

static uint16_t SmZeroMissionControls(uint16_t buttons, const SnesModInputContext *ctx) {
  if (!ctx->wram || !InGameplay(ctx->wram)) {
    Seat *seat = &g_zm_seats[ctx->player & 1];
    seat->power_bomb_selecting = seat->power_bomb_shot = seat->power_bomb_release = false;
    seat->pulse_phase = 0;
    seat->missile_held = (buttons & g_missile_shoulder) != 0;
    return buttons;
  }
  if (!(g_dread_mode && (buttons&g_aim_shoulder)) && SmQuickMorphAirInput(&buttons,ctx)) return buttons;
  const uint8_t *wram = ctx->wram;
  Seat *seat = &g_zm_seats[ctx->player & 1];
  bool aim = (buttons & g_aim_shoulder) != 0;
  bool missile = (buttons & g_missile_shoulder) != 0;
  uint16_t out = (uint16_t)(buttons & ~(g_aim_shoulder | g_missile_shoulder));
  if (aim && g_dread_mode) out|=g_aim_shoulder;
  else if (aim) {
    if (buttons & SNES_PAD_DOWN) seat->aim_down = true;
    else if (buttons & SNES_PAD_UP) seat->aim_down = false;
    out = (uint16_t)(out & ~(SNES_PAD_UP | SNES_PAD_DOWN));
    out |= HostButtons(Word(wram, seat->aim_down ? kCfgAimDown : kCfgAimUp));
  }
  if (g_missile_enabled) {
    unsigned item=Word(wram,kHudItem);
    bool armed = item != 0;
    bool morph=MorphBall(wram);
    bool power_bombs=morph && Word(wram,kMaxPowerBombs) && Word(wram,kPowerBombs);
    bool pressed = missile && !seat->missile_held;
    bool released = !missile && seat->missile_held;
    bool fired_pending = false;
    uint16_t shoot = HostButtons(Word(wram, kCfgShoot));
    if (!morph) seat->power_bomb_shot = seat->power_bomb_release = false;
    if (morph && (missile || seat->power_bomb_selecting || seat->power_bomb_shot)) {
      /* The native HUD cycles across several frames. Never let that interval
       * turn a PB request into a regular bomb, and retain a one-frame tap.
       * Expanded input is recorded/exchanged normally; inventory and actual
       * firing remain entirely owned by the game's handlers. */
      if (!power_bombs) {
        out &= (uint16_t)~shoot;
        seat->power_bomb_shot = false;
      } else if (item != 3) {
        if (buttons & shoot) seat->power_bomb_shot = true;
        out &= (uint16_t)~shoot;
      } else if (seat->power_bomb_shot) {
        out |= shoot;
        seat->power_bomb_shot = false;
        fired_pending = true;
        seat->power_bomb_release = !missile && !g_missile_toggle;
      }
    }
    if (g_missile_toggle) {
      if (pressed) {
        seat->power_bomb_selecting=power_bombs && item!=3;
        if (!seat->power_bomb_selecting && (!morph || item==3))
          Pulse(seat,HostButtons(Word(wram,armed ? kCfgItemCancel : kCfgItemSwitch)));
      }
      if (!power_bombs || item==3) seat->power_bomb_selecting=false;
      if (seat->power_bomb_selecting || seat->power_bomb_shot)
        Pulse(seat,HostButtons(Word(wram,kCfgItemSwitch)));
    } else {
      /* Native item-switch pulses select the real PB slot; they never create
       * a bomb or alter inventory. Keep cycling if another weapon was armed. */
      if (((missile || seat->power_bomb_shot) && power_bombs && item!=3) || (missile && !armed && !morph))
        Pulse(seat, HostButtons(Word(wram, kCfgItemSwitch)));
      if ((released || seat->power_bomb_release) && armed &&
          !seat->power_bomb_shot && !fired_pending) {
        Pulse(seat, HostButtons(Word(wram, kCfgItemCancel)));
        seat->power_bomb_release = false;
      }
    }
    seat->missile_held = missile;
  } else if (missile) {
    out |= g_missile_shoulder;   /* unchanged when the missile button is off */
  }
  return (uint16_t)(out | PulseOutput(seat));
}

static void SmZeroMissionActivate(void) {
  SmModValue aim_value, missile_value;
  g_dread_mode=snes_mod_runtime_feature_enabled_c(PACKAGE,"dread-shoulders");
  const char *feature=g_dread_mode ? "dread-shoulders" : "zero-mission";
  const char *aim = SmModOption(&aim_value, PACKAGE, feature, "aim-shoulder", "R");
  const char *missile = SmModOption(&missile_value, PACKAGE, feature, "missile", "hold");
  g_aim_shoulder = !strcmp(aim, "L") ? SNES_PAD_L : SNES_PAD_R;
  g_missile_shoulder = g_aim_shoulder == SNES_PAD_R ? SNES_PAD_L : SNES_PAD_R;
  g_missile_enabled = strcmp(missile, "off") != 0;
  g_missile_toggle = !strcmp(missile, "toggle");
  SmDreadAimConfigure(g_dread_mode,g_aim_shoulder==SNES_PAD_R ? 0x10 : 0x20);
  memset(g_zm_seats, 0, sizeof(g_zm_seats));
  snes_mod_register_input_transform(SmZeroMissionControls);
  fprintf(stderr, "[mods] zero mission controls: aim=%s missile=%s\n", aim, missile);
}

/* ── radial-menu ─────────────────────────────────────────────────────── */

static int g_radial_positions[6] = {1,0,2,4,6,7};
static int RadialTarget(const SnesModInputContext *ctx) {
  return SmRadialItemAt(g_radial_positions,ctx->right_x,ctx->right_y,kStickDeadzone);
}

static bool Owned(const uint8_t *wram, int item) {
  switch (item) {
    case 0: return true;
    case 1: return Word(wram, kMissiles) != 0;
    case 2: return Word(wram, kSupers) != 0;
    case 3: return Word(wram, kPowerBombs) != 0;
    case 4: return (Word(wram, kEquipped) & 0x4000) != 0;
    case 5: return (Word(wram, kEquipped) & 0x8000) != 0;
    default: return false;
  }
}

/* Seat 0's pointed-at item for the ring; -1 when the stick is centred. */
static int g_ring_target = -1;

static uint16_t SmRadialMenu(uint16_t buttons, const SnesModInputContext *ctx) {
  if (!ctx->wram || !InGameplay(ctx->wram)) {
    Seat *seat=&g_radial_seats[ctx->player & 1];
    seat->radial_target=-1;seat->pulse_phase=0;
    if (ctx->player == 0) g_ring_target = -1;
    return buttons;
  }
  Seat *seat = &g_radial_seats[ctx->player & 1];
  int target = RadialTarget(ctx);
  if (ctx->player == 0) g_ring_target = target;
  if (target >= 0) seat->radial_target=Owned(ctx->wram,target) ? target : -1;
  target=seat->radial_target;
  if (target>=0 && !Owned(ctx->wram,target)) {
    target=seat->radial_target=-1;seat->pulse_phase=0;
  }
  if (target >= 0) {
    if (Word(ctx->wram,kHudItem)==target) seat->radial_target=-1;
    else Pulse(seat,HostButtons(Word(ctx->wram,target ? kCfgItemSwitch : kCfgItemCancel)));
  }
  return (uint16_t)(buttons | PulseOutput(seat));
}

static void SmRadialRing(uint32_t *pixels, int stride, int width, int height,
                         int stage_x, const uint8_t *wram) {
  (void)width;
  if (g_ring_target < 0 || !InGameplay(wram)) return;
  static const char *const labels[] = {"BEAM","MSL","SUP","PB","GRP","XRY"};
  int cx = stage_x + 128, cy = 112;
  for (int item=0;item<6;++item) {
    bool owned = Owned(wram, item), selected = item == g_ring_target;
    int position=g_radial_positions[item];
    int x = cx + kSmRadialNodeX[position] - 14, y = cy + kSmRadialNodeY[position] - 6;
    uint32_t fill = selected ? 0xFF2C6E2Cu : 0xFF101820u;
    uint32_t ink = !owned ? 0xFF5A5A5Au : Word(wram, kHudItem) == item ? 0xFFFFE070u : 0xFFF0F0F0u;
    snes_ovl_fill_rect(pixels, stride, height, x, y, 28, 12, fill);
    snes_ovl_stroke_rect(pixels, stride, height, x, y, 28, 12, selected ? 0xFF8CF08Cu : 0xFF405060u);
    snes_ovl_draw_text(pixels, stride, height, x + 2, y + 2, labels[item], ink, 1);
  }
}

static void SmRadialMenuActivate(void) {
  memset(g_radial_seats, 0, sizeof(g_radial_seats));
  for (size_t i=0;i<2;++i) g_radial_seats[i].radial_target=-1;
  memcpy(g_radial_positions,kSmRadialDefaultPositions,sizeof(g_radial_positions));
  if (snes_mod_runtime_feature_enabled_c(PACKAGE,"radial-layout")) {
    for (int item=0;item<6;++item) {
      SmModValue value;
      const char *name=SmModOption(&value,PACKAGE,"radial-layout",kSmRadialPositionOptions[item],
          kSmRadialDirections[kSmRadialDefaultPositions[item]]);
      int position=SmRadialPositionIndex(name);
      if (position>=0) g_radial_positions[item]=position;
    }
  }
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
  snes_mod_register_activation_plugin("super-metroid.run-by-default",
                                      SmRunByDefaultActivate);
}
