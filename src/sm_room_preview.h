#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Immutable ROM-derived scenery. Coordinates are relative to the active
 * room; none of these operations execute guest code or modify guest RAM. */
void SmRoomPreviewSetRom(const uint8_t *rom, size_t size);
void SmRoomPreviewPrepare(const uint8_t ram[0x20000], int left, int top,
                           int right, int bottom);
bool SmRoomPreviewPixel(int x, int y, uint16_t *color);
bool SmRoomPreviewPixelAt(int x, int y, int camera_x, int camera_y, uint16_t *color);
unsigned SmRoomPreviewCount(void);
