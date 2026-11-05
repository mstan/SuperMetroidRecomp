#include "sm_rooms.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static uint8_t active[0x20000], original[0x20000];
int main(int argc,char **argv) {
  if (argc!=3) return 2;
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
    printf("logic %s: %u resident rooms, 60 native ticks, exact whole-manager save/load continuation; active RAM unchanged\n",logic ? "On" : "Off",SmRoomsScene()->count);
    free(saved); free(a); free(b);
  }
  SmRoomsSetRom(NULL,0); free(rom); return 0;
}
