#include "sm_rooms.h"
#include "sm_room_collision.h"
#include "sm_room_runtime.h"
#include "sm_room_preview.h"
#include "sm_shot_view.h"
#include "sm_door.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static uint8_t active[0x20000], original[0x20000];
static uint8_t *cartridge;
static uint8_t Read(void *unused,uint32_t address) {
  (void)unused;unsigned offset=((address>>16)&127)*32768u+(address&32767);
  return (address&0x8000) && offset<0x300000 ? cartridge[offset] : 255;
}
/* Owned-ROM provider for the active endpoint. The live game uses its actual
 * active bus; this probe runs that endpoint's native PLMs in a private VM. */
static bool root_door(unsigned x,unsigned y,unsigned direction,bool closing) {
  static uint16_t vram[0x8000];
  const SmWorldRoom *room=SmRoomPreviewRoom((uint16_t)SmShotWord(active,0x79b));
  SmRoomRuntime *r=SmRoomRuntimeCreateConfigured(Read,NULL,room,(uint16_t)SmShotWord(active,0x7bb),0,active,false);
  assert(r && SmRoomRuntimeCapture(r,active,vram));
  bool ok=closing ? SmRoomRuntimeDoorClose(r,x,y,direction) : SmRoomRuntimeDoorOpen(r,x,y,direction);
  for (unsigned i=0; ok && i<64; ++i) ok=SmRoomRuntimeStepDoors(r);
  ok=ok && SmRoomRuntimePromoteDoors(r,active);SmRoomRuntimeDestroy(r);return ok;
}
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
static void world_shots(const uint8_t *rom) {
  if (SmShotWord(original,0x79b)!=0x91f8) return;
  for (unsigned logic=0; logic<2; ++logic) for (unsigned mode=0; mode<2; ++mode) {
    memcpy(active,original,sizeof(active));SmRoomsSetRom(rom,0x300000);SmRoomsSetDoorEvent(root_door);
    for (unsigned i=0; i<60; ++i) assert(SmRoomsFrame(active,true,213,logic!=0,mode!=0));
    unsigned cap=0x10002+2*(22*144+142);
    active[cap+1]=(uint8_t)((active[cap+1]&15)|0xc0);
    for (unsigned i=0; i<60; ++i) assert(SmRoomsFrame(active,true,213,logic!=0,mode!=0));
    put(0xdde,0);put(0xc18,0x8000);put(0xc40,0x8123);put(0xb64,2321);put(0xb78,392);
    put(0xbb4,4);put(0xbc8,4);put(0x1a,0);put(0x1e,0);put(0x26,0);put(0x28,0);put(0x1c,2324);put(0x18,2321);
    uint16_t index=0xffff;
    assert(SmRoomsShotBlock(active,true,&index)==1 && SmShotWord(active,0x28)==65535);
    assert(SmShotWord(active,0xb64)==2321 && SmShotWord(active,0xb78)==392);
    size_t bound=SmRoomsStateBound();void *saved=malloc(bound),*a=malloc(bound),*b=malloc(bound);assert(saved && a && b);
    size_t bytes=SmRoomsSave(saved,bound);assert(bytes);
    static uint8_t after_hit[0x20000];memcpy(after_hit,active,sizeof(active));
    for (unsigned i=0; i<60; ++i) assert(SmRoomsFrame(active,true,213,logic!=0,mode!=0));
    const SmRoomScene *scene=SmRoomsScene();bool open=false;
    for (unsigned i=0; i<scene->count; ++i) if (scene->rooms[i].placement.room==0x93aa)
      open=(SmShotWord(scene->rooms[i].ram,0x10002+2*(6*32+1))>>12)==4 ||
          (SmShotWord(scene->rooms[i].ram,0x10002+2*(6*32+1))>>12)==0;
    assert(open);size_t na=SmRoomsSave(a,bound);assert(na);
    assert(SmRoomsLoad(saved,bytes));memcpy(active,after_hit,sizeof(active));
    for (unsigned i=0; i<60; ++i) assert(SmRoomsFrame(active,true,213,logic!=0,mode!=0));
    size_t nb=SmRoomsSave(b,bound);assert(na==nb && !memcmp(a,b,na));
    free(saved);free(a);free(b);
  }
  SmRoomsSetDoorEvent(NULL);memcpy(active,original,sizeof(active));
  puts("inactive-face native beam hit, player slot ownership and exact mid-hit restore pass On/Off and Native/Continuous");
}
static void uncapped_entrance(const uint8_t *rom) {
  if (SmShotWord(original,0x79b)!=0x91f8) return;
  for (unsigned d=0; d<8; ++d) {
    unsigned header=Read(NULL,0x8fe68a+2*d) | Read(NULL,0x8fe68b+2*d)<<8;
    assert(SmDoorHasArrivalCap(d)==(header!=0));
  }
  memcpy(active,original,sizeof(active));SmRoomsSetRom(rom,0x300000);
  put(0x915,1024);put(0xafa,1150);
  for (unsigned i=0; i<60; ++i) assert(SmRoomsFrame(active,true,213,false,true));
  assert(SmRoomsDoorPhase(0x8922)==SM_DOOR_OPEN);
  const SmRoomScene *scene=SmRoomsScene();const uint8_t *ram=NULL;
  for (unsigned i=0; i<scene->count; ++i) if (scene->rooms[i].placement.room==0x95d4) ram=scene->rooms[i].ram;
  assert(ram);unsigned corner=SmShotWord(ram,0x10002);
  unsigned cap=0x10002+2*(70*144+142);
  active[cap+1]&=15; /* open only the real source cap */
  for (unsigned i=0; i<60; ++i) assert(SmRoomsFrame(active,true,213,false,true));
  scene=SmRoomsScene();ram=NULL;
  for (unsigned i=0; i<scene->count; ++i) if (scene->rooms[i].placement.room==0x95d4) ram=scene->rooms[i].ram;
  assert(ram && SmShotWord(ram,0x10002)==corner);
  for (unsigned i=0; i<40; ++i)
    assert(SmShotWord(ram,0x1c87+2*i)!=0 || !SmDoorHeader(SmShotWord(ram,0x1c37+2*i)));
  memcpy(active,original,sizeof(active));
  puts("uncapped native arrival keeps its geometric link and never inserts a corner bubble");
}
static const uint8_t *hall(void) {
  const SmRoomScene *scene=SmRoomsScene();
  for (unsigned i=0; i<scene->count; ++i) if (scene->rooms[i].placement.room==0x93aa) return scene->rooms[i].ram;
  assert(0 && "resident hall missing");return NULL;
}
static void beam(int x,int y) {
  memset(active+SM_SHOT_POOL,0,SM_SHOT_POOL_BYTES);
  put(0xb64,x);put(0xb78,y);put(0xbb4,4);put(0xbc8,4);
  put(0xc18,0x8000);put(0xc2c,2);put(0xc40,0x8123);put(0xcce,1);
}
static void world_combat(const uint8_t *rom) {
  if (SmShotWord(original,0x79b)!=0x91f8) return;
  int target_x=0,target_y=0;
  for (unsigned run=0; run<2; ++run) {
    bool logic=run==0;
    memcpy(active,original,sizeof(active));memset(active+SM_SHOT_POOL,0,SM_SHOT_POOL_BYTES);
    SmRoomsSetRom(rom,0x300000);SmRoomsSetDoorEvent(root_door);
    for (unsigned i=0; i<60; ++i) assert(SmRoomsFrame(active,true,213,logic,true));
    unsigned cap=0x10002+2*(22*144+142);
    active[cap+1]=(uint8_t)((active[cap+1]&15)|0xc0);
    for (unsigned i=0; i<60; ++i) assert(SmRoomsFrame(active,true,213,logic,true));
    const uint8_t *ram=hall();unsigned enemy=SmShotWord(ram,0x17ac),health=0;
    if (logic) {
      assert(enemy<2048 && !(enemy&63));enemy+=0xf78;health=SmShotWord(ram,enemy+20);
      target_x=(int)SmShotWord(ram,enemy+2)+2304;target_y=(int)SmShotWord(ram,enemy+6)+256;
      assert(health>2);
    }
    beam(target_x,target_y);
    uint8_t pool[SM_SHOT_POOL_BYTES];memcpy(pool,active+SM_SHOT_POOL,sizeof(pool));
    assert(SmRoomsFrame(active,true,213,logic,true));
    assert(!memcmp(pool,active+SM_SHOT_POOL,sizeof(pool)));
    if (logic) assert(SmShotWord(hall(),enemy+20)==health);
    memset(active+SM_SHOT_POOL,0,SM_SHOT_POOL_BYTES);active[cap+1]&=15;
    for (unsigned i=0; i<60; ++i) assert(SmRoomsFrame(active,true,213,logic,true));
    ram=hall();
    if (logic) {
      enemy=SmShotWord(ram,0x17ac);assert(enemy<2048);enemy+=0xf78;
      target_x=(int)SmShotWord(ram,enemy+2)+2304;target_y=(int)SmShotWord(ram,enemy+6)+256;
      health=SmShotWord(ram,enemy+20);assert(health>2);
    }
    beam(target_x,target_y);
    static uint8_t before_hit[sizeof(active)],after_hit[sizeof(active)];memcpy(before_hit,active,sizeof(active));
    size_t bound=SmRoomsStateBound();void *saved=malloc(bound),*end=malloc(bound),*again=malloc(bound);assert(saved && end && again);
    size_t bytes=SmRoomsSave(saved,bound);assert(bytes);
    assert(SmRoomsFrame(active,true,213,logic,true));
    if (logic) {
      if (!(SmShotWord(active,0xc04)&16)) {
        const uint8_t *r=hall();
        fprintf(stderr,"world hit no impact: target %d,%d enemy %04X props %04X map %04X inv %u health %u source %04X bts %02X arrival %u\n",
            target_x,target_y,SmShotWord(r,enemy),SmShotWord(r,enemy+14),SmShotWord(r,enemy+22),SmShotWord(r,enemy+40),
            SmShotWord(r,enemy+20),SmShotWord(active,cap),active[0x16402+143+22*144],SmRoomsDoorPhase(0x893a));
        for (unsigned k=0; k<32; ++k) if (SmShotWord(r,0xf78+64*k))
          fprintf(stderr,"enemy %u id%04X pos%u,%u props%04X map%04X active%04X\n",k,SmShotWord(r,0xf78+64*k),
              SmShotWord(r,0xf7a+64*k),SmShotWord(r,0xf7e +64*k),SmShotWord(r,0xf86+64*k),SmShotWord(r,0xf8e +64*k),SmShotWord(r,0x17ac+2*k));
      }
      assert(SmShotWord(active,0xc04)&16);
      assert(SmShotWord(hall(),enemy) && SmShotWord(hall(),enemy+20)<health);
    } else assert(!memcmp(before_hit+SM_SHOT_POOL,active+SM_SHOT_POOL,SM_SHOT_POOL_BYTES));
    memcpy(after_hit,active,sizeof(active));size_t na=SmRoomsSave(end,bound);assert(na);
    assert(SmRoomsLoad(saved,bytes));memcpy(active,before_hit,sizeof(active));
    assert(SmRoomsFrame(active,true,213,logic,true));size_t nb=SmRoomsSave(again,bound);
    assert(nb==na && !memcmp(end,again,na) && !memcmp(active,after_hit,sizeof(active)));
    free(saved);free(end);free(again);
  }
  SmRoomsSetDoorEvent(NULL);memcpy(active,original,sizeof(active));
  puts("world enemy beam hits require an open native connection; Off, closed caps and exact hit restore pass");
}
static const SmRoomImage *image(unsigned room) {
  const SmRoomScene *scene=SmRoomsScene();
  for (unsigned i=0; i<scene->count; ++i) if (scene->rooms[i].placement.room==room) return &scene->rooms[i];
  assert(0 && "handoff resident missing");return NULL;
}
static void image_vram(uint16_t *out,const SmRoomImage *in) {
  for (unsigned i=0; i<0x8000; ++i) out[i]=(uint16_t)SmShotWord(in->vram,2*i);
}
static void neighbor_pickups(const uint8_t *rom) {
  if (SmShotWord(original,0x79b)!=0x91f8) return;
  static uint8_t previous[0x20000],checkpoint[0x20000],finished[0x20000];
  static uint16_t vram[0x8000],previous_vram[0x8000];
  for (unsigned mode=0; mode<3; ++mode) {
    bool logic=mode!=1;
    memcpy(active,original,sizeof(active));SmRoomsSetRom(rom,0x300000);SmRoomsSetDoorEvent(root_door);
    for (unsigned i=0; i<60; ++i) assert(SmRoomsFrame(active,true,213,logic,true));
    const SmRoomImage *r=image(0x93aa);SmRoomPlacement entry=r->placement;
    memcpy(previous,active,sizeof(active));memcpy(active,r->ram,sizeof(active));image_vram(vram,r);
    assert(SmRoomsHandoff(previous,previous_vram,active,vram,&entry,0));
    put(0x998,8);put(0xaf6,40);put(0xafa,144);
    assert(SmRoomsFrame(active,true,213,logic,true));assert(root_door(1,6,1,false));
    for (unsigned i=0; i<60; ++i) assert(SmRoomsFrame(active,true,213,logic,true));
    /* Seed a native small-energy drop immediately before departure. Only
     * native pickup execution may consume it or change the player's energy. */
    unsigned slot=34;
    const unsigned at[]={0x1997,0x1a03,0x1b47,0x1b8f,0x1bb3,0x1bd7,0x1aff,0x1b23,0x1a4b,0x1a93,0xf410};
    const unsigned value[]={0xf337,0xefe0,0xed8d,0x7fff,0x1010,0x3000,2,400,1,144,0xffff};
    for (unsigned i=0; i<sizeof(at)/sizeof(*at); ++i) put(at[i]+slot,(int)value[i]);
    put(0x198d,0x8000);
    memcpy(previous,active,sizeof(active));memcpy(previous_vram,vram,sizeof(vram));
    r=image(0x91f8);entry=r->placement;memcpy(active,r->ram,sizeof(active));image_vram(vram,r);
    put(0x998,8);put(0xaf6,2250);put(0xb10,2250);put(0xafa,400);put(0xb14,400);
    put(0x9c2,100);put(0x9c4,500);put(0xd32,0xc4f0);
    assert(SmRoomsHandoff(previous,previous_vram,active,vram,&entry,1));
    if (mode==2) assert(root_door(142,22,0,true));
    /* Save at a completed frame boundary, with the drop still out of range. */
    assert(SmRoomsFrame(active,true,213,logic,true));assert(SmShotWord(active,0x9c2)==100);
    put(0xaf6,2303);put(0xb10,2303);
    size_t bound=SmRoomsStateBound();void *saved=malloc(bound),*end=malloc(bound),*again=malloc(bound);
    assert(saved && end && again);size_t bytes=SmRoomsSave(saved,bound);assert(bytes);
    memcpy(checkpoint,active,sizeof(active));size_t final_bytes=0;
    for (unsigned pass=0; pass<2; ++pass) {
      if (pass) { assert(SmRoomsLoad(saved,bytes));memcpy(active,checkpoint,sizeof(active)); }
      for (unsigned i=0; i<3; ++i) assert(SmRoomsFrame(active,true,213,logic,true));
      assert(SmShotWord(active,0x9c2)==(mode==0 ? 105 : 100));
      if (mode) assert(SmShotWord(image(0x93aa)->ram,0x1a03+slot)==0xefe0);
      size_t n=SmRoomsSave(pass ? again : end,bound);assert(n);
      if (!pass) { final_bytes=n;memcpy(finished,active,sizeof(active)); }
      else assert(n==final_bytes && !memcmp(end,again,n) && !memcmp(active,finished,sizeof(active)));
    }
    free(saved);free(end);free(again);
  }
  SmRoomsSetDoorEvent(NULL);memcpy(active,original,sizeof(active));
  puts("neighbor native pickups reward once through open door; closed door and logic Off retain drops; exact whole-world restore");
}
static void retained_projectile(const uint8_t *rom) {
  if (SmShotWord(original,0x79b)!=0x91f8) return;
  static uint8_t previous[0x20000],checkpoint[0x20000],finished[0x20000];
  static uint16_t vram[0x8000],previous_vram[0x8000];
  for (unsigned mode=0; mode<3; ++mode) {
    bool logic=mode==0;
    memcpy(active,original,sizeof(active));SmRoomsSetRom(rom,0x300000);SmRoomsSetDoorEvent(root_door);
    for (unsigned i=0; i<60; ++i) assert(SmRoomsFrame(active,true,213,logic,true));
    const SmRoomImage *r=image(0x93aa);SmRoomPlacement entry=r->placement;
    memcpy(previous,active,sizeof(active));memcpy(active,r->ram,sizeof(active));image_vram(vram,r);
    assert(SmRoomsHandoff(previous,previous_vram,active,vram,&entry,0));
    put(0x998,8);put(0xaf6,40);put(0xafa,139);put(0x9c2,500);put(0x9c4,500);put(0x18a8,0);put(0x18aa,0);
    assert(SmRoomsFrame(active,true,213,logic,true));
    assert(root_door(1,6,1,false));
    for (unsigned i=0; i<60; ++i) assert(SmRoomsFrame(active,true,213,logic,true));
    assert(SmRoomsScene()->open_rooms);
    /* A native moving projectile is a setup prerequisite. Its actual private
     * cartridge movement, terrain reaction and player damage are the result. */
    unsigned slot=34;
    const unsigned at[]={0x1997,0x1a03,0x1b47,0x1b6b,0x1b8f,0x1bb3,0x1bd7,0x1ab7,0x1adb,0x1a27,0x1a6f,0x1a4b,0x1a93,0x19bb,0xf380};
    const unsigned value[]={0x9db0,0x9e1e,0x9dda,0x8420,0x7fff,0x0404,0x14,0xfc00,0,0,0,40,122,0,0};
    for (unsigned i=0; i<sizeof(at)/sizeof(*at); ++i) put(at[i]+slot,(int)value[i]);
    put(0x198d,0x8000);
    if (mode==2) put(0x1a4b+slot,4);
    assert(SmRoomsFrame(active,true,213,logic,true));
    if (mode==2) { put(0x1a4b+slot,-4);assert(SmRoomsFrame(active,true,213,logic,true)); }
    memcpy(previous,active,sizeof(active));memcpy(previous_vram,vram,sizeof(vram));
    r=image(0x91f8);entry=r->placement;memcpy(active,r->ram,sizeof(active));image_vram(vram,r);
    put(0x998,8);put(0xaf6,2280);put(0xafa,400);put(0x9c2,500);put(0x9c4,500);put(0x18a8,0);put(0x18aa,0);
    assert(SmRoomsHandoff(previous,previous_vram,active,vram,&entry,1));
    assert(SmRoomsFrame(active,true,213,logic,true));r=image(0x93aa);
    size_t bound=SmRoomsStateBound();void *saved=malloc(bound),*end=malloc(bound),*again=malloc(bound);assert(saved && end && again);
    size_t bytes=SmRoomsSave(saved,bound);assert(bytes && SmRoomsValidate(saved,bytes));memcpy(checkpoint,active,sizeof(active));
    bool crossed=r->object_rooms[49]==0x91f8;unsigned steps=0;
    for (unsigned frame=0; frame<20; ++frame) {
      assert(SmRoomsFrame(active,true,213,logic,true));r=image(0x93aa);
      ++steps;
      if (r->object_rooms[49]==0x91f8) crossed=true;
      if (mode==1) assert((int16_t)SmShotWord(r->ram,0x1a4b+slot)==40 && !r->object_rooms[49]);
      if (SmShotWord(active,0x9c2)<500) break;
    }
    if (mode==1) assert(!crossed && SmShotWord(active,0x9c2)==500);
    else {
      if (!crossed || SmShotWord(active,0x9c2)>=500)
        fprintf(stderr,"retained mode%u crossed%u hp%u id%04x x%d y%d owner%04x\n",mode,crossed,SmShotWord(active,0x9c2),
            SmShotWord(r->ram,0x1997+slot),(int16_t)SmShotWord(r->ram,0x1a4b+slot),
            (int16_t)SmShotWord(r->ram,0x1a93+slot),r->object_rooms[49]);
      assert(crossed && SmShotWord(active,0x9c2)<500);
    }
    size_t final_bytes=SmRoomsSave(end,bound);assert(final_bytes);memcpy(finished,active,sizeof(active));
    assert(SmRoomsLoad(saved,bytes));memcpy(active,checkpoint,sizeof(active));
    for (unsigned i=0; i<steps; ++i) assert(SmRoomsFrame(active,true,213,logic,true));
    size_t repeated_bytes=SmRoomsSave(again,bound);
    assert(repeated_bytes==final_bytes && !memcmp(end,again,final_bytes) && !memcmp(active,finished,sizeof(active)));
    free(saved);free(end);free(again);
  }
  SmRoomsSetDoorEvent(NULL);memcpy(active,original,sizeof(active));
  puts("retained native projectile crosses into Root and damages Samus; Off freezes neighbors but preserves already-crossed objects; exact midflight restore");
}
int main(int argc,char **argv) {
  if (argc!=3 && argc!=4) return 2;
  samples();
  uint8_t *rom=malloc(0x300000); assert(rom);
  FILE *f=fopen(argv[1],"rb"); assert(f); assert(fread(rom,1,0x300000,f)==0x300000); fclose(f);
  cartridge=rom;
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
      SmEprojTile tile;
      assert(SmRoomsEprojTile(active,active,0,0,143,24,&tile) && !(tile.tile>>12));
      assert(SmRoomsEprojTile(active,active,0,0,145,24,&tile) && (tile.tile>>12)<8);
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
      assert(SmRoomsEprojTile(active,active,0,0,145,24,&tile) && (tile.tile>>12)>=8);
      memcpy(active,original,sizeof(active));
      puts("open portal samples, exact center-crossing ownership and closed opposing cap collision pass");
    }
    printf("logic %s: %u resident rooms, 60 native ticks, exact whole-manager save/load continuation; active RAM unchanged\n",logic ? "On" : "Off",SmRoomsScene()->count);
    free(saved); free(a); free(b);
  }
  world_shots(rom);
  uncapped_entrance(rom);
  world_combat(rom);
  neighbor_pickups(rom);
  retained_projectile(rom);
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
