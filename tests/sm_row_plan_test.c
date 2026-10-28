/* Compare complete planned rows with the independent scalar compositor and
 * window evaluator, including scene-aware margins and signed mosaic grids. */
#define SM_NATIVE_RASTER 1
#define SM_NATIVE_ROW_PLAN 1
#include "../src/sm_renderer.c"
#include <assert.h>
#include <stdio.h>

static uint32_t random_state = 0x6f5a849d;
static unsigned next_random(void) {
  random_state ^= random_state << 13;
  random_state ^= random_state >> 17;
  random_state ^= random_state << 5;
  return random_state;
}
static uint8_t test_ram[0x20000];
static Ppu test_ppu;
static uint16_t test_objects[SM_MAX_WIDTH], center_pixels[3][256];
static uint32_t expected[SM_MAX_WIDTH], actual[SM_MAX_WIDTH];
static void put_word(unsigned address, unsigned value) {
  test_ram[address] = (uint8_t)value;
  test_ram[address + 1] = (uint8_t)(value >> 8);
}

int main(void) {
  const int widths[] = {256, 342, 682, SM_MAX_WIDTH};
  for (unsigned iteration = 0; iteration < 512; ++iteration) {
    memset(&test_ppu, 0, sizeof(test_ppu));
    Ppu *p = &test_ppu;
    for (unsigned i = 0; i < sizeof(test_ram); ++i) test_ram[i] = (uint8_t)next_random();
    put_word(0x7a5, 64); put_word(0x7a7, 32); put_word(0x998, 8);
    put_word(0x911, (next_random() & 1023) - 96);
    put_word(0x915, (next_random() & 511) - 64);
    p->bgmode = 1 | (next_random() & 0x78);
    p->mosaic = (uint8_t)next_random();
    p->cgadsub = (uint8_t)next_random(); p->cgwsel = (uint8_t)next_random();
    p->fixedColor = next_random() & 0x7fff;
    p->inidisp = next_random() & 15;
    p->windowsel = next_random() & 0xffffff;
    p->wbgobjlog = next_random() & 0xfff;
    p->window1left = iteration & 1 ? 0 : next_random() & 255;
    p->window1right = iteration & 2 ? 255 : next_random() & 255;
    p->window2left = iteration & 4 ? 0 : next_random() & 255;
    p->window2right = iteration & 8 ? 255 : next_random() & 255;
    p->bgTileAdr = next_random() & 0xfff;
    for (unsigned i = 0; i < 3; ++i) {
      p->hScroll[i] = next_random() & 1023;
      p->vScroll[i] = next_random() & 1023;
      p->bgXsc[i] = (uint8_t)next_random();
    }
    if (!(iteration % 8)) p->bgXsc[2] = 0x5a;
    for (unsigned i = 0; i < 2; ++i) {
      p->screenEnabled[i] = next_random() & 0x17;
      p->screenWindowed[i] = next_random() & 0x17;
    }
    for (unsigned i = 0; i < 0x8000; ++i) p->vram[i] = (uint16_t)next_random();
    SmRasterLine line = {0};
    line.vram = p->vram;
    for (unsigned i = 0; i < 256; ++i) line.palette[i] = next_random() & 0x7fff;
    int width = widths[iteration % 4];
    SmViewport view = {width, (width - 256) / 2, 0, true};
    int y = (int)(next_random() % 224);
    for (int i = 0; i < width; ++i) {
      unsigned index = next_random() & 255;
      unsigned priority = 2 + (next_random() & 3) * 4;
      test_objects[i] = index ? (uint16_t)((priority << 12) | 0x400 | index) : 0;
    }
    SmBg2Object object = {0};
    object.valid = (iteration & 3) == 0;
    object.crocomire = (iteration & 4) != 0;
    object.parked = (iteration & 8) != 0;
    object.x = (int)(next_random() & 511) - 128;
    object.y = (int)(next_random() & 255);
    SmBackgroundLine backgrounds[3];
    bool center_valid[3] = {false};
    unsigned enabled = p->screenEnabled[0] | p->screenEnabled[1];
    for (unsigned layer = 0; layer < 3; ++layer) {
      backgrounds[layer] = background_line(p, test_ram, layer, object);
      if ((enabled & (1u << layer)) &&
          !((p->mosaic & (1u << layer)) && (p->mosaic >> 4)))
        center_valid[layer] = SnesRenderMode1TileRow(p, line.vram, layer, 0, y + 1,
                                                   256, center_pixels[layer]);
    }
    int wl[] = {p->window1left == 0 ? -view.extra : p->window1left,
                p->window2left == 0 ? -view.extra : p->window2left};
    int wr[] = {p->window1right == 255 ? 255 + view.extra : p->window1right,
                p->window2right == 255 ? 255 + view.extra : p->window2right};
    SnesRenderWindowPlan native = SnesRenderWindows(p, wl[0], wr[0], wl[1], wr[1]);
    SmWideWindow wide = {(iteration & 1) != 0, (int)(next_random() % 768) - 256,
                         (int)(next_random() % 768) - 256, (iteration & 2) != 0};
    if (wide.valid) {
      unsigned which = !wide.first;
      wl[which] = wide.left; wr[which] = wide.right;
    }
    SnesRenderWindowPlan margin = SnesRenderWindows(p, wl[0], wr[0], wl[1], wr[1]);
    uint8_t brightness[32];
    for (unsigned c = 0; c < 32; ++c)
      brightness[c] = (uint8_t)(((c << 3) | (c >> 2)) * (p->inidisp & 15) / 15);
    int matte_x = enabled & 1 ? backgrounds[0].world_x : (int16_t)read16(test_ram, 0x911);
    int matte_y = enabled & 1 ? backgrounds[0].world_y : (int16_t)read16(test_ram, 0x915);
    memset(expected, 0, sizeof(expected)); memset(actual, 0, sizeof(actual));
    for (int sx = 0; sx < width; ++sx) {
      int x = sx - view.extra;
      const SmBackgroundLine *world = &backgrounds[0];
      if ((x < 0 || x >= 256) && world->room_width &&
          (matte_x + x < 0 || matte_x + x >= world->room_width ||
           matte_y + y + 1 < 0 || matte_y + y + 1 >= world->room_height)) continue;
      uint16_t pixels[3] = {0};
      for (unsigned layer = 0; layer < 3; ++layer) {
        if (!(enabled & (1u << layer))) continue;
        int px = x, py = y + 1;
        if (p->mosaic & (1u << layer)) {
          int size = (p->mosaic >> 4) + 1;
          px -= ((px % size) + size) % size; py -= py % size;
        }
        pixels[layer] = background_pixel(&backgrounds[layer], line.vram, px, py, px < 0 || px >= 256);
      }
      uint16_t screens[] = {0x500, 0x500};
      for (unsigned sub = 0; sub < 2; ++sub) {
        for (unsigned layer = 0; layer < 3; ++layer) {
          unsigned bit = 1u << layer;
          if ((p->screenEnabled[sub] & bit) &&
              !((p->screenWindowed[sub] & bit) && in_window(p, layer, x, view.extra, &wide)) &&
              pixels[layer] > screens[sub]) screens[sub] = pixels[layer];
        }
        if ((p->screenEnabled[sub] & 16) &&
            (!(p->screenWindowed[sub] & 16) || !in_window(p, 4, x, view.extra, &wide)) &&
            test_objects[sx] > screens[sub]) screens[sub] = test_objects[sx];
      }
      expected[sx] = colour(p, line.palette, brightness, screens[0], screens[1],
                             in_window(p, 5, x, view.extra, &wide));
    }
    native_render_mode1_row(p, &line, backgrounds, center_pixels, center_valid, test_objects,
        y, view, matte_x, matte_y, brightness, &native, &margin, NULL, actual);
    for (int x = 0; x < width; ++x) {
      if (expected[x] != actual[x]) fprintf(stderr, "iteration=%u width=%d x=%d expected=%06x actual=%06x\n",
                                          iteration, width, x, expected[x], actual[x]);
      assert(expected[x] == actual[x]);
    }
    /* A finished center replaces only the center, even when predecoded
     * center tiles are unusable. Margin windows/matte keep their oracle. */
    memset(center_pixels, 0xa5, sizeof(center_pixels));
    memset(actual, 0, sizeof(actual));
    native_render_mode1_row(p, &line, backgrounds, center_pixels, center_valid, test_objects,
        y, view, matte_x, matte_y, brightness, &native, &margin, expected + view.extra, actual);
    for (int x = 0; x < width; ++x) assert(expected[x] == actual[x]);
  }
  puts("512 complete planned Metroid rows match independent scalar composition.");
  return 0;
}
