#include "sm_room_runtime.h"
#include "sm_shot_view.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static uint8_t *rom, active[0x20000], before[0x20000];
static uint8_t Read(void *ctx,uint32_t a) {
  (void)ctx; unsigned at=((a>>16)&127)*32768u+(a&32767);
  return (a&0x8000) && at<0x300000 ? rom[at] : 255;
}
static void power_bombs(SmRoomRuntime *r) {
  const uint8_t *ram=SmRoomRuntimeRam(r),*graphics=SmRoomRuntimeVram(r);
  if (SmShotWord(ram,0x79b)!=0x9fe5) return;
  static uint8_t seed[0x20000],view[0x20000],input[0x20000];static uint16_t vram[0x8000];
  size_t size=SmRoomRuntimeStateSize();void *original=malloc(size),*fixture=malloc(size),*end=malloc(size),*again=malloc(size);
  assert(original && fixture && end && again && SmRoomRuntimeSave(r,original,size));
  uint8_t actor[64];memcpy(actor,ram+0xf78,sizeof(actor));assert(SmShotWord(actor,0)==0xe87f);
  memcpy(seed,ram,sizeof(seed));memset(seed+0xf78,0,2048);memset(seed+0x1997,0,36);
  for (unsigned slot=0; slot<32; slot+=31) {
    unsigned at=0xf78+64*slot;memcpy(seed+at,actor,sizeof(actor));
    SmShotPut(seed,at+2,64);SmShotPut(seed,at+6,64);SmShotPut(seed,at+20,1000);SmShotPut(seed,at+40,0);
    SmShotPut(seed,at+14,(int)SmShotWord(seed,at+14)&~0x4c00);
  }
  for (unsigned i=0; i<0x8000; ++i) vram[i]=(uint16_t)(graphics[2*i] | graphics[2*i+1]<<8);
  assert(SmRoomRuntimeCapture(r,seed,vram) && SmRoomRuntimeSave(r,fixture,size));
  int ox=768,oy=-256;memcpy(view,active,sizeof(view));
  SmShotPut(view,0xce2,64+ox);SmShotPut(view,0xce4,64+oy);SmShotPut(view,0xcea,64<<8);memcpy(input,view,sizeof(input));
  SmRoomRuntimeSetMasks(r,UINT32_MAX,(1u<<18)-1,0,0);
  assert(SmRoomRuntimePowerBomb(r,view,ox,oy) && SmRoomRuntimeSave(r,again,size));
  assert(!memcmp(fixture,again,size) && !memcmp(view,input,sizeof(view)));
  for (unsigned edge=0; edge<3; ++edge) {
    assert(SmRoomRuntimeLoad(r,fixture,size));SmRoomRuntimeSetMasks(r,UINT32_MAX,(1u<<18)-1,1u<<31,0);
    memcpy(view,input,sizeof(view));
    if (edge==1) SmShotPut(view,0xce2,64+ox+64);
    if (edge==2) SmShotPut(view,0xce4,64+oy+48);
    uint8_t untouched[0x20000];memcpy(untouched,view,sizeof(untouched));
    assert(SmRoomRuntimePowerBomb(r,view,ox,oy));
    assert(!memcmp(view,untouched,sizeof(view)) && SmShotWord(ram,0xf78)==0xe87f && SmShotWord(ram,0xf8c)==1000);
    unsigned target=0xf78+31*64;
    assert(SmShotWord(ram,target)==0xe87f && SmShotWord(ram,target+20)==(edge ? 1000 : 600));
    if (!edge) {
      assert(SmShotWord(ram,target+40)==48 && (SmShotWord(ram,target+14)&0x800));
      assert(SmRoomRuntimeSave(r,end,size));
      assert(SmRoomRuntimePowerBomb(r,view,ox,oy) && SmShotWord(ram,target+20)==600);
      assert(SmRoomRuntimeLoad(r,fixture,size));SmRoomRuntimeSetMasks(r,UINT32_MAX,(1u<<18)-1,1u<<31,0);
      assert(SmRoomRuntimePowerBomb(r,view,ox,oy) && SmRoomRuntimeSave(r,again,size) && !memcmp(end,again,size));
    }
  }
  assert(SmRoomRuntimeLoad(r,fixture,size));memcpy(seed,ram,sizeof(seed));
  unsigned target=0xf78+31*64;SmShotPut(seed,target+20,50);
  assert(SmRoomRuntimeCapture(r,seed,vram));SmRoomRuntimeSetMasks(r,UINT32_MAX,(1u<<18)-1,1u<<31,0);
  unsigned killed=SmShotWord(ram,0xe50);memcpy(view,input,sizeof(view));
  assert(SmRoomRuntimePowerBomb(r,view,ox,oy));
  assert(!SmShotWord(ram,target) && SmShotWord(ram,0xe50)==killed+1 && SmShotWord(ram,0xf78)==0xe87f);
  assert(SmShotWord(ram,0x1997+34)); /* native death explosion, not a host-created reward */
  assert(SmRoomRuntimeLoad(r,original,size));free(original);free(fixture);free(end);free(again);
  puts("native power bombs translate ellipse, respect masks without freeing slots, deal400 once, spawn native death effect, and restore exactly");
}
static void power_bomb_projectiles(SmRoomRuntime *r) {
  const uint8_t *ram=SmRoomRuntimeRam(r),*graphics=SmRoomRuntimeVram(r);
  if (SmShotWord(ram,0x79b)!=0x93aa) return;
  static uint8_t seed[0x20000],view[0x20000],input[0x20000];static uint16_t vram[0x8000];
  size_t size=SmRoomRuntimeStateSize();void *original=malloc(size),*fixture=malloc(size),*end=malloc(size),*again=malloc(size);
  assert(original && fixture && end && again && SmRoomRuntimeSave(r,original,size));
  memcpy(seed,ram,sizeof(seed));memset(seed+0x1997,0,36);SmShotPut(seed,0xa66,7);
  SmShotPut(seed,0xce2,900);SmShotPut(seed,0xce4,1000);SmShotPut(seed,0xcea,0);
  unsigned slot=34,id=0x8e5e;assert((Read(NULL,0x860002u+id) | Read(NULL,0x860003u+id)<<8)==0x8dff);
  const unsigned at[]={0x1997,0x1a03,0x1b47,0x1b8f,0x1bb3,0x1bd7,0x1ab7,0x1adb,0x1a27,0x1a6f,0x1a4b,0x1a93};
  const unsigned value[]={0x8e5e,0x8dff,0x8ca4,0x7fff,0x0808,0x1080,0,0,0,0,64,64};
  for (unsigned i=0; i<sizeof(at)/sizeof(*at); ++i) SmShotPut(seed,at[i]+slot,(int)value[i]);
  SmShotPut(seed,0x198d,0x8000);
  for (unsigned i=0; i<0x8000; ++i) vram[i]=(uint16_t)(graphics[2*i] | graphics[2*i+1]<<8);
  assert(SmRoomRuntimeCapture(r,seed,vram) && SmRoomRuntimeSave(r,fixture,size));
  int ox=768,oy=-256;memcpy(input,active,sizeof(input));
  SmShotPut(input,0xa66,3);SmShotPut(input,0xce2,64+ox);SmShotPut(input,0xce4,64+oy);SmShotPut(input,0xcea,64<<8);
  for (unsigned eligible=0; eligible<2; ++eligible) {
    assert(SmRoomRuntimeLoad(r,fixture,size));memcpy(view,input,sizeof(view));
    SmRoomRuntimeSetMasks(r,0,1u<<17,0,eligible ? 1u<<17 : 0);SmRoomRuntimeSetPlayer(r,view,ox,oy,NULL);
    assert(SmRoomRuntimeStepVisible(r,64,64,0,0,213));
    assert(SmShotWord(ram,0x1997+slot)==(eligible ? 0 : id));
    assert(SmShotWord(view,0xa66)==(eligible ? 0 : 3));
    assert(SmShotWord(ram,0xa66)==7 && SmShotWord(ram,0xce2)==900 && SmShotWord(ram,0xce4)==1000 && !SmShotWord(ram,0xcea));
    assert(!memcmp(view+0xce2,input+0xce2,10) && SmRoomRuntimeSave(r,end,size));
    unsigned speed=SmShotWord(view,0xa66);
    assert(SmRoomRuntimeLoad(r,fixture,size));memcpy(view,input,sizeof(view));
    SmRoomRuntimeSetMasks(r,0,1u<<17,0,eligible ? 1u<<17 : 0);
    assert(SmRoomRuntimeStepVisible(r,64,64,0,0,213) && SmRoomRuntimeSave(r,again,size));
    assert(!memcmp(end,again,size) && SmShotWord(view,0xa66)==speed);
  }
  SmRoomRuntimeSetPlayer(r,NULL,0,0,NULL);assert(SmRoomRuntimeLoad(r,original,size));
  free(original);free(fixture);free(end);free(again);
  puts("native Draygon projectile PB response uses translated live blast, clears Root slowdown only when eligible, restores private fields and repeats exactly");
}
static void hit_shots(SmRoomRuntime *r) {
  static uint8_t view[0x20000],input[sizeof(view)],result[sizeof(view)];
  const uint8_t *ram=SmRoomRuntimeRam(r);unsigned slot=32;
  for (unsigned i=0; i<32; ++i) {
    unsigned at=0xf78+64*i;
    if (SmShotWord(ram,at) && SmShotWord(ram,at+20)>2 && SmShotWord(ram,at+22) &&
        !(SmShotWord(ram,at+14)&0x400) && !SmShotWord(ram,at+40)) { slot=i;break; }
  }
  assert(slot<32);unsigned at=0xf78+64*slot,health=SmShotWord(ram,at+20);
  size_t size=SmRoomRuntimeStateSize();void *saved=malloc(size),*end=malloc(size),*again=malloc(size);
  assert(saved && end && again && SmRoomRuntimeSave(r,saved,size));
  memcpy(view,active,sizeof(view));memset(view+SM_SHOT_POOL,0,SM_SHOT_POOL_BYTES);
  int ox=768,oy=-256;
  for (unsigned i=0; i<2; ++i) {
    SmShotPut(view,0xb64+2*i,(int)SmShotWord(ram,at+2)+ox);
    SmShotPut(view,0xb78+2*i,(int)SmShotWord(ram,at+6)+oy);
    SmShotPut(view,0xbb4+2*i,4);SmShotPut(view,0xbc8+2*i,4);
    SmShotPut(view,0xc18+2*i,0x8000);SmShotPut(view,0xc40+2*i,0x8123);
    SmShotPut(view,0xc2c+2*i,i ? 0x4000 : 2);
  }
  SmShotPut(view,0xcce,2);memcpy(input,view,sizeof(view));
  assert(SmRoomRuntimeHitShots(r,view,ox,oy,0));
  assert(SmRoomRuntimeSave(r,again,size) && !memcmp(saved,again,size) && !memcmp(view,input,sizeof(view)));
  if (!SmRoomRuntimeHitShots(r,view,ox,oy,1)) { fprintf(stderr,"hit: %s\n",SmRoomRuntimeError(r));abort(); }
  assert(SmShotWord(ram,at) && SmShotWord(ram,at+20)>0 && SmShotWord(ram,at+20)<health);
  assert(SmShotWord(view,0xc04)&16); /* cartridge consumed the allowed beam */
  for (unsigned a=0; a<18; ++a)
    assert(!memcmp(view+SM_SHOT_POOL+20*a+2,input+SM_SHOT_POOL+20*a+2,18));
  assert(SmShotWord(view,0xb64)==SmShotWord(input,0xb64) && SmShotWord(view,0xb78)==SmShotWord(input,0xb78));
  assert(!memcmp(view,input,SM_SHOT_POOL) && !memcmp(view+0xcd4,input+0xcd4,sizeof(view)-0xcd4));
  memcpy(result,view,sizeof(view));assert(SmRoomRuntimeSave(r,end,size));
  assert(SmRoomRuntimeHitShots(r,view,ox,oy,1)); /* same impact cannot hit twice */
  assert(SmRoomRuntimeSave(r,again,size) && !memcmp(end,again,size) && !memcmp(view,result,sizeof(view)));
  assert(SmRoomRuntimeLoad(r,saved,size));memcpy(view,input,sizeof(view));
  assert(SmRoomRuntimeHitShots(r,view,ox,oy,1) && SmRoomRuntimeSave(r,again,size));
  assert(!memcmp(end,again,size) && !memcmp(view,result,sizeof(view)));
  assert(SmRoomRuntimeLoad(r,saved,size));free(saved);free(end);free(again);
  puts("native remote enemy hit, masked slot isolation, no duplicate impact, exact restore and world coordinates pass");
}
static void touch(SmRoomRuntime *r) {
  static uint8_t view[0x20000],input[sizeof(view)],result[sizeof(view)];
  const uint8_t *ram=SmRoomRuntimeRam(r);unsigned index=SmShotWord(ram,0x17ac);assert(index<2048);
  unsigned enemy=0xf78+index;int ox=768,oy=-256;
  size_t size=SmRoomRuntimeStateSize();void *saved=malloc(size),*end=malloc(size),*again=malloc(size);
  assert(saved && end && again && SmRoomRuntimeSave(r,saved,size));
  memcpy(view,active,sizeof(view));
  int x=(int)SmShotWord(ram,enemy+2)+ox,y=(int)SmShotWord(ram,enemy+6)+oy;
  SmShotPut(view,0xaf6,x);SmShotPut(view,0xafa,y);SmShotPut(view,0xb10,x);SmShotPut(view,0xb14,y);
  SmShotPut(view,0xafe,5);SmShotPut(view,0xb00,21);SmShotPut(view,0x9c2,1999);SmShotPut(view,0x9c4,1999);
  SmShotPut(view,0x18a8,0);SmShotPut(view,0x18aa,0);SmShotPut(view,0xa6e,0);memcpy(input,view,sizeof(view));
  SmShotPut(view,0xaf6,x+1024);memcpy(result,view,sizeof(view));
  assert(SmRoomRuntimeTouch(r,view,ox,oy) && !memcmp(view,result,sizeof(view)));
  assert(SmRoomRuntimeLoad(r,saved,size));memcpy(view,input,sizeof(view));
  if (!SmRoomRuntimeTouch(r,view,ox,oy)) { fprintf(stderr,"touch: %s\n",SmRoomRuntimeError(r));abort(); }
  assert(SmShotWord(view,0x9c2)<1999 && SmShotWord(view,0x18a8)==96 && SmShotWord(view,0x18aa)==5);
  assert(SmShotWord(view,0xaf6)==(unsigned)(x&65535) && SmShotWord(view,0xafa)==(unsigned)(y&65535));
  assert(!memcmp(view,input,0x9a2) && !memcmp(view+0xb64,input+0xb64,0x18a8-0xb64));
  assert(!memcmp(view+0x18ac,input+0x18ac,sizeof(view)-0x18ac));
  memcpy(result,view,sizeof(view));assert(SmRoomRuntimeSave(r,end,size));
  assert(SmRoomRuntimeTouch(r,view,ox,oy) && !memcmp(view,result,sizeof(view)));
  assert(SmRoomRuntimeLoad(r,saved,size));memcpy(view,input,sizeof(view));
  assert(SmRoomRuntimeTouch(r,view,ox,oy) && SmRoomRuntimeSave(r,again,size));
  assert(!memcmp(end,again,size) && !memcmp(view,result,sizeof(view)));
  assert(SmRoomRuntimeLoad(r,saved,size));free(saved);free(end);free(again);
  puts("native remote actor contact commits damage/knockback once, preserves world body coordinates and restores exactly");
}
static void projectile_interactions(SmRoomRuntime *r) {
  static uint8_t seed[0x20000],view[sizeof(seed)],input[sizeof(seed)],result[sizeof(seed)];
  static uint16_t vram[0x8000];
  const uint8_t *ram=SmRoomRuntimeRam(r),*graphics=SmRoomRuntimeVram(r);
  size_t size=SmRoomRuntimeStateSize();void *original=malloc(size),*fixture=malloc(size),*end=malloc(size),*again=malloc(size);
  assert(original && fixture && end && again && SmRoomRuntimeSave(r,original,size));
  memcpy(seed,ram,sizeof(seed));memset(seed+0x1997,0,36);memset(seed+0xf380,0,36);
  for (unsigned i=0; i<0x8000; ++i) vram[i]=(uint16_t)(graphics[2*i] | graphics[2*i+1]<<8);
  /* A real shootable Chozo orb definition, in the last native slot. Position
   * it directly to isolate collision responses from its encounter's spawn AI. */
  unsigned slot=34,id=0xad5e;
  unsigned radius=Read(NULL,0x860006+id) | Read(NULL,0x860007+id)<<8;
  unsigned properties=Read(NULL,0x860008+id) | Read(NULL,0x860009+id)<<8;
  unsigned shot_list=Read(NULL,0x86000c+id) | Read(NULL,0x86000d+id)<<8;
  assert((properties&0x8000) && (properties&0xfff)==8 && shot_list);
  SmShotPut(seed,0x1997+slot,(int)id);SmShotPut(seed,0x1bb3+slot,(int)radius);
  SmShotPut(seed,0x1bd7+slot,(int)properties);SmShotPut(seed,0x1a4b+slot,64);SmShotPut(seed,0x1a93+slot,64);
  assert(SmRoomRuntimeCapture(r,seed,vram) && SmRoomRuntimeSave(r,fixture,size));
  int ox=768,oy=-256;
  for (unsigned plasma=0; plasma<2; ++plasma) {
    assert(SmRoomRuntimeLoad(r,fixture,size));memcpy(view,active,sizeof(view));
    memset(view+SM_SHOT_POOL,0,SM_SHOT_POOL_BYTES);
    for (unsigned i=0; i<2; ++i) {
      SmShotPut(view,0xb64+2*i,64+ox);SmShotPut(view,0xb78+2*i,64+oy);
      SmShotPut(view,0xc18+2*i,0x8000 | (plasma ? 8 : 0));SmShotPut(view,0xc40+2*i,0x8123);
    }
    SmShotPut(view,0xcce,2);memcpy(input,view,sizeof(view));
    assert(SmRoomRuntimeProjectiles(r,view,ox,oy,0,false));
    assert(SmRoomRuntimeSave(r,again,size) && !memcmp(fixture,again,size) && !memcmp(view,input,sizeof(view)));
    if (!SmRoomRuntimeProjectiles(r,view,ox,oy,1,false)) {
      fprintf(stderr,"projectile response: %s\n",SmRoomRuntimeError(r));abort();
    }
    assert(((SmShotWord(view,0xc04)&16)!=0)==!plasma);
    assert(SmShotWord(ram,0x1b47+slot)==shot_list && !(SmShotWord(ram,0x1bd7+slot)&0x8000));
    assert(SmShotWord(ram,0x1bfb+slot)==(0x8000u | (plasma ? 8 : 0)));
    for (unsigned a=0; a<18; ++a)
      assert(!memcmp(view+SM_SHOT_POOL+20*a+2,input+SM_SHOT_POOL+20*a+2,18));
    assert(!memcmp(view,input,SM_SHOT_POOL) && !memcmp(view+0xcd4,input+0xcd4,sizeof(view)-0xcd4));
    memcpy(result,view,sizeof(view));assert(SmRoomRuntimeSave(r,end,size));
    assert(SmRoomRuntimeProjectiles(r,view,ox,oy,1,false) && !memcmp(view,result,sizeof(view)));
    assert(SmRoomRuntimeSave(r,again,size) && !memcmp(end,again,size));
    assert(SmRoomRuntimeLoad(r,fixture,size));memcpy(view,input,sizeof(view));
    assert(SmRoomRuntimeProjectiles(r,view,ox,oy,1,false) && SmRoomRuntimeSave(r,again,size));
    assert(!memcmp(end,again,size) && !memcmp(result,view,sizeof(view)));
  }
  for (unsigned suit=0; suit<3; ++suit) {
    assert(SmRoomRuntimeLoad(r,fixture,size));memcpy(view,active,sizeof(view));
    SmShotPut(view,0xaf6,64+ox);SmShotPut(view,0xafa,64+oy);SmShotPut(view,0xb10,64+ox);SmShotPut(view,0xb14,64+oy);
    SmShotPut(view,0xafe,5);SmShotPut(view,0xb00,21);SmShotPut(view,0x9c2,1999);SmShotPut(view,0x9c4,1999);
    SmShotPut(view,0x9a2,suit==2 ? 0x20 : suit);
    SmShotPut(view,0x18a8,0);SmShotPut(view,0x18aa,0);SmShotPut(view,0xa6e,0);memcpy(input,view,sizeof(view));
    assert(SmRoomRuntimeProjectiles(r,view,ox,oy,0,true));
    assert(SmShotWord(view,0x9c2)==1999u-(8u>>suit) && !SmShotWord(ram,0x1997+slot));
    assert(SmShotWord(view,0x18a8)==96 && SmShotWord(view,0x18aa)==5);
    assert(SmShotWord(view,0xaf6)==64u+ox && SmShotWord(view,0xafa)==((64u+oy)&65535));
    assert(!memcmp(view,input,0x9a2) && !memcmp(view+0xb64,input+0xb64,0x18a8-0xb64));
    assert(!memcmp(view+0x18ac,input+0x18ac,sizeof(view)-0x18ac));
    memcpy(result,view,sizeof(view));assert(SmRoomRuntimeSave(r,end,size));
    assert(SmRoomRuntimeProjectiles(r,view,ox,oy,0,true) && !memcmp(view,result,sizeof(view)));
    assert(SmRoomRuntimeSave(r,again,size) && !memcmp(end,again,size));
    assert(SmRoomRuntimeLoad(r,fixture,size));memcpy(view,input,sizeof(view));
    assert(SmRoomRuntimeProjectiles(r,view,ox,oy,0,true) && SmRoomRuntimeSave(r,again,size));
    assert(!memcmp(end,again,size) && !memcmp(result,view,sizeof(view)));
  }
  assert(SmRoomRuntimeLoad(r,original,size));free(original);free(fixture);free(end);free(again);
  puts("native resident projectile contact/suit damage, shootable reactions/plasma, slot isolation and exact restore pass");
}
typedef struct EprojTerrainProbe { unsigned queries,crossings;bool native,wall; } EprojTerrainProbe;
static bool eproj_terrain(void *context,const uint8_t *ram,int x,int y,SmEprojTile *tile) {
  EprojTerrainProbe *probe=context;++probe->queries;
  unsigned w=SmShotWord(ram,0x7a5),h=SmShotWord(ram,0x7a7);
  bool outside=x<0 || y<0 || x>=(int)w || y>=(int)h;
  probe->crossings+=outside;
  unsigned block=outside ? 0 : (unsigned)y*w+(unsigned)x;
  *tile=(SmEprojTile){x,y,probe->native ? (uint16_t)SmShotWord(ram,0x10002+2*block) :
      (uint16_t)(probe->wall && outside ? 0x8000 : 0),probe->native ? ram[0x16402+block] : 0};
  return true;
}
static void projectile_terrain(SmRoomRuntime *r,SmRoomRuntime *twin) {
  static uint8_t seed[0x20000];static uint16_t vram[0x8000];
  const uint8_t *ram=SmRoomRuntimeRam(r),*graphics=SmRoomRuntimeVram(r);
  size_t size=SmRoomRuntimeStateSize();void *original=malloc(size),*fixture=malloc(size),*end=malloc(size),*again=malloc(size);
  assert(original && fixture && end && again && SmRoomRuntimeSave(r,original,size));
  memcpy(seed,ram,sizeof(seed));memset(seed+0xf78,0,2048);memset(seed+0x1997,0,36);
  SmShotPut(seed,0xe4c,0);SmShotPut(seed,0xe4e,0);SmShotPut(seed,0x198d,0x8000);
  unsigned w=SmShotWord(seed,0x7a5),h=SmShotWord(seed,0x7a7),slot=34;
  SmShotPut(seed,0x1997+slot,0x9db0);SmShotPut(seed,0x1a03+slot,0x9e1e);
  SmShotPut(seed,0x1b47+slot,0x9dda);SmShotPut(seed,0x1b8f+slot,0x7fff);SmShotPut(seed,0x1bb3+slot,0x0404);
  SmShotPut(seed,0x1a27+slot,0);SmShotPut(seed,0x1a6f+slot,0);
  for (unsigned i=0; i<0x8000; ++i) vram[i]=(uint16_t)(graphics[2*i] | graphics[2*i+1]<<8);
  for (unsigned wall=0; wall<2; ++wall) for (unsigned direction=0; direction<4; ++direction) {
    int x=direction==0 ? (int)w*16-2 : direction==1 ? 2 : 64;
    int y=direction==2 ? (int)h*16-2 : direction==3 ? 2 : 64;
    int dx=direction==0 ? 4 : direction==1 ? -4 : 0,dy=direction==2 ? 4 : direction==3 ? -4 : 0;
    for (unsigned i=0; i<w*h; ++i) { SmShotPut(seed,0x10002+2*i,0x8000);seed[0x16402+i]=0; }
    SmShotPut(seed,0x1a4b+slot,x);SmShotPut(seed,0x1a93+slot,y);
    SmShotPut(seed,0x1ab7+slot,dx*256);SmShotPut(seed,0x1adb+slot,dy*256);
    assert(SmRoomRuntimeCapture(r,seed,vram) && SmRoomRuntimeSave(r,fixture,size));
    EprojTerrainProbe probe={.wall=wall!=0};SmRoomRuntimeSetWorld(r,eproj_terrain,&probe);
    assert(SmRoomRuntimeStepVisible(r,64,64,0,0,213));
    assert(probe.crossings && SmShotWord(ram,0x10002)==0x8000 && ram[0x16402]==0);
    assert((SmShotWord(ram,0x1997+slot)!=0)==!wall);
    if (!wall) assert(SmShotWord(ram,0x1a4b+slot)==((unsigned)(x+dx)&65535) &&
        SmShotWord(ram,0x1a93+slot)==((unsigned)(y+dy)&65535));
    assert(SmRoomRuntimeSave(r,end,size) && SmRoomRuntimeLoad(r,fixture,size));
    assert(SmRoomRuntimeStepVisible(r,64,64,0,0,213) && SmRoomRuntimeSave(r,again,size) && !memcmp(end,again,size));
    SmRoomRuntimeSetWorld(r,NULL,NULL);
  }
  /* Compare the complete native moving-projectile pool against unmodified
   * cartridge collision for every slope shape/flip and movement direction. */
  for (unsigned shape=0; shape<32; ++shape) for (unsigned flip=0; flip<4; ++flip)
    for (unsigned direction=0; direction<4; ++direction) {
      for (unsigned i=0; i<w*h; ++i) { SmShotPut(seed,0x10002+2*i,0x1000);seed[0x16402+i]=(uint8_t)(shape+flip*64); }
      SmShotPut(seed,0x1a4b+slot,64);SmShotPut(seed,0x1a93+slot,64);
      SmShotPut(seed,0x1ab7+slot,direction==0 ? 1024 : direction==1 ? -1024 : 0);
      SmShotPut(seed,0x1adb+slot,direction==2 ? 1024 : direction==3 ? -1024 : 0);
      assert(SmRoomRuntimeCapture(r,seed,vram) && SmRoomRuntimeSave(r,fixture,size) && SmRoomRuntimeLoad(twin,fixture,size));
      EprojTerrainProbe probe={.native=true};SmRoomRuntimeSetWorld(r,eproj_terrain,&probe);
      assert(SmRoomRuntimeStepVisible(r,64,64,0,0,213) && SmRoomRuntimeStepVisible(twin,64,64,0,0,213));
      if (memcmp(ram+0x198d,SmRoomRuntimeRam(twin)+0x198d,0x296)) {
        fprintf(stderr,"foreign slope mismatch shape=%u flip=%u direction=%u\n",shape,flip,direction);abort();
      }
      assert(probe.queries && SmShotWord(ram,0x10002)==0x1000 && ram[0x16402]==shape+flip*64);
      SmRoomRuntimeSetWorld(r,NULL,NULL);
    }
  assert(SmRoomRuntimeLoad(r,original,size));free(original);free(fixture);free(end);free(again);
  puts("native projectiles cross four signed room boundaries, stop on foreign walls, restore exactly and match all native slope shapes/flips");
}
static void actor_blocks(SmRoomRuntime *r) {
  static uint8_t seed[0x20000],native[0x20000],foreign[0x20000],owner_before[0x20000];
  static uint16_t vram[0x8000];const uint8_t *ram=SmRoomRuntimeRam(r),*gfx=SmRoomRuntimeVram(r);
  size_t size=SmRoomRuntimeStateSize();void *original=malloc(size);assert(original && SmRoomRuntimeSave(r,original,size));
  memcpy(seed,ram,sizeof(seed));unsigned index=31*64,block=6*SmShotWord(seed,0x7a5)+6;
  for (unsigned i=0; i<0x8000; ++i) vram[i]=(uint16_t)(gfx[2*i] | gfx[2*i+1]<<8);
  for (unsigned shape=0; shape<34; ++shape) for (unsigned flip=0; flip<4; ++flip)
  for (unsigned direction=0; direction<4; ++direction) for (unsigned policy=0; policy<3; ++policy) {
    bool horizontal=direction<2,positive=!(direction&1);int dx=0,dy=0;
    if (horizontal) dx=positive ? -512 : 512;else dy=positive ? -256 : 256;
    SmShotPut(seed,0x10002+2*block,shape<32 ? 0x1000 : shape==32 ? 0 : 0x8000);
    seed[0x16402+block]=(uint8_t)((shape&31) | flip<<6);
    assert(SmRoomRuntimeCapture(r,seed,vram));memcpy(owner_before,ram,sizeof(owner_before));memcpy(native,seed,sizeof(native));
    memset(native,0,64);SmShotPut(native,0xe54,(int)index);SmShotPut(native,SM_ENEMY_POOL+index,0xe87f);
    int along=6*16+(positive ? -1 : 18),across=6*16+8,sign=positive ? 1 : -1;
    SmShotPut(native,0xf7a+index,horizontal ? along : across);SmShotPut(native,0xf7e +index,horizontal ? across : along);
    SmShotPut(native,0xf82+index,8);SmShotPut(native,0xf84+index,8);
    SmShotPut(native,0x12,0);SmShotPut(native,0x14,sign*2);SmShotPut(native,0x18,along+sign*2);
    SmShotPut(native,0x1a,along+sign*2+(positive ? 7 : -8));SmShotPut(native,0x1c,0);SmShotPut(native,0x1e,0);
    SmShotPut(native,0x20,policy==0 ? 0 : policy==1 ? 0x4000 : 0x8000);memcpy(foreign,native,sizeof(foreign));
    SmEnemyTranslate(foreign,index,-dx,-dy,horizontal);
    int expected=SmRoomRuntimeEnemyBlock(r,native,0,0,6,6,horizontal);
    int actual=SmRoomRuntimeEnemyBlock(r,foreign,dx,dy,6,6,horizontal);assert(expected>=0 && actual==expected);
    SmEnemyTranslate(native,index,-dx,-dy,horizontal);
    assert(!memcmp(native,foreign,64) && !memcmp(native+SM_ENEMY_POOL+index,foreign+SM_ENEMY_POOL+index,SM_ENEMY_BYTES));
    assert(SmShotWord(native,0x1e77)==SmShotWord(foreign,0x1e77));
    assert(!memcmp(owner_before,ram,64) && !memcmp(owner_before+SM_ENEMY_POOL,ram+SM_ENEMY_POOL,2048));
  }
  /* Native enemy-breakable terrain must change in its real room, preserving
   * the actor pool and every other terrain cell. */
  memset(seed+0x1c37,0,80);SmShotPut(seed,0x10002+2*block,0xa123);seed[0x16402+block]=15;
  assert(SmRoomRuntimeCapture(r,seed,vram));memcpy(foreign,seed,sizeof(foreign));
  SmShotPut(foreign,0xe54,(int)index);SmShotPut(foreign,SM_ENEMY_POOL+index,0xe87f);
  SmShotPut(foreign,0xf7a+index,6*16+8+512);SmShotPut(foreign,0xf7e +index,6*16+8);
  assert(SmRoomRuntimeEnemyBlock(r,foreign,-512,0,6,6,true)==0);
  assert(SmShotWord(ram,0x10002+2*block)==0x123 && SmShotWord(foreign,0x10002+2*block)==0xa123);
  assert(!memcmp(seed+SM_ENEMY_POOL,ram+SM_ENEMY_POOL,2048));
  for (unsigned b=0; b<SmShotWord(seed,0x7a5)*SmShotWord(seed,0x7a7); ++b)
    if (b!=block) assert(SmShotWord(seed,0x10002+2*b)==SmShotWord(ram,0x10002+2*b));
  assert(SmRoomRuntimeLoad(r,original,size));free(original);
  puts("actor foreign block reactions match native air/walls and all slopes/flips/policies in four directions; breakable terrain belongs to its room");
}
typedef struct ActorTerrain { SmRoomRuntime *owner;int x;unsigned samples,walls; } ActorTerrain;
static int actor_neighbor(void *context,uint8_t source[0x20000],bool horizontal,uint16_t *index) {
  ActorTerrain *t=context;int x,y;(void)index;assert(SmEnemySample(source,horizontal,&x,&y));
  if (x>=0 && x<16 && y>=0 && y<16) return -1;
  x-=t->x/16;if (x<0 || x>=16 || y<0 || y>=16) return 1;
  ++t->samples;int result=SmRoomRuntimeEnemyBlock(t->owner,source,-t->x,0,x,y,horizontal);
  if (horizontal && result==1) ++t->walls;
  return result;
}
static void actor_walk(SmRoomRuntime *r,SmRoomRuntime *owner) {
  static uint8_t seed[0x20000],terrain[0x20000];static uint16_t vram[0x8000];
  const uint8_t *ram=SmRoomRuntimeRam(r),*gfx=SmRoomRuntimeVram(r);assert(SmShotWord(ram,0xf78)==0xe87f);
  size_t size=SmRoomRuntimeStateSize();void *original=malloc(size),*other=malloc(size),*checkpoint=malloc(size),*owner_checkpoint=malloc(size),*end=malloc(size),*again=malloc(size);
  assert(original && other && checkpoint && owner_checkpoint && end && again);
  assert(SmRoomRuntimeSave(r,original,size) && SmRoomRuntimeSave(owner,other,size));
  for (unsigned direction=0; direction<2; ++direction) {
    assert(SmRoomRuntimeLoad(r,original,size));memcpy(seed,ram,sizeof(seed));
    for (unsigned i=1; i<32; ++i) SmShotPut(seed,0xf78+64*i,0);
    memset(seed+0x1997,0,36);memset(seed+0x10002,0,512);memset(seed+0x16402,0,256);
    for (unsigned x=0; x<16; ++x) SmShotPut(seed,0x10002+2*(9*16+x),0x8000);
    SmShotPut(seed,0xf7a,direction ? 1 : 255);SmShotPut(seed,0xf7e,136);
    SmShotPut(seed,0xf82,8);SmShotPut(seed,0xf84,8);SmShotPut(seed,0xfac,direction ? 0xb9c1 : 0xba24);SmShotPut(seed,0xfae,1000);
    for (unsigned i=0; i<0x8000; ++i) vram[i]=(uint16_t)(gfx[2*i] | gfx[2*i+1]<<8);
    assert(SmRoomRuntimeCapture(r,seed,vram));memcpy(terrain,seed,sizeof(terrain));
    for (unsigned y=0; y<9; ++y) SmShotPut(terrain,0x10002+2*(y*16+(direction ? 13 : 2)),0x8000);
    assert(SmRoomRuntimeCapture(owner,terrain,vram));ActorTerrain query={owner,direction ? -256 : 256,0,0};
    SmRoomRuntimeSetWorld(r,NULL,&query);SmRoomRuntimeSetEnemyWorld(r,actor_neighbor);
    for (unsigned i=0; i<8; ++i) assert(SmRoomRuntimeStepVisible(r,128,64,0,0,213));
    assert(SmRoomRuntimeSave(r,checkpoint,size) && SmRoomRuntimeSave(owner,owner_checkpoint,size));
    for (unsigned pass=0; pass<2; ++pass) {
      if (pass) { assert(SmRoomRuntimeLoad(r,checkpoint,size) && SmRoomRuntimeLoad(owner,owner_checkpoint,size));query.samples=query.walls=0; }
      bool crossed=false;
      for (unsigned i=0; i<160; ++i) {
        assert(SmRoomRuntimeStepVisible(r,128,64,0,0,213));int x=(int16_t)SmShotWord(ram,0xf7a);
        if (direction ? x<0 : x>=256) crossed=true;
        assert(direction ? x>=-24 : x<=280);
      }
      assert(crossed && query.samples && query.walls);
      assert(SmRoomRuntimeSave(r,pass ? again : end,size));
      if (pass) assert(!memcmp(end,again,size));
    }
    SmRoomRuntimeSetWorld(r,NULL,NULL);
  }
  assert(SmRoomRuntimeLoad(r,original,size) && SmRoomRuntimeLoad(owner,other,size));
  free(original);free(other);free(checkpoint);free(owner_checkpoint);free(end);free(again);
  puts("real native Beetom walking crosses both signed room boundaries, uses foreign floor/wall and restores exactly");
}
static void player_ai(SmRoomRuntime *r) {
  static uint8_t seed[0x20000],view[0x20000],checkpoint[0x20000],finished[0x20000];
  static uint16_t vram[0x8000];const uint8_t *ram=SmRoomRuntimeRam(r),*gfx=SmRoomRuntimeVram(r);
  size_t size=SmRoomRuntimeStateSize();void *original=malloc(size),*grab=malloc(size),*end=malloc(size),*again=malloc(size);
  assert(original && grab && end && again && SmRoomRuntimeSave(r,original,size));
  memcpy(seed,ram,sizeof(seed));for (unsigned i=1; i<32; ++i) SmShotPut(seed,0xf78+64*i,0);
  memset(seed+0x1997,0,36);assert(SmShotWord(seed,0xf78)==0xe87f);
  SmShotPut(seed,0xf7a,128);SmShotPut(seed,0xf7e,128);SmShotPut(seed,0xf82,8);SmShotPut(seed,0xf84,8);
  SmShotPut(seed,0xfa0,0);SmShotPut(seed,0x9c2,777);SmShotPut(seed,0x9c4,999);SmShotPut(seed,0xd32,0xc4f0);
  for (unsigned i=0; i<0x8000; ++i) vram[i]=(uint16_t)(gfx[2*i] | gfx[2*i+1]<<8);
  assert(SmRoomRuntimeCapture(r,seed,vram));SmRoomRuntimeSetMasks(r,1,0,1,0);
  assert(SmRoomRuntimeStepVisible(r,128,128,0,0,213));
  memcpy(view,seed,sizeof(view));SmShotPut(view,0x79b,0x91f8);
  for (unsigned i=0; i<4; ++i) SmShotPut(view,(unsigned[]){0xaf6,0xafa,0xb10,0xb14}[i],i&1 ? 384 : 640);
  SmShotPut(view,0x9c2,1000);SmShotPut(view,0x9c4,1000);SmShotPut(view,0x18a8,0);SmShotPut(view,0x18aa,0);SmShotPut(view,0x8b,0);
  assert(SmRoomRuntimeTouch(r,view,512,256));assert(SmShotWord(ram,0x7810)==1);
  SmRoomRuntimeSetPlayer(r,view,512,256,NULL);
  assert(SmRoomRuntimeStepVisible(r,128,128,0,0,213));
  for (unsigned i=0; i<4; ++i) {
    SmShotPut(view,0x8b,i&1 ? 0 : 0x80);assert(SmRoomRuntimeStepVisible(r,128,128,0,0,213));
  }
  assert(SmShotWord(ram,0xfb0)==60 && SmShotWord(ram,0x9c2)==777);
  assert(SmShotWord(view,0xaf6)==640 && SmShotWord(view,0xafa)==384 && SmShotWord(view,0x79b)==0x91f8);
  assert(SmRoomRuntimeSave(r,grab,size));memcpy(checkpoint,view,sizeof(view));
  for (unsigned pass=0; pass<2; ++pass) {
    if (pass) { assert(SmRoomRuntimeLoad(r,grab,size));memcpy(view,checkpoint,sizeof(view)); }
    for (unsigned i=0; i<60; ++i) {
      SmShotPut(view,0x8b,i&1 ? 0 : 0x80);assert(SmRoomRuntimeStepVisible(r,128,128,0,0,213));
    }
    assert(SmRoomRuntimeStepVisible(r,128,128,0,0,213));
    assert(!SmShotWord(ram,0xfb0) && !SmShotWord(ram,0x7810));
    assert(SmRoomRuntimeSave(r,pass ? again : end,size));
    if (!pass) memcpy(finished,view,sizeof(view));
  }
  assert(!memcmp(end,again,size) && !memcmp(view,finished,sizeof(view)));
  SmRoomRuntimeSetPlayer(r,NULL,0,0,NULL);SmRoomRuntimeSetMasks(r,UINT32_MAX,(1u<<18)-1,UINT32_MAX,(1u<<18)-1);
  assert(SmRoomRuntimeLoad(r,original,size));free(original);free(grab);free(end);free(again);
  puts("native Beetom grab consumes live root input and releases; private view restored; exact mid-grab continuation");
}
static void visual_sprites(SmRoomRuntime *r) {
  static uint8_t seed[0x20000];static uint16_t vram[0x8000];
  const uint8_t *ram=SmRoomRuntimeRam(r),*gfx=SmRoomRuntimeVram(r);assert(!SmRoomRuntimeActors(r));
  size_t size=SmRoomRuntimeStateSize();void *original=malloc(size),*checkpoint=malloc(size),*end=malloc(size),*again=malloc(size);
  assert(original && checkpoint && end && again && SmRoomRuntimeSave(r,original,size));
  memcpy(seed,ram,sizeof(seed));memset(seed+0xef78,0,0x80);unsigned index=62;
  SmShotPut(seed,0xef78+index,0xbe5a);SmShotPut(seed,0xeff8+index,3);SmShotPut(seed,0xf078+index,0);
  SmShotPut(seed,0xf0f8+index,128);SmShotPut(seed,0xf1f8+index,128);
  for (unsigned i=0; i<0x8000; ++i) vram[i]=(uint16_t)(gfx[2*i] | gfx[2*i+1]<<8);
  assert(SmRoomRuntimeCapture(r,seed,vram));
  for (unsigned tick=1; tick<=12; ++tick) {
    assert(SmRoomRuntimeStepVisible(r,128,128,0,0,213));
    if (tick==1) assert(SmShotWord(ram,0xef78+index)==0xbe5a && SmShotWord(ram,0xeff8+index)==2);
    if (tick==3) assert(SmShotWord(ram,0xef78+index)==0xbe5e && SmShotWord(ram,0xeff8+index)==3);
    if (tick==3) assert(SmRoomRuntimeSave(r,checkpoint,size));
  }
  assert(!SmShotWord(ram,0xef78+index) && SmRoomRuntimeSave(r,end,size));
  assert(SmRoomRuntimeLoad(r,checkpoint,size));
  for (unsigned tick=0; tick<9; ++tick) assert(SmRoomRuntimeStepVisible(r,128,128,0,0,213));
  assert(SmRoomRuntimeSave(r,again,size) && !memcmp(end,again,size));
  assert(SmRoomRuntimeLoad(r,original,size));free(original);free(checkpoint);free(end);free(again);
  puts("logic Off advances and deletes a real native B4 effect in the highest slot without initializing enemies");
}
static void player_pickups(SmRoomRuntime *r) {
  static uint8_t seed[0x20000],view[0x20000],before_view[0x20000];static uint16_t vram[0x8000];
  const uint8_t *ram=SmRoomRuntimeRam(r),*gfx=SmRoomRuntimeVram(r);unsigned slot=34;
  size_t size=SmRoomRuntimeStateSize();void *original=malloc(size);assert(original && SmRoomRuntimeSave(r,original,size));
  for (unsigned eligible=0; eligible<2; ++eligible) for (unsigned kind=1; kind<=5; ++kind) for (unsigned grapple=0; grapple<2; ++grapple) {
    assert(SmRoomRuntimeLoad(r,original,size));memcpy(seed,ram,sizeof(seed));memset(seed+0xf78,0,2048);memset(seed+0x1997,0,36);
    SmShotPut(seed,0x9c2,777);SmShotPut(seed,0x9c4,999);SmShotPut(seed,0x198d,0x8000);
    const unsigned addresses[]={0x1997,0x1a03,0x1b47,0x1b8f,0x1bb3,0x1bd7,0x1aff,0x1b23,0x1a4b,0x1a93,0xf410};
    const unsigned values[]={0xf337,0xefe0,0xed8d,0x7fff,0x1010,0x3000,kind*2,grapple ? 370 : 400,128,128,0xffff};
    for (unsigned i=0; i<sizeof(addresses)/sizeof(*addresses); ++i) SmShotPut(seed,addresses[i]+slot,(int)values[i]);
    for (unsigned i=0; i<0x8000; ++i) vram[i]=(uint16_t)(gfx[2*i] | gfx[2*i+1]<<8);
    assert(SmRoomRuntimeCapture(r,seed,vram));memcpy(view,seed,sizeof(view));
    SmShotPut(view,0x79b,0x91f8);SmShotPut(view,0xaf6,grapple ? 576 : 640);SmShotPut(view,0xafa,384);
    SmShotPut(view,0xb10,(int)SmShotWord(view,0xaf6));SmShotPut(view,0xb14,384);
    const unsigned fields[]={0x9c2,0x9c4,0x9c6,0x9c8,0x9ca,0x9cc,0x9ce,0x9d0};
    const unsigned amounts[]={10,99,3,10,1,10,2,10};
    for (unsigned i=0; i<8; ++i) SmShotPut(view,fields[i],(int)amounts[i]);
    SmShotPut(view,0xd08,640);SmShotPut(view,0xd0c,384);SmShotPut(view,0xd32,grapple ? 0xc703 : 0xc4f0);
    memcpy(before_view,view,sizeof(view));SmRoomRuntimeSetMasks(r,0,1u<<17,0,eligible ? 1u<<17 : 0);
    SmRoomRuntimeSetPlayer(r,view,512,256,NULL);
    assert(SmRoomRuntimeStepVisible(r,128,128,0,0,213));
    const unsigned reward_at[]={0,0x9c2,0x9c2,0x9ce,0x9c6,0x9ca};
    const unsigned reward[]={0,5,20,1,2,1};unsigned at=reward_at[kind];
    if (eligible) assert(SmShotWord(view,at)==SmShotWord(before_view,at)+reward[kind]);
    else assert(!memcmp(view,before_view,sizeof(view)) && SmShotWord(ram,0x1a03+slot)==0xefe0);
    unsigned once=SmShotWord(view,at);assert(SmRoomRuntimeStepVisible(r,128,128,0,0,213));
    assert(SmShotWord(view,at)==once && SmShotWord(ram,0x9c2)==777);
    assert(SmShotWord(view,0xaf6)==SmShotWord(before_view,0xaf6) && SmShotWord(view,0x79b)==0x91f8);
  }
  SmRoomRuntimeSetPlayer(r,NULL,0,0,NULL);SmRoomRuntimeSetMasks(r,UINT32_MAX,(1u<<18)-1,UINT32_MAX,(1u<<18)-1);
  assert(SmRoomRuntimeLoad(r,original,size));free(original);
  puts("native health/ammo drops reward Root once by body or live grapple; ineligible drops retained; private player restored");
}
static void ceres_scenery(const SmWorld *world) {
  const SmWorldRoom *room=SmWorldFindRoom(world,0xe0b5);assert(room);
  uint16_t state=SmWorldRoomState(room,Read,NULL,active);
  for (unsigned logic=0; logic<2; ++logic) {
    SmRoomRuntime *r=SmRoomRuntimeCreateConfigured(Read,NULL,room,state,0,active,logic!=0);
    assert(r && !*SmRoomRuntimeError(r));const uint8_t *ram=SmRoomRuntimeRam(r),*vram=SmRoomRuntimeVram(r);
    assert(ram[0x5d]==0x66 && SmShotWord(ram,0x7eb)==9);
    for (unsigned i=0; i<1536; ++i) assert(vram[0xda00+i]==Read(NULL,0xb0ba00+i));
    for (unsigned i=0; i<32; ++i) {
      unsigned at=0xf78+64*i;assert(!SmShotWord(ram,at)); /* Ridley and its parameter3 lock await actual entry. */
    }
    unsigned status=SmShotWord(ram,0x93f);
    for (unsigned i=0; i<120; ++i) assert(SmRoomRuntimeStepVisible(r,16,127,0,0,213) && SmRoomRuntimeStepEffects(r));
    assert(SmShotWord(ram,0x93f)==status);
    for (unsigned i=0; i<1536; ++i) assert(vram[0xda00+i]==Read(NULL,0xb0ba00+i));
    SmRoomRuntimeDestroy(r);
  }
  const SmWorldRoom *elevator=SmWorldFindRoom(world,0xdf45);assert(elevator);
  uint8_t seed[0x20000];memcpy(seed,active,sizeof(seed));
  SmShotPut(seed,0x79b,0);SmShotPut(seed,0x93f,2); /* escape prerequisite setup, not the tested result8000 */
  seed[0xd82e]|=1;
  uint16_t escape=SmWorldRoomState(elevator,Read,NULL,seed);assert(escape!=elevator->default_state);
  for (unsigned logic=0; logic<2; ++logic) {
    SmRoomRuntime *r=SmRoomRuntimeCreateConfigured(Read,NULL,elevator,escape,0,seed,logic!=0);
    assert(r && !*SmRoomRuntimeError(r));const uint8_t *ram=SmRoomRuntimeRam(r);
    assert(ram[0x56]==7 && SmShotWord(ram,0x783)==1);
    assert(SmShotWord(ram,0x78)==256 && !SmShotWord(ram,0x7a) && !SmShotWord(ram,0x7c) && SmShotWord(ram,0x7e)==256);
    assert(SmShotWord(ram,0x80)==128 && SmShotWord(ram,0x82)==1008);
    uint8_t packed[32768];uint32_t source=ram[0x7c3] | ram[0x7c4]<<8 | (uint32_t)ram[0x7c5]<<16;
    size_t packed_bytes=SmWorldUnpack(Read,NULL,source,packed,sizeof(packed));assert(packed_bytes);
    assert(!memcmp(SmRoomRuntimeVram(r),packed,packed_bytes));
    unsigned ordinary=0;
    for (unsigned i=0; i<32; ++i) {
      unsigned at=0xf78+64*i,id=SmShotWord(ram,at);
      if (!id) continue;
      assert(logic || SmRoomEnvironmentEnemy(id));
      if (id==0xe23f) { unsigned parameter=SmShotWord(ram,at+60);assert(parameter!=2 && parameter!=3);++ordinary; }
    }
    assert(ordinary==logic); /* parameter4's ordinary door remains when actor logic is enabled */
    for (unsigned i=0; i<120; ++i) assert(SmRoomRuntimeStepVisible(r,232,631,0,512,213) && SmRoomRuntimeStepEffects(r));
    assert(SmShotWord(ram,0x93f)==2 && SmShotWord(seed,0x93f)==2);
    SmRoomRuntimeDestroy(r);
  }
  puts("Ceres On/Off: complete lab scenery retained; Ridley lock and escape destruction await entry, ordinary doors retained");
}
static void ceres_environment(const SmWorld *world) {
  const SmWorldRoom *room=SmWorldFindRoom(world,0xdfd7);assert(room);
  uint8_t seed[0x20000];memcpy(seed,active,sizeof(seed));SmShotPut(seed,0x79b,0);seed[0xd82e]|=1;SmShotPut(seed,0x93f,2);
  uint16_t state=SmWorldRoomState(room,Read,NULL,seed);assert(state!=room->default_state);
  SmRoomRuntime *r=SmRoomRuntimeCreateConfigured(Read,NULL,room,state,0,seed,false);
  SmRoomRuntime *entered=SmRoomRuntimeCreateConfigured(Read,NULL,room,state,0,seed,true);
  assert(r && entered && !*SmRoomRuntimeError(r) && !*SmRoomRuntimeError(entered) && !SmRoomRuntimeActors(r));
  const uint8_t *ram=SmRoomRuntimeRam(r);unsigned count=0;
  for (unsigned i=0; i<32; ++i) { unsigned id=SmShotWord(ram,0xf78+64*i);assert(!id || SmRoomEnvironmentEnemy(id));count+=id!=0; }
  assert(count==5);uint8_t initial[2048];memcpy(initial,ram+0xf78,sizeof(initial));
  for (unsigned i=0; i<120; ++i) { SmRoomRuntimeSetMasks(r,UINT32_MAX,(1u<<18)-1,0,0);assert(SmRoomRuntimeStepVisible(r,62,304,0,256,213) && SmRoomRuntimeStepEffects(r)); }
  assert(!SmRoomRuntimeActors(r) && memcmp(initial,ram+0xf78,sizeof(initial)));
  size_t size=SmRoomRuntimeStateSize();void *saved=malloc(size),*end=malloc(size),*again=malloc(size);assert(saved && end && again);
  assert(SmRoomRuntimeSave(r,saved,size));
  memcpy(again,saved,size);((uint8_t *)again)[35+0xf78]=0x7f;((uint8_t *)again)[35+0xf79]=0xe8;
  assert(!SmRoomRuntimeValidate(again,size,room->header,state,false)); /* SRMS35-byte header; ordinary NPC forbidden Off */
  for (unsigned pass=0; pass<2; ++pass) {
    if (pass) assert(SmRoomRuntimeLoad(r,saved,size));
    for (unsigned i=0; i<17; ++i) { SmRoomRuntimeSetMasks(r,UINT32_MAX,(1u<<18)-1,0,0);assert(SmRoomRuntimeStepVisible(r,62,304,0,256,213) && SmRoomRuntimeStepEffects(r)); }
    assert(SmRoomRuntimeSave(r,pass ? again : end,size));
  }
  assert(!memcmp(end,again,size));free(saved);free(end);free(again);
  uint8_t *full=malloc(sizeof(seed));uint16_t *vram=calloc(0x8000,sizeof(*vram));assert(full && vram);memcpy(full,SmRoomRuntimeRam(entered),sizeof(seed));
  uint8_t doors[128];memcpy(doors,full+0xf78,sizeof(doors));SmRoomRuntimePromote(r,full,vram);
  assert(!memcmp(doors,full+0xf78,sizeof(doors))); /* real entry's ordinary E23F doors survive */
  for (unsigned i=0; i<count; ++i) {
    const uint8_t *phase=ram+0xf78+64*i;unsigned matches=0;
    for (unsigned j=0; j<32; ++j) if (SmShotWord(full,0xf78+64*j)==0xe1ff &&
        SmShotWord(full,0xf7a+64*j)==SmShotWord(phase,2) && SmShotWord(full,0xf7e +64*j)==SmShotWord(phase,6)) {
      assert(!memcmp(full+0xf78+64*j,phase,64));assert(!memcmp(full+0x7000+64*j,ram+0x7000+64*i,64));++matches;
    }
    assert(matches==1);
  }
  assert(SmRoomRuntimeEnableActors(r) && SmRoomRuntimeActors(r));
  assert(!memcmp(SmRoomRuntimeRam(r)+0xf78,doors,sizeof(doors)));
  for (unsigned j=2; j<7; ++j) assert(!memcmp(SmRoomRuntimeRam(r)+0xf78+64*j,full+0xf78+64*j,64));
  free(full);free(vram);SmRoomRuntimeDestroy(r);SmRoomRuntimeDestroy(entered);
  puts("Off Ceres steam alone initializes/animates/restores exactly; real entry and enabling actors retain steam phase without replacing ordinary doors");
}
static void ceres_entry_promotion(const SmWorld *world) {
  if (SmShotWord(active,0x79b)!=0xdf45) return;
  /* The explicit WRAM input is a real fresh-boot Ceres entry capture, with
   * native A08A9E/F6C5 initialization, rather than a fabricated actor record. */
  const SmWorldRoom *room=SmWorldFindRoom(world,0xdf45);assert(room);
  unsigned helper=32;
  for (unsigned i=0; i<32; ++i) {
    unsigned at=0xf78+64*i;
    if (SmShotWord(active,at)==0xe23f && SmShotWord(active,at+60)==2) helper=i;
  }
  assert(helper<32 && SmShotWord(active,0xf78+64*helper+48)==0xf7bd);
  uint8_t *entered=malloc(sizeof(active));uint16_t *vram=calloc(0x8000,sizeof(*vram));assert(entered && vram);
  for (unsigned logic=0; logic<2; ++logic) {
    SmRoomRuntime *r=SmRoomRuntimeCreateConfigured(Read,NULL,room,(uint16_t)SmShotWord(active,0x7bb),0,active,logic!=0);
    assert(r && !*SmRoomRuntimeError(r) && SmRoomRuntimeEntryOnly(r,active));
    assert(!SmRoomRuntimeEntryOnly(r,SmRoomRuntimeRam(r)));
    memcpy(entered,active,sizeof(active));assert(SmRoomRuntimePromoteDoors(r,entered));SmRoomRuntimePromote(r,entered,vram);
    assert(!memcmp(entered,active,sizeof(active)));
    SmRoomRuntimeDestroy(r);
  }
  free(entered);free(vram);
  puts("Actual native Ceres entry fixture: On/Off promotion preserves unflagged E23F2 initialization and door state exactly");
}
int main(int argc,char **argv) {
  if (argc!=4) return 2;
  rom=malloc(0x300000); assert(rom);
  FILE *f=fopen(argv[1],"rb"); assert(f);
  assert(fread(rom,1,0x300000,f)==0x300000); fclose(f);
  f=fopen(argv[2],"rb"); assert(f);
  assert(fread(active,1,sizeof(active),f)==sizeof(active)); fclose(f);
  memcpy(before,active,sizeof(active));
  SmWorld *world=malloc(sizeof(*world)); assert(world);
  assert(SmWorldLoad(world,Read,NULL));
  if (strtoul(argv[3],NULL,16)==0x9fe5) { ceres_scenery(world);ceres_environment(world); }
  ceres_entry_promotion(world);
  const SmWorldRoom *room=SmWorldFindRoom(world,(uint16_t)strtoul(argv[3],NULL,16)); assert(room);
  uint16_t state=SmWorldRoomState(room,Read,NULL,active);
  SmRoomRuntime *r=SmRoomRuntimeCreate(Read,NULL,room,state,active); assert(r);
  SmRoomRuntime *twin=SmRoomRuntimeCreate(Read,NULL,room,state,active); assert(twin);
  if (*SmRoomRuntimeError(r)) { fprintf(stderr,"init: %s\n",SmRoomRuntimeError(r)); return 1; }
  actor_blocks(r);
  if (room->header==0x9fe5) { actor_walk(r,twin);player_ai(r); }
  player_pickups(r);
  unsigned moved=0; uint8_t enemies[2048];
  memcpy(enemies,SmRoomRuntimeRam(r)+0xf78,sizeof(enemies));
  uint8_t initial_vram[65536]; memcpy(initial_vram,SmRoomRuntimeVram(r),sizeof(initial_vram));
  uint8_t fx[0x700]; memcpy(fx,SmRoomRuntimeRam(r)+0x1900,sizeof(fx));
  int x=enemies[2] | enemies[3]<<8, y=enemies[6] | enemies[7]<<8;
  unsigned ticks=room->header==0x9fe5 ? 300 : 60;
  for (unsigned i=0; i<ticks; ++i) {
    if (!SmRoomRuntimeStep(r,x+64,y,x-64,y-64)) {
      fprintf(stderr,"step %u: %s\n",i,SmRoomRuntimeError(r)); return 1;
    }
    if (!SmRoomRuntimeStepEffects(r)) {
      fprintf(stderr,"effects %u: %s\n",i,SmRoomRuntimeError(r)); return 1;
    }
    assert(SmRoomRuntimeStep(twin,x+64,y,x-64,y-64));
    assert(SmRoomRuntimeStepEffects(twin));
    assert(!memcmp(SmRoomRuntimeRam(r),SmRoomRuntimeRam(twin),sizeof(active)));
    assert(!memcmp(SmRoomRuntimeVram(r),SmRoomRuntimeVram(twin),65536));
  }
  const uint8_t *ram=SmRoomRuntimeRam(r);
  for (unsigned i=0; i<32; ++i)
    moved+=memcmp(enemies+64*i+2,ram+0xf78+64*i+2,8)!=0;
  assert(!memcmp(active,before,sizeof(active)));
  printf("room %04X: %u native ticks, %u slots moved, FX changed=%d, type=%u; interleaved twins identical; active RAM unchanged\n",
      room->header,ticks,moved,memcmp(fx,ram+0x1900,sizeof(fx))!=0,ram[0x196e]);
  if (ram[0x196e]==2 || ram[0x196e]==4) {
    assert(memcmp(initial_vram,SmRoomRuntimeVram(r),sizeof(initial_vram)));
    puts("native liquid animation publishes changed tiles into private VRAM");
  }
  size_t bytes=SmRoomRuntimeStateSize(); void *snapshot=malloc(bytes); assert(snapshot);
  void *continued=malloc(bytes), *repeated=malloc(bytes); assert(continued && repeated);
  assert(SmRoomRuntimeSave(r,snapshot,bytes));
  assert(!SmRoomRuntimeLoad(r,snapshot,bytes-1));
  assert(SmRoomRuntimeStep(r,x+64,y,x-64,y-64) && SmRoomRuntimeStepEffects(r));
  assert(SmRoomRuntimeSave(r,continued,bytes));
  assert(SmRoomRuntimeLoad(r,snapshot,bytes));
  assert(SmRoomRuntimeStep(r,x+64,y,x-64,y-64) && SmRoomRuntimeStepEffects(r));
  assert(SmRoomRuntimeSave(r,repeated,bytes));
  assert(!memcmp(continued,repeated,bytes));
  puts("resident save/restore continuation matches exactly; truncated input rejected");
  power_bombs(r);
  power_bomb_projectiles(r);
  if (room->header==0x93aa) { hit_shots(r);touch(r);projectile_interactions(r);projectile_terrain(r,twin); }
  uint8_t *entered=malloc(sizeof(active)); uint16_t *entered_vram=calloc(0x8000,sizeof(*entered_vram));
  assert(entered && entered_vram); memcpy(entered,SmRoomRuntimeRam(r),sizeof(active));
  entered[0xf8c]^=0x55; /* A fresh native load must not replace resident health. */
  entered[0x9c2]=0x34; entered[0x9c3]=0x12; entered[0xaf6]=0x78; entered[0xaf7]=0x56;
  uint8_t stack[32]; memcpy(stack,entered+0x1fe0,sizeof(stack));
  SmRoomRuntimePromote(r,entered,entered_vram);
  assert(!memcmp(entered+0xf78,SmRoomRuntimeRam(r)+0xf78,2048));
  assert(entered[0x9c2]==0x34 && entered[0x9c3]==0x12 && entered[0xaf6]==0x78 && entered[0xaf7]==0x56);
  assert(!memcmp(stack,entered+0x1fe0,sizeof(stack)));
  puts("promotion retains resident enemies while preserving active player and continuation stack");
  free(entered); free(entered_vram);
  free(snapshot); free(continued); free(repeated);
  uint8_t *visual_seed=malloc(sizeof(active));assert(visual_seed);memcpy(visual_seed,active,sizeof(active));
  SmShotPut(visual_seed,0xef78+62,0xbe5a);SmShotPut(visual_seed,0xeff8+62,3);
  SmRoomRuntime *visual=SmRoomRuntimeCreateConfigured(Read,NULL,room,state,0,visual_seed,false); assert(visual && !*SmRoomRuntimeError(visual));
  assert(SmShotWord(visual_seed,0xef78+62)==0xbe5a && !SmShotWord(SmRoomRuntimeRam(visual),0xef78+62));free(visual_seed);
  visual_sprites(visual);
  uint8_t visual_start[65536]; memcpy(visual_start,SmRoomRuntimeVram(visual),sizeof(visual_start));
  for (unsigned frame=0; frame<60; ++frame) {
    assert(SmRoomRuntimeStepVisible(visual,x+64,y,x-64,y-64,213));
    assert(SmRoomRuntimeStepEffects(visual));
  }
  const uint8_t *visual_ram=SmRoomRuntimeRam(visual);
  assert(SmRoomRuntimeProjectiles(visual,active,0,0,1023,true) && !memcmp(active,before,sizeof(active)));
  for (unsigned slot=0; slot<32; ++slot) assert(!visual_ram[0xf78+64*slot] && !visual_ram[0xf79+64*slot]);
  if (visual_ram[0x196e]==2 || visual_ram[0x196e]==4) assert(memcmp(visual_start,SmRoomRuntimeVram(visual),sizeof(visual_start)));
  puts("logic Off: no enemies spawned; environmental effects continue");
  SmRoomRuntimeDestroy(visual);
  SmRoomRuntimeDestroy(r); SmRoomRuntimeDestroy(twin); free(world); free(rom); return 0;
}
