#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "sm_room_scene.h"
#include "mods/sm_world.h"

/* Immutable ROM-derived scenery. Coordinates are relative to the active
 * room; none of these operations execute guest code or modify guest RAM. */
void SmRoomPreviewSetRom(const uint8_t *rom, size_t size);
void SmRoomPreviewPrepare(const uint8_t ram[0x20000], int left, int top,
                           int right, int bottom);
bool SmRoomPreviewPixel(int x, int y, uint16_t *color);
bool SmRoomPreviewPixelAt(int x, int y, int camera_x, int camera_y, uint16_t *color);
unsigned SmRoomPreviewCount(void);
bool SmRoomPreviewPlacement(unsigned index, SmRoomPlacement *placement);
const SmWorldRoom *SmRoomPreviewRoom(uint16_t header);
const SmWorldDoor *SmRoomPreviewDoors(uint16_t room, unsigned *count);
const uint8_t *SmRoomPreviewVram(unsigned index);
bool SmRoomSceneSample(const SmRoomImage *room, int x, int y,
    int camera_x, int camera_y, uint16_t layers[3]);
