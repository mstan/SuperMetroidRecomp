#pragma once
#include <stdbool.h>
#include <stdint.h>

/* Read-only Zero Mission USA resource. No cartridge program is imported. */
typedef struct SmMapArt {
  uint8_t tiles[0x3000];
  uint16_t palette[80];
  bool ready;
} SmMapArt;
bool SmMapArtLoad(SmMapArt *art, const char *path);
uint32_t SmMapArtPixel(const SmMapArt *art, unsigned tile, unsigned x,
                      unsigned y, unsigned palette);
/* Native wall tiles, with flips; mask bits are left/right/top/bottom. */
unsigned SmMapArtRoomTile(unsigned walls);
