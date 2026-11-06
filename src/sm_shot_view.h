#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

/* The cartridge keeps all player weapons in one linked slot pool. A block
 * reaction borrows that pool and the collision workspace in its own room's
 * coordinates; it does not become another projectile simulation. */
enum { SM_SHOT_POOL = 0xb64, SM_SHOT_POOL_BYTES = 0x170 };
static inline unsigned SmShotWord(const uint8_t *r,unsigned at) { return r[at] | r[at+1]<<8; }
static inline void SmShotPut(uint8_t *r,unsigned at,int value) {
  r[at]=(uint8_t)value; r[at+1]=(uint8_t)((unsigned)value>>8);
}
static inline int SmShotCell(int x) { return x<0 ? -((-x+15)/16) : x/16; }
static inline bool SmShotSample(const uint8_t *r,bool horizontal,int *x,int *y) {
  unsigned index=SmShotWord(r,0xdde);
  if (index>=20 || (index&1)) return false;
  bool missile=SmShotWord(r,0x1e)==1;
  int span=missile ? 0 : (int)SmShotWord(r,0x1a)-(int)SmShotWord(r,0x26);
  if (span<0 || span>16) return false;
  if (horizontal) {
    *x=SmShotCell((int16_t)SmShotWord(r,0x1c));
    *y=SmShotCell((int16_t)SmShotWord(r,0xb78+index)-
        (missile ? 0 : (int)SmShotWord(r,0xbc8+index)))+span;
  } else {
    *x=SmShotCell((int16_t)SmShotWord(r,0xb64+index)-
        (missile ? 0 : (int)SmShotWord(r,0xbb4+index)))+span;
    *y=SmShotCell((int16_t)SmShotWord(r,0x1c));
  }
  return true;
}
static inline void SmShotTranslate(uint8_t *r,int x,int y) {
  for (unsigned i=0; i<20; i+=2) {
    if (!SmShotWord(r,0xc18+i) && !SmShotWord(r,0xc40+i)) continue;
    SmShotPut(r,0xb64+i,(int16_t)SmShotWord(r,0xb64+i)+x);
    SmShotPut(r,0xb78+i,(int16_t)SmShotWord(r,0xb78+i)+y);
  }
}
static inline void SmShotInput(uint8_t *to,const uint8_t *from,int x,int y,bool horizontal) {
  memcpy(to,from,64);
  memcpy(to+SM_SHOT_POOL,from+SM_SHOT_POOL,SM_SHOT_POOL_BYTES);
  SmShotPut(to,0xdde,(int)SmShotWord(from,0xdde));
  SmShotTranslate(to,x,y);
  int axis=horizontal ? x : y;
  SmShotPut(to,0x18,(int16_t)SmShotWord(to,0x18)+axis);
  SmShotPut(to,0x1c,(int16_t)SmShotWord(to,0x1c)+axis);
}
static inline void SmShotOutput(uint8_t *to,const uint8_t *from,int x,int y) {
  memcpy(to+SM_SHOT_POOL,from+SM_SHOT_POOL,SM_SHOT_POOL_BYTES);
  SmShotTranslate(to,x,y);
  SmShotPut(to,0x26,(int)SmShotWord(from,0x26));
  SmShotPut(to,0x28,(int)SmShotWord(from,0x28));
}
