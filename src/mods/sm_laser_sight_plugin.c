/* super-metroid.map-hud / laser-sight: a red sight line along Samus' shot
 * direction (Metroid Prime style), drawn while she aims diagonally or, as an
 * option, whenever she can shoot. Presentation only.
 *
 * The direction and muzzle position are the game's own: the pose's
 * direction_shots_fired byte (pose table $91:B629, 8 bytes per pose; values
 * >= $10 mean "cannot shoot") and the projectile origin offsets that
 * InitProjectilePositionDirection ($90:BA56) adds to Samus' position, minus
 * the pose's y_offset_to_gfx. Running poses and the aim-up moonwalks use the
 * second offset table, as the game does. The line runs to the frame edge
 * and does not stop at walls.
 */

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "mod_runtime.h"
#include "sm_mod_overlay.h"
#include "sm_mod_support.h"

#define PACKAGE "super-metroid.map-hud"

extern const uint8_t *g_rom;

enum {
  kGameState = 0x0998, kLayer1X = 0x0911, kLayer1Y = 0x0915,
  kPose = 0x0A1C, kMovementType = 0x0A1F, kSamusX = 0x0AF6, kSamusY = 0x0AFA,
  kPoseParams = 0x91B629,
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

static void SmLaserSightDraw(uint32_t *pixels, int stride, int width, int height,
                             int stage_x, const uint8_t *wram) {
  if (!g_rom || Word(wram, kGameState) != 8) return;
  uint16_t pose = Word(wram, kPose);
  if (pose > 0xFC) return;
  const uint8_t *params = PoseParams(pose);
  int dir = params[3];
  if (dir >= 10) return;
  bool diagonal = dir == 1 || dir == 3 || dir == 6 || dir == 8;
  if (!diagonal && !g_always) return;
  bool alt = pose == 0x75 || pose == 0x76 || wram[kMovementType] == 1;
  int x = (int16_t)(Word(wram, kSamusX) - Word(wram, kLayer1X)) +
          (alt ? kOrigin4X : kOrigin3X)[dir] + stage_x;
  int y = (int16_t)(Word(wram, kSamusY) - Word(wram, kLayer1Y)) +
          (alt ? kOrigin4Y : kOrigin3Y)[dir] - params[4];
  /* Dotted: 3 lit, 2 dark, starting a little ahead of the muzzle. */
  for (int i = 6; i < 512; ++i) {
    int px = x + kStepX[dir] * i, py = y + kStepY[dir] * i;
    if (px < 0 || px >= width || py < 32 || py >= height) break;   /* not over the HUD */
    if (i % 5 < 3) pixels[py * stride + px] = 0xFFFF3030u;
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
