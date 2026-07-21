#include "cpu_state.h"
#include "widescreen.h"

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

uint8_t g_ram[0x20000];
bool g_ws_active;
int g_ws_extra;

void WsShadowPrefillTile(int layer, uint32_t worldTileX, uint32_t worldTileY,
                         uint16_t entry) {
  (void)layer;
  (void)worldTileX;
  (void)worldTileY;
  (void)entry;
}

void SmWidescreenGetSideSpace(int camera_x, int camera_y, int max_extra,
                              int *left_pixels, int *right_pixels);
int SmWidescreenEnemyCenterInView(CpuState *cpu);
int SmWidescreenEnemyBoxInView(CpuState *cpu);
int SmWidescreenEprojCenterInView(CpuState *cpu);
int SmWidescreenProjectileCenterInView(CpuState *cpu);
int SmWidescreenProjectileSbaInView(CpuState *cpu);
int SmWidescreenProjectileFarInView(CpuState *cpu);
int SmWidescreenAtmosphericXInView(CpuState *cpu);
int SmWidescreenScreenXInView(CpuState *cpu);

static void write16(uint16_t address, uint16_t value) {
  g_ram[address] = (uint8_t)(value & 0xff);
  g_ram[(uint16_t)(address + 1)] = (uint8_t)(value >> 8);
}

static void reset_state(void) {
  memset(g_ram, 0, sizeof(g_ram));
  g_ws_active = true;
  g_ws_extra = 43;
}

static void set_room_scrolls(uint16_t width, uint16_t height,
                             const uint8_t *scrolls) {
  write16(0x07A9, width);
  write16(0x07AB, height);
  memcpy(g_ram + 0xCD20, scrolls, (size_t)width * height);
}

static void test_side_space_uses_contiguous_scroll_run(void) {
  int left = -1, right = -1;
  const uint8_t scrolls[] = {
      2, 2, 2, 2, 0, 0, 0, 0,
      2, 2, 2, 2, 0, 0, 0, 0,
      2, 2, 2, 2, 2, 2, 2, 2,
      0, 0, 0, 0, 2, 2, 2, 2,
  };

  reset_state();
  set_room_scrolls(8, 4, scrolls);

  SmWidescreenGetSideSpace(130, 211, 43, &left, &right);

  assert(left == 43);
  assert(right == 43);
}

static void test_side_space_clamps_room_boundary(void) {
  int left = -1, right = -1;
  const uint8_t scrolls[] = {1, 0};

  reset_state();
  set_room_scrolls(2, 1, scrolls);

  SmWidescreenGetSideSpace(0, 0, 43, &left, &right);

  assert(left == 0);
  assert(right == 0);
}

static void test_side_space_falls_back_to_native_visible_screen(void) {
  int left = -1, right = -1;
  const uint8_t scrolls[] = {1, 0, 1};

  reset_state();
  set_room_scrolls(3, 1, scrolls);

  SmWidescreenGetSideSpace(128, 0, 43, &left, &right);

  assert(left == 43);
  assert(right == 0);
}

static void test_side_space_rejects_missing_scroll_run(void) {
  int left = -1, right = -1;
  const uint8_t scrolls[] = {0, 0};

  reset_state();
  set_room_scrolls(2, 1, scrolls);

  SmWidescreenGetSideSpace(0, 0, 43, &left, &right);

  assert(left == 0);
  assert(right == 0);
}

static void test_enemy_culling_extends_to_widescreen_margin(void) {
  CpuState cpu = {0};

  reset_state();
  write16(0x0911, 100);
  write16(0x0E54, 0);

  write16(0x0F7A, 60);
  assert(SmWidescreenEnemyCenterInView(&cpu));

  write16(0x0F7A, 56);
  assert(!SmWidescreenEnemyCenterInView(&cpu));

  write16(0x0F7A, 399);
  assert(SmWidescreenEnemyCenterInView(&cpu));

  write16(0x0F7A, 400);
  assert(!SmWidescreenEnemyCenterInView(&cpu));

  write16(0x0F7A, 50);
  write16(0x0F82, 8);
  assert(SmWidescreenEnemyBoxInView(&cpu));

  write16(0x0F7A, 407);
  assert(SmWidescreenEnemyBoxInView(&cpu));

  write16(0x0F7A, 408);
  assert(!SmWidescreenEnemyBoxInView(&cpu));
}

