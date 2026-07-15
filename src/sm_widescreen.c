#include <stdint.h>

#include "cpu_state.h"
#include "snes/ws_shadow.h"
#include "widescreen.h"

extern uint8_t g_ram[0x20000];

static uint16_t SmWsRead16(uint16_t address) {
  return (uint16_t)(g_ram[address] | (g_ram[(uint16_t)(address + 1)] << 8));
}

void SmWidescreenGetSideSpace(int camera_x, int camera_y, int max_extra,
                              int *left_pixels, int *right_pixels) {
  int left = 0, right = 0;
  int room_width = SmWsRead16(0x07A9);
  int room_height = SmWsRead16(0x07AB);

  /* Super Metroid divides each room into 256x256 scroll screens. A zero in
   * $7E:CD20 marks a locked/unavailable screen even though decompressed level
   * blocks may still occupy that part of the room buffer. Mirror the game's
   * horizontal camera test (which uses the viewport's vertical midpoint),
   * then expose only the contiguous run of nonzero screens containing the
   * camera. This keeps red-scroll screens and stale blockmap storage out of
   * the widescreen margins. */
  if (max_extra > 0 && room_width > 0 && room_height > 0 &&
      room_width <= 255 && room_height <= 255) {
    int row = (camera_y + 128) >> 8;
    if (row < 0)
      row = 0;
    if (row >= room_height)
      row = room_height - 1;

    int col = (camera_x + 128) >> 8;
    if (col < 0)
      col = 0;
    if (col >= room_width)
      col = room_width - 1;

    const uint8_t *scrolls = g_ram + 0xCD20;
    int row_base = row * room_width;
    if (!scrolls[row_base + col]) {
      int native_left_col = camera_x >> 8;
      int native_right_col = (camera_x + 255) >> 8;
      if (native_left_col >= 0 && native_left_col < room_width &&
          scrolls[row_base + native_left_col]) {
        col = native_left_col;
      } else if (native_right_col >= 0 && native_right_col < room_width &&
                 scrolls[row_base + native_right_col]) {
        col = native_right_col;
      } else {
        col = -1;
      }
    }

    if (col >= 0) {
      int first = col;
      int last = col;
      while (first > 0 && scrolls[row_base + first - 1])
        first--;
      while (last + 1 < room_width && scrolls[row_base + last + 1])
        last++;

      int interval_left = first << 8;
      int interval_right = (last + 1) << 8;
      left = camera_x - interval_left;
      right = interval_right - (camera_x + 256);
      if (left < 0)
        left = 0;
      if (right < 0)
        right = 0;
      if (left > max_extra)
        left = max_extra;
      if (right > max_extra)
        right = max_extra;
    }
  }

  *left_pixels = left;
  *right_pixels = right;
}

static uint16_t SmWsReadTilemapEntry(uint32_t blockmap_base,
                                     uint32_t world_tile_x,
                                     uint32_t world_tile_y) {
  uint16_t room_width = SmWsRead16(0x07A5);
  uint16_t room_height = SmWsRead16(0x07A7);
  uint32_t block_x = world_tile_x >> 1;
  uint32_t block_y = world_tile_y >> 1;
  if (!room_width || block_x >= room_width || block_y >= room_height)
    return 0;

  /* The complete room blockmap is decompressed at $7F:0002. Each block
   * selects a 2x2 group of raw SNES tilemap entries in the table at
   * $7E:A000; bits 10/11 flip the whole 16x16 block. This is the same
   * expansion performed by the game's native row/column VRAM streamer. */
  uint32_t block_address =
      blockmap_base + 2u * (block_y * room_width + block_x);
  if (block_address + 1 >= 0x20000u)
    return 0;
  uint16_t block = (uint16_t)(g_ram[block_address] |
      (g_ram[block_address + 1] << 8));
  uint32_t quadrant_x = world_tile_x & 1;
  uint32_t quadrant_y = world_tile_y & 1;
  uint16_t flip = 0;
  if (block & 0x0400) {
    quadrant_x ^= 1;
    flip ^= 0x4000;
  }
  if (block & 0x0800) {
    quadrant_y ^= 1;
    flip ^= 0x8000;
  }

  uint32_t table_address = 0xA000u + (uint32_t)(block & 0x03ff) * 8u +
                           (quadrant_y * 2u + quadrant_x) * 2u;
  return (uint16_t)(SmWsRead16((uint16_t)table_address) ^ flip);
}

