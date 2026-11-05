#include "sm_rooms.h"
#include "sm_room_collision.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static uint8_t active[0x20000], original[0x20000];
static void put(unsigned at,int value) { active[at]=(uint8_t)value; active[at+1]=(uint8_t)((unsigned)value>>8); }
static void samples(void) {
  /* Exercise both scan directions, half-body boundary overlap and the
   * cartridge's eight-bit multiply / fifteen-bit tile-index aliases. */
  for (unsigned width=16; width<=256; width+=16) for (int y=-1; y<257; y+=17) {
    put(0x7a5,(int)width); put(0x20,y*16);
    for (int x=-1; x<(int)width+2; ++x) {
      int native_x=x<0 ? 4096+x : x;
      int product=((unsigned)y&255)*width;
      unsigned index=(unsigned)(product+native_x)&0x7fff;
      put(0xdc4,0x1234); /* an extension changed the current anchor */
      int bx,by; SmRoomCollisionSample(active,false,index,&bx,&by);
      assert(bx==x && by==y);
      if (x==-1) {
        index=(unsigned)(product-1)&0x7fff;
        SmRoomCollisionSample(active,false,index,&bx,&by); assert(bx==x && by==y);
      }
    }
  }
  put(0x20,-1); put(0xafa,8); put(0xb00,21); put(0x1c,3);
  for (unsigned remaining=0; remaining<4; ++remaining) {
    put(0x1a,(int)remaining); int x,y; SmRoomCollisionSample(active,true,0,&x,&y);
    assert(x==-1 && y==2-(int)remaining);
  }
  puts("native collision sample coordinates survive both boundary/scan-direction wraps");
}
int main(int argc,char **argv) {
  if (argc!=3 && argc!=4) return 2;
  samples();
  uint8_t *rom=malloc(0x300000); assert(rom);
  FILE *f=fopen(argv[1],"rb"); assert(f); assert(fread(rom,1,0x300000,f)==0x300000); fclose(f);
  f=fopen(argv[2],"rb"); assert(f); assert(fread(active,1,sizeof(active),f)==sizeof(active)); fclose(f);
  memcpy(original,active,sizeof(active));
  for (unsigned logic=0; logic<2; ++logic) {
    SmRoomsSetRom(rom,0x300000);
    for (unsigned frame=0; frame<60; ++frame) assert(SmRoomsFrame(active,true,213,logic!=0,true));
    const SmRoomScene *scene=SmRoomsScene(); assert(scene && scene->count && scene->logic==logic);
    if (!logic) for (unsigned i=0; i<scene->count; ++i) for (unsigned slot=0; slot<32; ++slot)
      assert(!scene->rooms[i].ram[0xf78+slot*64] && !scene->rooms[i].ram[0xf79+slot*64]);
    size_t bound=SmRoomsStateBound(); void *saved=malloc(bound),*a=malloc(bound),*b=malloc(bound); assert(saved && a && b);
    size_t bytes=SmRoomsSave(saved,bound); assert(bytes && SmRoomsValidate(saved,bytes));
    assert(!SmRoomsLoad(saved,bytes-1));
    assert(SmRoomsFrame(active,true,213,logic!=0,true)); size_t na=SmRoomsSave(a,bound); assert(na);
    assert(SmRoomsLoad(saved,bytes));
    assert(SmRoomsFrame(active,true,213,logic!=0,true)); size_t nb=SmRoomsSave(b,bound); assert(nb==na && !memcmp(a,b,na));
    assert(!memcmp(active,original,sizeof(active)));
    if (active[0x79b]==0xf8 && active[0x79c]==0x91 &&
        original[0xaf7]==8 && original[0xafa]>=0x60 && original[0xafb]==1) {
      SmRoomPlacement destination; uint16_t door=0;
      assert(SmRoomsCollisionBlock(active,143,24)==0);
      assert(SmRoomsCollisionBlock(active,144,24)==0);
      assert(SmRoomsCollisionBlock(active,145,24)==0);
      assert(SmRoomsCollisionBlock(active,144,21)==16);
      put(0xaf6,2303); put(0xafa,400);
      assert(!SmRoomsCrossing(active,&door,&destination));
      put(0xaf6,2304);
      assert(SmRoomsCrossing(active,&door,&destination) && door==0x893a &&
          destination.room==0x93aa && destination.x==2304 && destination.y==256);
      memcpy(active,original,sizeof(active));
      /* At 4:3 the native camera stops before the continuous doorway. Keep
       * the body visible and move presentation by the same two pixels per
       * tick, rather than waiting for an offscreen center crossing. */
      int previous_camera=0;
      for (int x=2290; x<2304; x+=2) {
        put(0xaf6,x);
        assert(SmRoomsFrame(active,true,0,logic!=0,true));
        int camera=SmRoomsScene()->camera_dx;
        assert(abs(camera-previous_camera)<=2);
        assert(x-2048-camera<=243);
        previous_camera=camera;
      }
      memcpy(active,original,sizeof(active));
      /* A source closing event must retain the real opposing cap collision.
       * This fixture changes the source tile only; native paired PLMs close
       * the resident face and the border query must then report solid. */
      unsigned cap=0x10002+2*(22*144+142);
      active[cap+1]=(uint8_t)((active[cap+1]&15)|0xc0);
      for (unsigned frame=0; frame<60; ++frame) assert(SmRoomsFrame(active,true,213,logic!=0,true));
      assert(SmRoomsCollisionBlock(active,145,24)==16);
      memcpy(active,original,sizeof(active));
      puts("open portal samples, exact center-crossing ownership and closed opposing cap collision pass");
    }
    printf("logic %s: %u resident rooms, 60 native ticks, exact whole-manager save/load continuation; active RAM unchanged\n",logic ? "On" : "Off",SmRoomsScene()->count);
    free(saved); free(a); free(b);
  }
  if (argc==4) {
    f=fopen(argv[3],"rb"); assert(f); assert(fread(original,1,sizeof(original),f)==sizeof(original)); fclose(f);
    assert((original[0x79b] | original[0x79c]<<8)==0x96ba);
    assert((original[0xb36] | original[0xb37]<<8)==1); /* captured cartridge up-state */
    for (unsigned logic=0; logic<2; ++logic) {
      memcpy(active,original,sizeof(active)); SmRoomsSetRom(rom,0x300000);
      put(0xaf6,384); put(0xafa,18);
      for (unsigned frame=0; frame<60; ++frame) assert(SmRoomsFrame(active,true,213,logic!=0,true));
      assert(SmRoomsUpwardDoorCarry(active));
      put(0xb36,2); assert(!SmRoomsUpwardDoorCarry(active));
      put(0xb36,0); assert(!SmRoomsUpwardDoorCarry(active)); put(0xb36,1);
      put(0xaf6,16); assert(!SmRoomsUpwardDoorCarry(active)); put(0xaf6,384);
      put(0x998,9); assert(!SmRoomsUpwardDoorCarry(active));
    }
    puts("up-door native carry requires an open pair, upward movement, aligned body and gameplay, On and Off");
  }
  SmRoomsSetRom(NULL,0); free(rom); return 0;
}
