#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "mods/sm_world.h"
enum { SM_DOOR_SHUT, SM_DOOR_OPENING, SM_DOOR_OPEN, SM_DOOR_CLOSING };
static inline unsigned SmDoorWord(const uint8_t *p) { return p[0] | p[1]<<8; }
static inline bool SmDoorHeader(unsigned h) {
  return h==0xc83e || (h>=0xc842 && h<=0xc8b4 && !((h-0xc842)%6)) ||
      (h>=0xc8ba && h<=0xc8c6 && !((h-0xc8ba)%4));
}
static inline unsigned SmDoorPhase(SmWorldRead read,void *context,const uint8_t *ram,unsigned x,unsigned y) {
  unsigned w=SmDoorWord(ram+0x7a5),h=SmDoorWord(ram+0x7a7);
  if (x>=w || y>=h || (uint64_t)w*h>0x3200) return SM_DOOR_SHUT;
  unsigned block=2*(y*w+x);
  for (unsigned i=0; i<40; ++i) if (SmDoorWord(ram+0x1c87+2*i)==block) {
    unsigned header=SmDoorWord(ram+0x1c37+2*i);
    if (header>=0xc8ba && header<=0xc8c6 && !((header-0xc8ba)%4)) return SM_DOOR_CLOSING;
    if (header>=0xc842 && header<=0xc8b4 && !((header-0xc842)%6)) {
      uint32_t p=0x840000u+header;
      unsigned primary=read(context,p+2) | read(context,p+3)<<8;
      unsigned closing=read(context,p+4) | read(context,p+5)<<8;
      unsigned instruction=SmDoorWord(ram+0x1d27+2*i);
      if (header<0xc8a2 ? instruction>=closing && instruction<primary :
          instruction>=closing && instruction<closing+28) return SM_DOOR_CLOSING;
    }
    if ((header>=0xc8a2 && header<=0xc8b4 && !((header-0xc8a2)%6)) ||
        (header>=0xc842 && header<0xc8a2 && SmDoorWord(ram+0x1cd7+2*i)==0x8aa6)) return SM_DOOR_OPENING;
  }
  unsigned type=SmDoorWord(ram+0x10002+block)>>12;
  return type==0 || type==4 ? SM_DOOR_OPEN : SM_DOOR_SHUT;
}
