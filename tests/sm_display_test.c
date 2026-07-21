#include "sm_display.h"

#include <assert.h>
#include <limits.h>

static void test_widths(void) {
  assert(SmDisplay_ComputeFrameWidth(4, 3, true) == 256);
  assert(SmDisplay_ComputeFrameWidth(16, 10, true) == 308);
  assert(SmDisplay_ComputeFrameWidth(16, 9, true) == 342);
  assert(SmDisplay_ComputeFrameWidth(21, 9, true) == 446);
  assert(SmDisplay_ComputeFrameWidth(1, 2, true) == 256);
  assert(SmDisplay_ComputeFrameWidth(INT_MAX, 1, true) == 446);
  assert(SmDisplay_ComputeFrameWidth(0, 0, true) == 256);
  assert(SmDisplay_ComputeFrameWidth(16, 9, false) == 256);
}

static void test_presentation(void) {
  int width, height;
  SmDisplayViewport viewport;
  SmDisplay_ComputePresentationSize(342, 224, &width, &height);
  assert(width == 399 && height == 224);
  SmDisplay_ComputeViewport(342, 224, 1920, 1080, false, false, &viewport);
  assert(viewport.width == 1920 && viewport.height == 1080);
  assert(viewport.x == 0 && viewport.y == 0);
  SmDisplay_ComputeViewport(342, 224, 3840, 2160, false, false, &viewport);
  assert(viewport.width == 3840 && viewport.height == 2160);
  assert(viewport.x == 0 && viewport.y == 0);
  SmDisplay_ComputeViewport(342, 224, 1600, 1000, false, false, &viewport);
  assert(viewport.width == 1600 && viewport.height == 898);
  assert(viewport.x == 0 && viewport.y == 51);
  SmDisplay_ComputeViewport(256, 224, 800, 600, false, false, &viewport);
  assert(viewport.width == 800 && viewport.height == 600);
  assert(viewport.x == 0 && viewport.y == 0);
}

int main(void) {
  test_widths();
  test_presentation();
  return 0;
}
