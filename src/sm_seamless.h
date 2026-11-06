#pragma once
#include <stdbool.h>
#include <stdint.h>
/* Called only at the guest's ordinary frame boundary. Native transition
 * mode and non-planar connections retain their cartridge scheduler. */
bool SmSeamlessDoor(void);
bool SmActiveDoorEvent(unsigned block_x, unsigned block_y, unsigned orientation, bool closing);
int SmActiveEnemyBlock(uint8_t source[0x20000],int offset_x,int offset_y,int block_x,int block_y,bool horizontal);
