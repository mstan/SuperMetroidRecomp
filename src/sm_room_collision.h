#pragma once
#include <stdbool.h>
#include <stdint.h>
/* Cartridge direct-page collision workspace, decoded without unsigned
 * coordinate wrapping at a room boundary. Coordinates are tile cells. */
void SmRoomCollisionSample(const uint8_t ram[0x20000],bool horizontal,unsigned original_index,int *x,int *y);
int SmRoomCollisionBody(const uint8_t ram[0x20000],bool horizontal,unsigned original_index);
int SmRoomCollisionInside(const uint8_t ram[0x20000]);
bool SmRoomCollisionFrameBegin(void);
