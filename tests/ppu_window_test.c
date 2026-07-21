#include "snes/ppu.h"

#include <assert.h>

static void test_authentic_width_noop(void) {
  int w1l = 0, w1r = 255, w2l = 32, w2r = 255;
  PpuWidescreenAdjustPinnedWindowEdges(0, 256, &w1l, &w1r, &w2l, &w2r);
  assert(w1l == 0);
  assert(w1r == 255);
  assert(w2l == 32);
  assert(w2r == 255);
}

static void test_pinned_window_edges_follow_widescreen_margins(void) {
  int w1l = 0, w1r = 255, w2l = 0, w2r = 255;
  PpuWidescreenAdjustPinnedWindowEdges(-43, 299, &w1l, &w1r, &w2l, &w2r);
  assert(w1l == -43);
  assert(w1r == 298);
  assert(w2l == -43);
  assert(w2r == 298);
}

static void test_non_edge_window_values_remain_native(void) {
  int w1l = 8, w1r = 247, w2l = 16, w2r = 240;
  PpuWidescreenAdjustPinnedWindowEdges(-12, 287, &w1l, &w1r, &w2l, &w2r);
  assert(w1l == 8);
  assert(w1r == 247);
  assert(w2l == 16);
  assert(w2r == 240);
}

static void test_bg3_widen_starts_below_hud_band(void) {
  Ppu ppu = {0};
  ppu.wsBg3WidenY = 32;

  assert(PpuWidescreenLayerExtra(&ppu, 2, 31, 43) == 0);
  assert(PpuWidescreenLayerExtra(&ppu, 2, 32, 43) == 43);
  assert(PpuWidescreenLayerExtra(&ppu, 0, 31, 43) == 43);
}

static void test_repeat_band_fills_effect_layer_below_hud(void) {
  Ppu ppu = {0};
  ppu.wsRepeatY0[2] = 32;
  ppu.wsRepeatY1[2] = 224;

  assert(!PpuWidescreenLayerRepeatBandActive(&ppu, 2, 31));
  assert(PpuWidescreenLayerRepeatBandActive(&ppu, 2, 32));
  assert(PpuWidescreenLayerRepeatBandActive(&ppu, 2, 223));
  assert(!PpuWidescreenLayerRepeatBandActive(&ppu, 2, 224));
  assert(PpuWidescreenLineRepeatBandActive(&ppu, 80));
  assert(PpuWidescreenLayerExtra(&ppu, 2, 80, 43) == 0);
  assert(PpuWidescreenLayerExtra(&ppu, 0, 80, 43) == 43);
}

static void test_stretch_band_scales_effect_layer_below_hud(void) {
  Ppu ppu = {0};
  ppu.wsStretchY0[2] = 32;
  ppu.wsStretchY1[2] = 224;

  assert(!PpuWidescreenLayerStretchBandActive(&ppu, 2, 31));
  assert(PpuWidescreenLayerStretchBandActive(&ppu, 2, 32));
  assert(PpuWidescreenLayerStretchBandActive(&ppu, 2, 223));
  assert(!PpuWidescreenLayerStretchBandActive(&ppu, 2, 224));
  assert(PpuWidescreenLineRepeatBandActive(&ppu, 80));
  assert(PpuWidescreenLayerExtra(&ppu, 2, 80, 43) == 0);
  assert(PpuWidescreenLayerExtra(&ppu, 0, 80, 43) == 43);
}

int main(void) {
  test_authentic_width_noop();
  test_pinned_window_edges_follow_widescreen_margins();
  test_non_edge_window_values_remain_native();
  test_bg3_widen_starts_below_hud_band();
  test_repeat_band_fills_effect_layer_below_hud();
  test_stretch_band_scales_effect_layer_below_hud();
  return 0;
}
