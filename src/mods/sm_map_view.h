#pragma once
#include "sm_world.h"

typedef struct SmMapView {
  int x, y, width, height, cell;
  int center_x16, center_y16; /* sixteenth-cell units */
  int area; /* -1 for the whole-world layout */
  bool reveal, secrets, transparent;
} SmMapView;

void SmMapViewDraw(uint32_t *pixels, int stride, int height, const SmWorld *world,
                    const int16_t layout[SM_WORLD_ROOMS][2], SmWorldRead read,
                    void *context, const uint8_t *wram, SmMapView view);
void SmMapPlayerPoint(const SmWorld *world, const int16_t layout[SM_WORLD_ROOMS][2],
                       const uint8_t *wram, bool whole_world, int *x16, int *y16);
uint32_t SmMapAreaColor(unsigned area);
const char *SmMapAreaName(unsigned area);
