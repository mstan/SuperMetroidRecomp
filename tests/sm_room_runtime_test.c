#include "sm_room_runtime.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static uint8_t *rom, active[0x20000], before[0x20000];
static uint8_t Read(void *ctx,uint32_t a) {
  (void)ctx; unsigned at=((a>>16)&127)*32768u+(a&32767);
  return (a&0x8000) && at<0x300000 ? rom[at] : 255;
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
  const SmWorldRoom *room=SmWorldFindRoom(world,(uint16_t)strtoul(argv[3],NULL,16)); assert(room);
  uint16_t state=SmWorldRoomState(room,Read,NULL,active);
  SmRoomRuntime *r=SmRoomRuntimeCreate(Read,NULL,room,state,active); assert(r);
  SmRoomRuntime *twin=SmRoomRuntimeCreate(Read,NULL,room,state,active); assert(twin);
  if (*SmRoomRuntimeError(r)) { fprintf(stderr,"init: %s\n",SmRoomRuntimeError(r)); return 1; }
  unsigned moved=0; uint8_t enemies[2048];
  memcpy(enemies,SmRoomRuntimeRam(r)+0xf78,sizeof(enemies));
  uint8_t initial_vram[65536]; memcpy(initial_vram,SmRoomRuntimeVram(r),sizeof(initial_vram));
  uint8_t fx[0x700]; memcpy(fx,SmRoomRuntimeRam(r)+0x1900,sizeof(fx));
  int x=enemies[2] | enemies[3]<<8, y=enemies[6] | enemies[7]<<8;
  for (unsigned i=0; i<60; ++i) {
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
  printf("room %04X: 60 native ticks, %u slots moved, FX changed=%d, type=%u; interleaved twins identical; active RAM unchanged\n",
      room->header,moved,memcmp(fx,ram+0x1900,sizeof(fx))!=0,ram[0x196e]);
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
  SmRoomRuntime *visual=SmRoomRuntimeCreateConfigured(Read,NULL,room,state,0,active,false); assert(visual && !*SmRoomRuntimeError(visual));
  uint8_t visual_start[65536]; memcpy(visual_start,SmRoomRuntimeVram(visual),sizeof(visual_start));
  for (unsigned frame=0; frame<60; ++frame) {
    assert(SmRoomRuntimeStepVisible(visual,x+64,y,x-64,y-64,213));
    assert(SmRoomRuntimeStepEffects(visual));
  }
  const uint8_t *visual_ram=SmRoomRuntimeRam(visual);
  for (unsigned slot=0; slot<32; ++slot) assert(!visual_ram[0xf78+64*slot] && !visual_ram[0xf79+64*slot]);
  if (visual_ram[0x196e]==2 || visual_ram[0x196e]==4) assert(memcmp(visual_start,SmRoomRuntimeVram(visual),sizeof(visual_start)));
  puts("logic Off: no enemies spawned; environmental effects continue");
  SmRoomRuntimeDestroy(visual);
  SmRoomRuntimeDestroy(r); SmRoomRuntimeDestroy(twin); free(world); free(rom); return 0;
}
