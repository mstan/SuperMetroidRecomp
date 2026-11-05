#include "sm_room_collision.h"
#include "sm_rooms.h"
static unsigned word(const uint8_t *r,unsigned a) { return r[a] | r[a+1]<<8; }
static int cell(int x) { return x<0 ? -((-x+15)/16) : x/16; }
void SmRoomCollisionSample(const uint8_t r[0x20000],bool horizontal,unsigned original_index,int *x,int *y) {
  if (horizontal) {
    *x=cell((int16_t)word(r,0x20));
    *y=cell((int16_t)word(r,0xafa)-(int)word(r,0xb00))+
        (int)word(r,0x1c)-(int)word(r,0x1a);
  } else {
    *y=cell((int16_t)word(r,0x20));
    /* The native tile index loses its high bit after ASL/LSR and the multiply
     * uses eight-bit rows. Recover X before either unsigned wrap can turn the
     * preceding column into a distant column of the following row. This
     * works for both native left-to-right and right-to-left span iterations. */
    int raw=(int)((original_index-(((word(r,0x20)>>4)&255)*word(r,0x7a5)))&0x7fff);
    *x=raw>=16384 ? raw-32768 : raw>=2048 ? raw-4096 : raw;
  }
}
int SmRoomCollisionBody(const uint8_t r[0x20000],bool horizontal,unsigned original_index) {
  int x,y; SmRoomCollisionSample(r,horizontal,original_index,&x,&y);
  return SmRoomsCollisionBlock(r,x,y);
}
int SmRoomCollisionInside(const uint8_t r[0x20000]) {
  return SmRoomsCollisionBlock(r,cell((int16_t)word(r,0x1a)),cell((int16_t)word(r,0x1c)));
}
