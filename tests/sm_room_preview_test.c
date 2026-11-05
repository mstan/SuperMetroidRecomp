/* Decode real cartridge scenery using the same graph as the map. No copied
 * ROM assets or hardcoded room images. Run with the owner's stock ROM. */
#include "sm_room_preview.h"
#include "mods/sm_world.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static uint8_t *rom, ram[0x20000], before[0x20000];
static size_t size;
static uint8_t Read(void *ctx, uint32_t a) {
  (void)ctx; size_t at=((a>>16)&127)*32768u+(a&32767);
  return (a&0x8000) && at<size ? rom[at] : 255;
}
static void Word(unsigned a, unsigned v) { ram[a]=(uint8_t)v; ram[a+1]=(uint8_t)(v>>8); }
int main(int argc, char **argv) {
  if (argc!=2) return 2;
  FILE *f=fopen(argv[1],"rb"); assert(f);
  assert(!fseek(f,0,SEEK_END)); size=(size_t)ftell(f); rewind(f);
  assert(size==0x300000); rom=malloc(size); assert(rom);
  assert(fread(rom,1,size,f)==size); fclose(f);
  SmWorld *world=malloc(sizeof(*world)); assert(world);
  assert(SmWorldLoad(world,Read,NULL));
  SmRoomPreviewSetRom(rom,size);
  unsigned rooms=0, pixels=0, residents=0,ceres=0;
  for (unsigned i=0; i<world->room_count; ++i) {
    const SmWorldRoom *r=&world->rooms[i];
    Word(0x79b,r->header); Word(0x7bb,r->default_state);
    Word(0x7a5,r->width*16); Word(0x7a7,r->height*16);
    /* The active room's own terrain determines allocation overlap. */
    uint8_t level[65536]; uint32_t source=Read(NULL,0x8f0000u+r->default_state) |
        Read(NULL,0x8f0001u+r->default_state)<<8 | (uint32_t)Read(NULL,0x8f0002u+r->default_state)<<16;
    size_t bytes=SmWorldUnpack(Read,NULL,source,level,sizeof(level)); assert(bytes>=2);
    unsigned length=level[0] | level[1]<<8; assert(length<=0x6400 && bytes>=2+length);
    memcpy(ram+0x10000,level,2+length);
    memcpy(before,ram,sizeof(ram));
    SmRoomPreviewPrepare(ram,-213,-32,r->width*256+213,r->height*256+32);
    unsigned n=SmRoomPreviewCount();
    if (r->area==6) { assert(n); ++ceres; }
    if (n) ++rooms;
    else printf("room %04X: no visible planar preview\n",r->header);
    residents+=n;
    assert(n<=16);
    assert(!memcmp(before,ram,sizeof(ram)));
    for (int y=0; y<r->height*256; y+=16)
      for (int x=-208; x<r->width*256+208; x+=16) {
        uint16_t color;
        if (SmRoomPreviewPixel(x,y,&color)) { assert(color<32768); ++pixels; }
      }
    /* Repeat publication must preserve geometry/pixels and resident count. */
    SmRoomPreviewPrepare(ram,-213,-32,r->width*256+213,r->height*256+32);
    assert(SmRoomPreviewCount()==n);
  }
  printf("%u/%u rooms with previews, %u resident decodes, %u sampled pixels\n",rooms,world->room_count,residents,pixels);
  assert(rooms>100 && pixels>1000 && ceres==6);
  SmRoomPreviewSetRom(NULL,0);
  assert(!SmRoomPreviewCount());
  free(world); free(rom); return 0;
}
