#pragma once
#include "sm_shot_view.h"

/* Borrow one resource-owned actor for the terrain owner's native A0C845 /
 * A0C879 reaction. Other actor slots and the native movement driver stay in
 * their original machine. Sand/spike PLMs consequently belong to the room
 * whose terrain was touched. */
enum { SM_ENEMY_POOL=0xf78,SM_ENEMY_BYTES=64 };
/* -1 keeps the resource machine's native dispatch; 0/1 returns native carry;
 * -2 reports a bounded cartridge execution failure. */
typedef int (*SmEnemyQuery)(void *context,uint8_t ram[0x20000],bool horizontal,uint16_t *index);
static inline bool SmEnemySample(const uint8_t *ram,bool horizontal,int *x,int *y) {
  unsigned index=SmShotWord(ram,0xe54);
  if (index>=2048 || (index&63) || !SmShotWord(ram,SM_ENEMY_POOL+index)) return false;
  int span=(int)SmShotWord(ram,0x1e)-(int)SmShotWord(ram,0x1c);
  if (span<0 || span>64) return false;
  if (horizontal) {
    *x=SmShotCell((int16_t)SmShotWord(ram,0x1a));
    *y=SmShotCell((int16_t)SmShotWord(ram,0xf7e +index)-(int)SmShotWord(ram,0xf84+index))+span;
  } else {
    *x=SmShotCell((int16_t)SmShotWord(ram,0xf7a+index)-(int)SmShotWord(ram,0xf82+index))+span;
    *y=SmShotCell((int16_t)SmShotWord(ram,0x1a));
  }
  return true;
}
static inline void SmEnemyTranslate(uint8_t *ram,unsigned index,int dx,int dy,bool horizontal) {
  SmShotPut(ram,0xf7a+index,(int16_t)SmShotWord(ram,0xf7a+index)+dx);
  SmShotPut(ram,0xf7e +index,(int16_t)SmShotWord(ram,0xf7e +index)+dy);
  int delta=horizontal ? dx : dy;
  SmShotPut(ram,0x18,(int16_t)SmShotWord(ram,0x18)+delta);
  SmShotPut(ram,0x1a,(int16_t)SmShotWord(ram,0x1a)+delta);
}
static inline void SmEnemyInput(uint8_t *to,const uint8_t *from,int dx,int dy,bool horizontal) {
  unsigned index=SmShotWord(from,0xe54);
  memcpy(to,from,64);memcpy(to+SM_ENEMY_POOL+index,from+SM_ENEMY_POOL+index,SM_ENEMY_BYTES);
  SmShotPut(to,0xe54,(int)index);SmShotPut(to,0x1e77,(int)SmShotWord(from,0x1e77));
  SmEnemyTranslate(to,index,dx,dy,horizontal);
}
static inline void SmEnemyOutput(uint8_t *to,const uint8_t *from,unsigned index,int dx,int dy,bool horizontal) {
  memcpy(to,from,64);memcpy(to+SM_ENEMY_POOL+index,from+SM_ENEMY_POOL+index,SM_ENEMY_BYTES);
  SmShotPut(to,0x1e77,(int)SmShotWord(from,0x1e77));
  SmEnemyTranslate(to,index,dx,dy,horizontal);
}
