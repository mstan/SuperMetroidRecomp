#pragma once
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

enum { SM_ROOM_RESIDENTS = 16 };
typedef struct SmRoomPlacement {
  uint16_t room, state, incoming, reserved;
  int32_t x, y, width, height, left, top, right, bottom;
} SmRoomPlacement;
typedef struct SmRoomImage {
  SmRoomPlacement placement;
  uint8_t ram[0x20000], vram[0x10000];
} SmRoomImage;
/* A publication has no host pointers and owns all of its room resources. */
typedef struct SmRoomScene {
  uint32_t version, count, logic, seamless, root;
  SmRoomImage rooms[];
} SmRoomScene;
static inline size_t SmRoomSceneBytes(unsigned count) {
  return count<=SM_ROOM_RESIDENTS ? sizeof(SmRoomScene)+(size_t)count*sizeof(SmRoomImage) : 0;
}
static inline bool SmRoomSceneValid(const SmRoomScene *scene) {
  if (!scene || scene->version!=1 || scene->count>SM_ROOM_RESIDENTS || scene->logic>1 ||
      scene->seamless>1 || scene->root>65535) return false;
  for (unsigned i=0; i<scene->count; ++i) {
    const SmRoomImage *image=&scene->rooms[i]; const SmRoomPlacement *v=&image->placement;
    unsigned w=image->ram[0x7a5] | image->ram[0x7a6]<<8,h=image->ram[0x7a7] | image->ram[0x7a8]<<8;
    if (v->reserved || v->room<0x8000 || v->room==scene->root || v->state<0x8000 ||
        v->width<=0 || v->width>4096 || v->height<=0 || v->height>4096 ||
        w*16!=(unsigned)v->width || h*16!=(unsigned)v->height || (uint64_t)w*h>0x3200 ||
        v->x < -65536 || v->x>65536 || v->y < -65536 || v->y>65536 ||
        v->left < -131072 || v->right>131072 || v->top < -131072 || v->bottom>131072 ||
        v->left>=v->right || v->top>=v->bottom) return false;
  }
  return true;
}
