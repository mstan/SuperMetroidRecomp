#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "mods/sm_world.h"
enum { SM_DOOR_SHUT, SM_DOOR_OPENING, SM_DOOR_OPEN, SM_DOOR_CLOSING };
/* Native SpawnDoorClosingPLM indexes 8F:E68A with the full direction.
 * Directions 0..3 have zero entries and no arrival cap; their cap X/Y
 * fields can be placeholders and must never become synthetic bubbles. */
static inline bool SmDoorHasArrivalCap(unsigned direction) {
  return direction>=4 && direction<8;
}
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
/* Body-only policy after the native weapon/quota handler starts opening.
 * Resolve native extension blocks, and admit only actual door-cap BTS values.
 * Closed, closing, ordinary shot blocks and trigger cells keep their policy. */
static inline bool SmDoorOpeningBodyClear(SmWorldRead read,void *context,const uint8_t *ram,int x,int y) {
  unsigned w=SmDoorWord(ram+0x7a5),h=SmDoorWord(ram+0x7a7);
  if ((uint64_t)w*h>0x3200) return false;
  for (unsigned i=0;i<16;++i) {
    if (x<0 || y<0 || (unsigned)x>=w || (unsigned)y>=h) return false;
    unsigned index=(unsigned)y*w+(unsigned)x;
    unsigned type=SmDoorWord(ram+0x10002+2*index)>>12;
    int bts=(int8_t)ram[0x16402+index];
    if (type==5 && bts) { x+=bts;continue; }
    if (type==13 && bts) { y+=bts;continue; }
    if (type!=8 && type!=12) return false;
    if (type==12 && (bts<0x40 || bts>0x44)) return false;
    /* Draw commands replace the extension types with solid animation tiles,
     * retaining FF/FE/FD BTS. The live PLM owns its four-cell cap, so those
     * cells must be admitted along with the anchor. */
    for (unsigned slot=0;slot<40;++slot) {
      unsigned header=SmDoorWord(ram+0x1c37+2*slot);
      if (header<0xc842 || header>0xc8b4 || (header-0xc842)%6) continue;
      unsigned block=SmDoorWord(ram+0x1c87+2*slot);
      if (block&1) continue;
      unsigned anchor=block/2,ax=anchor%w,ay=anchor/w,orientation=((header-0xc842)/6)%4;
      bool span=orientation<2 ? (unsigned)x==ax && (unsigned)y>=ay && (unsigned)y<ay+4 :
          (unsigned)y==ay && (unsigned)x>=ax && (unsigned)x<ax+4;
      if (span && SmDoorPhase(read,context,ram,ax,ay)==SM_DOOR_OPENING) return true;
    }
    return false;
  }
  return false;
}
