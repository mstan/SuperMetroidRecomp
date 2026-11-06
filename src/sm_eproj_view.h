#pragma once
#include "sm_shot_view.h"

/* Read-only terrain borrowed by the cartridge's enemy-projectile collision
 * routine. Cell coordinates are relative to the projectile's resource owner;
 * extension blocks have already resolved to their actual owning cell. */
typedef struct SmEprojTile { int x,y; uint16_t tile; uint8_t bts; } SmEprojTile;
typedef bool (*SmEprojQuery)(void *context,const uint8_t ram[0x20000],int x,int y,SmEprojTile *tile);
typedef struct SmEprojLoan {
  bool active,horizontal;
  uint16_t index,slot,tile;
  uint8_t bts;
  int dx,dy;
} SmEprojLoan;

static inline bool SmEprojSample(const uint8_t *ram,bool horizontal,int *x,int *y) {
  unsigned slot=SmShotWord(ram,0x1991);
  if (slot>=36 || (slot&1) || !SmShotWord(ram,0x1997+slot)) return false;
  int span=(int)SmShotWord(ram,0x20)-(int)SmShotWord(ram,0x1a);
  if (span<0 || span>32) return false;
  if (horizontal) {
    *x=SmShotCell((int16_t)SmShotWord(ram,0x22));
    *y=SmShotCell((int16_t)SmShotWord(ram,0x1a93+slot)-(int)SmShotWord(ram,0x1e))+span;
  } else {
    *x=SmShotCell((int16_t)SmShotWord(ram,0x1a4b+slot)-(int)SmShotWord(ram,0x1c))+span;
    *y=SmShotCell((int16_t)SmShotWord(ram,0x22));
  }
  return true;
}
static inline bool SmEprojBegin(uint8_t *ram,uint16_t *index,bool horizontal,
    SmEprojQuery query,void *context,SmEprojLoan *loan) {
  int x,y;SmEprojTile target;
  if (!query || loan->active || !SmEprojSample(ram,horizontal,&x,&y) || !query(context,ram,x,y,&target)) return false;
  unsigned type=target.tile>>12;
  if (type==5 || type==13) return false; /* queries must resolve extensions */
  *loan=(SmEprojLoan){.active=true,.horizontal=horizontal,.index=*index,
      .slot=(uint16_t)SmShotWord(ram,0x1991),.tile=(uint16_t)SmShotWord(ram,0x10002),
      .bts=ram[0x16402],.dx=target.x*16,.dy=target.y*16};
  /* A temporary cell at (0,0) keeps native slope and half-block geometry in
   * the same coordinate system. No other actor or game tick runs in this
   * bounded, read-only reaction; restore it before the cartridge's RTS. */
  SmShotPut(ram,0x10002,target.tile);ram[0x16402]=target.bts;
  SmShotPut(ram,0x1a4b+loan->slot,(int16_t)SmShotWord(ram,0x1a4b+loan->slot)-loan->dx);
  SmShotPut(ram,0x1a93+loan->slot,(int16_t)SmShotWord(ram,0x1a93+loan->slot)-loan->dy);
  int delta=horizontal ? loan->dx : loan->dy;
  SmShotPut(ram,0x18,(int16_t)SmShotWord(ram,0x18)-delta);
  SmShotPut(ram,0x22,(int16_t)SmShotWord(ram,0x22)-delta);
  *index=0;
  return true;
}
static inline bool SmEprojEnd(uint8_t *ram,uint16_t *index,bool horizontal,SmEprojLoan *loan) {
  if (!loan->active || loan->horizontal!=horizontal) return false;
  SmShotPut(ram,0x1a4b+loan->slot,(int16_t)SmShotWord(ram,0x1a4b+loan->slot)+loan->dx);
  SmShotPut(ram,0x1a93+loan->slot,(int16_t)SmShotWord(ram,0x1a93+loan->slot)+loan->dy);
  int delta=horizontal ? loan->dx : loan->dy;
  SmShotPut(ram,0x18,(int16_t)SmShotWord(ram,0x18)+delta);
  SmShotPut(ram,0x22,(int16_t)SmShotWord(ram,0x22)+delta);
  SmShotPut(ram,0x10002,loan->tile);ram[0x16402]=loan->bts;
  SmShotPut(ram,0xdc4,loan->index/2);*index=loan->index;
  loan->active=false;
  return true;
}
