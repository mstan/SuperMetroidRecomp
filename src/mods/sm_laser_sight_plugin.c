/* super-metroid.map-hud / laser-sight: a red sight line along Samus' shot
 * direction (Metroid Prime style), drawn while she aims diagonally or, as an
 * option, whenever she can shoot. Presentation only.
 *
 * The direction and muzzle position are the game's own: the pose's
 * direction_shots_fired byte (pose table $91:B629, 8 bytes per pose; values
 * >= $10 mean "cannot shoot") and the projectile origin offsets that
 * InitProjectilePositionDirection ($90:BA56) adds to Samus' position, minus
 * the pose's y_offset_to_gfx. Running poses and the aim-up moonwalks use the
 * second offset table, as the game does. The line stops at the first block
 * the game treats as solid to a horizontal collision check (block types in
 * kBlockColl_Horiz_CheckColl, $94:9515: slopes, solid/shot/grapple, door,
 * spike, special, bomb), reading the room's level data ($7F:0002) and
 * following extension blocks through their BTS ($7F:6402). With custom
 * presentation, use the displayed immutable pose and its physical interpolation
 * instead of the next live simulation pose. A centered three-pixel footprint
 * stays on that native shot axis; each edge pixel also respects walls/HUD.
 */

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "mod_runtime.h"
#include "sm_mod_overlay.h"
#include "sm_mod_support.h"
#include "sm_display.h"
#include "sm_renderer.h"

#define PACKAGE "super-metroid.map-hud"

extern const uint8_t *g_rom;

enum {
  kGameState = 0x0998, kLayer1X = 0x0911, kLayer1Y = 0x0915,
  kPose = 0x0A1C, kMovementType = 0x0A1F, kSamusX = 0x0AF6, kSamusY = 0x0AFA,
  kPoseParams = 0x91B629, kRoomWidth = 0x07A5, kRoomHeight = 0x07A7,
  kLevelData = 0x10002, kBts = 0x16402,
};

static const int8_t kOrigin3X[10] = {2, 13, 11, 13, 2, -5, -14, -11, -19, -2};
static const int8_t kOrigin3Y[10] = {-8, -13, 1, 4, 13, 13, 4, 1, -19, -8};
static const int8_t kOrigin4X[10] = {2, 15, 15, 13, 2, -5, -13, -13, -15, -2};
static const int8_t kOrigin4Y[10] = {-8, -16, -2, 1, 13, 13, 1, -2, -16, -8};
static const int8_t kStepX[10] = {0, 1, 1, 1, 0, 0, -1, -1, -1, 0};
static const int8_t kStepY[10] = {-1, -1, 0, 1, 1, 1, 1, 0, -1, -1};

static bool g_always;

static uint16_t Word(const uint8_t *wram, uint16_t addr) {
  return (uint16_t)(wram[addr] | wram[addr + 1] << 8);
}
static const uint8_t *PoseParams(uint16_t pose) {
  uint32_t offset = ((kPoseParams >> 16) & 0x7F) << 15 | (kPoseParams & 0x7FFF);
  return g_rom + offset + (uint32_t)pose * 8;
}

/* Whether the block at room pixel (x, y) stops a shot line. */
static bool Solid(const uint8_t *wram, int x, int y) {
  int width = Word(wram, kRoomWidth), height = Word(wram, kRoomHeight);
  if (x < 0 || y < 0 || x >= width * 16 || y >= height * 16 || !width) return true;
  int index = (y >> 4) * width + (x >> 4);
  for (int hops = 0; hops < 4; ++hops) {
    if (index < 0 || index >= width * height) return true;
    int type = wram[kLevelData + index * 2 + 1] >> 4;
    int8_t bts = (int8_t)wram[kBts + index];
    if (type == 0x5) { index += bts; continue; }             /* horizontal extension */
    if (type == 0xD) { index += bts * width; continue; }     /* vertical extension */
    return type == 0x1 || type >= 0x8;
  }
  return true;
}

static void SmLaserSightDraw(uint32_t *pixels, int stride, int width, int height,
                             int stage_x, const uint8_t *wram) {
  if (!g_rom || Word(wram, kGameState) != 8) return;
  SmPlayerView player;
  bool presented = SmDisplay_GetPlayer(&player);
  if (presented) wram = player.ram;
  uint16_t pose = presented ? (uint16_t)player.pose : Word(wram, kPose);
  if (pose > 0xFC) return;
  const uint8_t *params = PoseParams(pose);
  int dir = params[3];
  if (dir >= 10) return;
  bool diagonal = dir == 1 || dir == 3 || dir == 6 || dir == 8;
  if (!diagonal && !g_always) return;
  bool alt = pose == 0x75 || pose == 0x76 || (presented ? player.movement : wram[kMovementType]) == 1;
  int x = (presented ? player.x : (int16_t)(Word(wram, kSamusX) - Word(wram, kLayer1X))) +
          (alt ? kOrigin4X : kOrigin3X)[dir] + stage_x;
  int y = (presented ? player.y : (int16_t)(Word(wram, kSamusY) - Word(wram, kLayer1Y))) +
          (alt ? kOrigin4Y : kOrigin3Y)[dir] - params[4];
  /* Room position of screen pixel (stage_x, 0). */
  int room_x = (presented ? player.camera_x : Word(wram, kLayer1X)) - stage_x;
  int room_y = presented ? player.camera_y : Word(wram, kLayer1Y);
  /* Dotted: 3 lit, 2 dark, starting a little ahead of the muzzle. */
  for (int i = 6; i < 512; ++i) {
    int px = x + kStepX[dir] * i, py = y + kStepY[dir] * i;
    if (px < 0 || px >= width || py < 32 || py >= height) break;   /* not over the HUD */
    if (Solid(wram, room_x + px, room_y + py)) break;
    if (i % 5 < 3) for (int oy = -1; oy <= 1; ++oy) for (int ox = -1; ox <= 1; ++ox) {
      if (ox * ox + oy * oy > 1) continue;
      int sx = px + ox, sy = py + oy;
      if (sx >= 0 && sx < width && sy >= 32 && sy < height &&
          !Solid(wram, room_x + sx, room_y + sy)) pixels[sy * stride + sx] = 0xFFFF3030u;
    }
  }
}

static void SmLaserSightActivate(void) {
  SmModValue when;
  g_always = !strcmp(SmModOption(&when, PACKAGE, "laser-sight", "when", "diagonal"), "always");
  SmModOverlayRegister(SmLaserSightDraw);
  fprintf(stderr, "[mods] laser sight (%s)\n", g_always ? "always" : "diagonal aim");
}

SNES_MOD_CONSTRUCTOR(sm_register_laser_sight_plugin) {
  snes_mod_register_presentation_plugin("super-metroid.map-hud.laser-sight",
                                        SmLaserSightActivate);
}