static void SmWsPrefillLayerMargins(int layer, uint32_t blockmap_base,
                                    int origin_x, int origin_y,
                                    int viewport_world_x,
                                    int left_pixels, int right_pixels) {
  int first_y = origin_y >> 3;
  int last_y = (origin_y + 223) >> 3;
  int first_left_x = (origin_x - left_pixels) >> 3;
  int last_left_x = (origin_x - 1) >> 3;
  int first_right_x = (origin_x + 256) >> 3;
  int last_right_x = (origin_x + 255 + right_pixels) >> 3;

  for (int tile_y = first_y; tile_y <= last_y; tile_y++) {
    for (int tile_x = first_left_x; tile_x <= last_left_x; tile_x++) {
      if (tile_x >= 0 && tile_y >= 0) {
        uint16_t entry = SmWsReadTilemapEntry(blockmap_base,
                                               (uint32_t)tile_x,
                                               (uint32_t)tile_y);
        /* Landing Site BG2 is a static, half-speed library background whose
         * 512px map contains stale ship-shaped data outside the single real
         * world instance. Keep ship tiles only in its physical world span
         * ($0400..$04FF); the authentic 256px center remains untouched. */
        if (layer == 1) {
          int physical_x = viewport_world_x + tile_x * 8 - origin_x;
          if (physical_x < 0x400 || physical_x >= 0x500)
            entry = 0x0338;  // verified all-zero 4bpp character in this tileset
        }
        WsShadowPrefillTile(layer, (uint32_t)tile_x, (uint32_t)tile_y,
                            entry);
      }
    }
    for (int tile_x = first_right_x; tile_x <= last_right_x; tile_x++) {
      if (tile_x >= 0 && tile_y >= 0) {
        uint16_t entry = SmWsReadTilemapEntry(blockmap_base,
                                               (uint32_t)tile_x,
                                               (uint32_t)tile_y);
        if (layer == 1) {
          int physical_x = viewport_world_x + tile_x * 8 - origin_x;
          if (physical_x < 0x400 || physical_x >= 0x500)
            entry = 0x0338;
        }
        WsShadowPrefillTile(layer, (uint32_t)tile_x, (uint32_t)tile_y,
                            entry);
      }
    }
  }
}

void SmWidescreenPrefillRoomMargins(int camera_x, int camera_y,
                                    int layer2_x, int layer2_y,
                                    int left_pixels, int right_pixels) {
  SmWsPrefillLayerMargins(0, 0x10002u, camera_x, camera_y, camera_x,
                          left_pixels, right_pixels);

  /* Landing Site ($8F:91F8) stores its ship-bearing BG2 blockmap at
   * $7F:9602. BG2's live 512px tilemap aliases the ship into the opposite
   * widescreen margin near a map-half boundary, so supply exact world tiles
   * for both margins just as we do for BG1. Other rooms retain their native
   * BG2 policy until their background-data contracts are verified. */
  if (SmWsRead16(0x079B) == 0x91F8) {
    SmWsPrefillLayerMargins(1, 0x19602u, layer2_x, layer2_y, camera_x,
                            left_pixels, right_pixels);
  }
}

static int SmWsEnemyInView(int include_half_width) {
  if (!g_ws_active || g_ws_extra <= 0)
    return 0;

  uint16_t enemy_index = SmWsRead16(0x0E54);
  uint16_t x = SmWsRead16((uint16_t)(0x0F7A + enemy_index));
  uint16_t half_width = include_half_width
      ? SmWsRead16((uint16_t)(0x0F82 + enemy_index))
      : 0;
  uint16_t camera_x = SmWsRead16(0x0911);

  /* These are the ROM's two signed horizontal overlap tests with the
   * configured side margin added symmetrically. Keep the vertical tests in
   * generated code untouched: widescreen is horizontal-only. */
  int32_t left_distance =
      (int32_t)x + half_width - camera_x + g_ws_extra;
  int32_t right_distance =
      (int32_t)camera_x + 256 + g_ws_extra + half_width - x;
  return left_distance >= 0 && right_distance >= 0;
}

int SmWidescreenEnemyCenterInView(CpuState *cpu) {
  (void)cpu;
  return SmWsEnemyInView(0);
}

int SmWidescreenEnemyBoxInView(CpuState *cpu) {
  (void)cpu;
  return SmWsEnemyInView(1);
}