static void test_effect_and_projectile_culling_use_widescreen_margin(void) {
  CpuState cpu = {0};

  reset_state();
  cpu.X = 4;
  write16(0x0911, 100);

  write16(0x1A4B + cpu.X, 60);
  assert(SmWidescreenEprojCenterInView(&cpu));
  write16(0x1A4B + cpu.X, 56);
  assert(!SmWidescreenEprojCenterInView(&cpu));
  write16(0x1A4B + cpu.X, 399);
  assert(SmWidescreenEprojCenterInView(&cpu));
  write16(0x1A4B + cpu.X, 400);
  assert(!SmWidescreenEprojCenterInView(&cpu));

  write16(0x0B64 + cpu.X, 60);
  assert(SmWidescreenProjectileCenterInView(&cpu));
  write16(0x0B64 + cpu.X, 56);
  assert(!SmWidescreenProjectileCenterInView(&cpu));
  write16(0x0B64 + cpu.X, 399);
  assert(SmWidescreenProjectileCenterInView(&cpu));
  write16(0x0B64 + cpu.X, 400);
  assert(!SmWidescreenProjectileCenterInView(&cpu));

  write16(0x0B64 + cpu.X, 25);
  assert(SmWidescreenProjectileSbaInView(&cpu));
  write16(0x0B64 + cpu.X, 24);
  assert(!SmWidescreenProjectileSbaInView(&cpu));

  write16(0x0B64 + cpu.X, 93);
  write16(0x0911, 200);
  assert(SmWidescreenProjectileFarInView(&cpu));
  write16(0x0B64 + cpu.X, 90);
  assert(!SmWidescreenProjectileFarInView(&cpu));

  write16(0x0911, 100);
  write16(0x0B64 + cpu.X, 462);
  assert(SmWidescreenProjectileFarInView(&cpu));
  write16(0x0B64 + cpu.X, 464);
  assert(!SmWidescreenProjectileFarInView(&cpu));
}

static void test_culling_helpers_disable_cleanly(void) {
  CpuState cpu = {0};

  reset_state();
  write16(0x0911, 100);
  write16(0x0F7A, 60);
  write16(0x1A4B, 60);
  write16(0x0B64, 60);
  write16(0x0ADC, 61);

  g_ws_active = false;
  assert(!SmWidescreenEnemyCenterInView(&cpu));
  assert(!SmWidescreenEprojCenterInView(&cpu));
  assert(!SmWidescreenProjectileCenterInView(&cpu));
  assert(!SmWidescreenProjectileFarInView(&cpu));
  assert(!SmWidescreenAtmosphericXInView(&cpu));
  assert(!SmWidescreenScreenXInView(&cpu));

  g_ws_active = true;
  g_ws_extra = 0;
  assert(!SmWidescreenEnemyCenterInView(&cpu));
  assert(!SmWidescreenEprojCenterInView(&cpu));
  assert(!SmWidescreenProjectileCenterInView(&cpu));
  assert(!SmWidescreenProjectileFarInView(&cpu));
  assert(!SmWidescreenAtmosphericXInView(&cpu));
  assert(!SmWidescreenScreenXInView(&cpu));
}

static void test_atmospheric_effect_x_uses_widescreen_bounds(void) {
  CpuState cpu = {0};

  reset_state();
  cpu.Y = 6;
  write16(0x0911, 100);

  write16(0x0ADC + cpu.Y, 61);
  assert(SmWidescreenAtmosphericXInView(&cpu));
  write16(0x0ADC + cpu.Y, 60);
  assert(!SmWidescreenAtmosphericXInView(&cpu));

  write16(0x0ADC + cpu.Y, 402);
  assert(SmWidescreenAtmosphericXInView(&cpu));
  write16(0x0ADC + cpu.Y, 403);
  assert(!SmWidescreenAtmosphericXInView(&cpu));
}

static void test_screen_x_uses_signed_widescreen_bounds(void) {
  CpuState cpu = {0};

  reset_state();

  cpu.A = (uint16_t)-43;
  assert(SmWidescreenScreenXInView(&cpu));
  cpu.A = (uint16_t)-44;
  assert(!SmWidescreenScreenXInView(&cpu));
  cpu.A = 298;
  assert(SmWidescreenScreenXInView(&cpu));
  cpu.A = 299;
  assert(!SmWidescreenScreenXInView(&cpu));
}

int main(void) {
  test_side_space_uses_contiguous_scroll_run();
  test_side_space_clamps_room_boundary();
  test_side_space_falls_back_to_native_visible_screen();
  test_side_space_rejects_missing_scroll_run();
  test_enemy_culling_extends_to_widescreen_margin();
  test_effect_and_projectile_culling_use_widescreen_margin();
  test_culling_helpers_disable_cleanly();
  test_atmospheric_effect_x_uses_widescreen_bounds();
  test_screen_x_uses_signed_widescreen_bounds();
  return 0;
}
