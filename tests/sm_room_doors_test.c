#include "sm_room_runtime.h"
#include "sm_door.h"
#include "sm_shot_view.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static uint8_t *rom,active[0x20000],unchanged[0x20000];
static unsigned word(const uint8_t *p) { return p[0] | p[1]<<8; }
static uint8_t Read(void *unused,uint32_t a) {
  (void)unused; unsigned offset=((a>>16)&127)*32768u+(a&32767);
  return (a&0x8000) && offset<0x300000 ? rom[offset] : 255;
}
static void Tick(SmRoomRuntime *r,unsigned n) {
  while (n--) {
    if (!SmRoomRuntimeStepDoors(r)) { fprintf(stderr,"door tick: %s\n",SmRoomRuntimeError(r)); abort(); }
  }
}
static bool Open(SmRoomRuntime *r,unsigned x,unsigned y) {
  const uint8_t *ram=SmRoomRuntimeRam(r); unsigned w=word(ram+0x7a5);
  unsigned type=word(ram+0x10002+2*(y*w+x))>>12;
  return type==0 || type==4;
}
static SmRoomRuntime *Create(const SmWorldRoom *room,uint16_t state) {
  static uint8_t fresh[sizeof(active)];memcpy(fresh,active,sizeof(fresh));
  /* This probe qualifies locked ammunition rules regardless of the captured
   * player's previous door unlocks. Production residents retain those bits. */
  memset(fresh+0xd8b0,0,64);
  SmRoomRuntime *r=SmRoomRuntimeCreateConfigured(Read,NULL,room,state,0,fresh,false);
  assert(r); if (*SmRoomRuntimeError(r)) { fprintf(stderr,"init %04X: %s\n",room->header,SmRoomRuntimeError(r)); abort(); }
  return r;
}
static void Shot(SmRoomRuntime *r,unsigned x,unsigned y,unsigned type,unsigned direction) {
  static uint8_t view[0x20000],before[sizeof(view)];
  memcpy(view,active,sizeof(view));
  /* Exercise the real block dispatcher in an offset world view, including
   * extension cells below/right of the cap's native anchor. */
  bool horizontal=direction<2;
  int ox=768,oy=-256,px=(int)(x*16+8)+ox,py=(int)(y*16+8)+oy;
  SmShotPut(view,0xdde,0);SmShotPut(view,0xc18,(int)type);SmShotPut(view,0xc40,0x8123);
  SmShotPut(view,0xb64,px);SmShotPut(view,0xb78,py);SmShotPut(view,0xbb4,4);SmShotPut(view,0xbc8,4);
  SmShotPut(view,0x18,horizontal ? px : py);SmShotPut(view,0x1c,horizontal ? px : py);
  SmShotPut(view,0x1e,1);SmShotPut(view,0x1a,0);SmShotPut(view,0x26,0);SmShotPut(view,0x28,0);
  memcpy(before,view,sizeof(view));
  int result=SmRoomRuntimeShot(r,view,ox,oy,(int)x,(int)y,horizontal,NULL,NULL);
  if (result<0) { fprintf(stderr,"shot: %s\n",SmRoomRuntimeError(r)); abort(); }
  assert(result==1 && SmShotWord(view,0x28)==65535);
  assert(!memcmp(view+SM_SHOT_POOL,before+SM_SHOT_POOL,SM_SHOT_POOL_BYTES));
  SmShotPut(view,0x28,0);assert(!memcmp(view,before,sizeof(view)));
  size_t size=SmRoomRuntimeStateSize();void *state=malloc(size);assert(state);
  assert(SmRoomRuntimeSave(r,state,size)); /* loan did not leave a suspended CPU */
  free(state);
}
int main(int argc,char **argv) {
  if (argc!=3) return 2;
  rom=malloc(0x300000); assert(rom);
  FILE *f=fopen(argv[1],"rb"); assert(f); assert(fread(rom,1,0x300000,f)==0x300000); fclose(f);
  f=fopen(argv[2],"rb"); assert(f); assert(fread(active,1,sizeof(active),f)==sizeof(active)); fclose(f);
  memcpy(unchanged,active,sizeof(active));
  SmWorld *world=malloc(sizeof(*world)); assert(world && SmWorldLoad(world,Read,NULL));
  unsigned blue=0,colored=0;
  for (unsigned n=0; n<world->room_count && blue!=15; ++n) {
    const SmWorldRoom *room=&world->rooms[n];
    SmRoomRuntime *r=Create(room,room->default_state); Tick(r,32);
    const uint8_t *ram=SmRoomRuntimeRam(r); unsigned w=word(ram+0x7a5),h=word(ram+0x7a7);
    for (unsigned block=0; block<w*h; ++block) {
      unsigned bts=ram[0x16402+block],type=word(ram+0x10002+block*2)>>12;
      if (type!=12 || bts<0x40 || bts>0x43 || (blue&(1u<<(bts-0x40)))) continue;
      unsigned x=block%w,y=block/w;
      assert(SmRoomRuntimeDoorShot(r,x,y,0x300)); Tick(r,32); assert(!Open(r,x,y));
      static uint8_t entered[sizeof(active)];
      memcpy(entered,ram,sizeof(entered));
      Shot(r,x+(bts>=0x42 ? 2 : 0),y+(bts<0x42 ? 2 : 0),0x8000,bts-0x40);
      Tick(r,48); assert(Open(r,x,y));
      /* Entry has already uploaded the closed cap to the native tilemap.
       * A completed opening has no PLM left to repaint that stale graphic. */
      bool redraw=false;
      assert(SmRoomRuntimePromoteDoors(r,entered,&redraw) && redraw);
      redraw=true;
      assert(SmRoomRuntimePromoteDoors(r,entered,&redraw) && !redraw);
      assert(SmRoomRuntimeDoorClose(r,x,y,bts-0x40));
      assert(SmDoorPhase(Read,NULL,ram,x,y)==SM_DOOR_CLOSING); Tick(r,48); assert(!Open(r,x,y));
      assert(SmRoomRuntimeDoorShot(r,x,y,0)); Tick(r,48); assert(Open(r,x,y));
      printf("blue orientation %u room %04X cap %u,%u: bomb rejected, beam opens (logic Off)\n",bts-0x40,room->header,x,y);
      blue|=1u<<(bts-0x40);
    }
    SmRoomRuntimeDestroy(r);
  }
  assert(blue==15);
  for (unsigned i=0; i<world->marker_count; ++i) {
    const SmWorldMarker *m=&world->markers[i];
    if (m->kind!=SM_MAP_CAP || m->detail<SM_CAP_MISSILE || m->detail>SM_CAP_POWER_BOMB ||
        (colored&(1u<<m->detail))) continue;
    const SmWorldRoom *room=SmWorldFindRoom(world,m->room);
    if (m->state!=room->default_state) continue;
    SmRoomRuntime *r=Create(room,m->state); Tick(r,32); assert(!Open(r,m->x,m->y));
    uint8_t *receiver=malloc(sizeof(active)); uint16_t *tiles=calloc(0x8000,sizeof(*tiles)); assert(receiver && tiles);
    memcpy(receiver,SmRoomRuntimeRam(r),sizeof(active));
    /* Stations remain entry-only, even while resident item and hatch
     * scenery animate. Their reserved timer must not advance. */
    receiver[0x1c37]=0x6f; receiver[0x1c38]=0xb7;
    receiver[0x1cd7]=0x69; receiver[0x1cd8]=0x84;
    receiver[0xde1c]=123; receiver[0xde1d]=0;
    receiver[0x9c2]=0x56; receiver[0x9c3]=0x34;
    uint8_t opened_bits[64]; memcpy(opened_bits,SmRoomRuntimeRam(r)+0xd8b0,sizeof(opened_bits));
    assert(SmRoomRuntimeDoorOpen(r,m->x,m->y,m->direction));
    assert(SmRoomRuntimePromoteDoors(r,receiver,NULL));
    assert(word(receiver+0x1c37)==0xb76f && word(receiver+0x9c2)==0x3456);
    SmRoomRuntime *promoted=Create(room,m->state);
    assert(SmRoomRuntimeCapture(promoted,receiver,tiles)); Tick(promoted,64);
    assert(Open(promoted,m->x,m->y) && word(SmRoomRuntimeRam(promoted)+0xde1c)==123);
    SmRoomRuntimeDestroy(promoted); free(receiver); free(tiles);
    Tick(r,64); assert(Open(r,m->x,m->y));
    assert(!memcmp(opened_bits,SmRoomRuntimeRam(r)+0xd8b0,sizeof(opened_bits)));
    assert(SmRoomRuntimeDoorClose(r,m->x,m->y,m->direction));
    assert(SmDoorPhase(Read,NULL,SmRoomRuntimeRam(r),m->x,m->y)==SM_DOOR_CLOSING);
    Tick(r,64); assert(!Open(r,m->x,m->y));
    for (unsigned hit=0; hit<6; ++hit) {
      Shot(r,m->x,m->y,0x8000,m->direction); Tick(r,12);
      if (Open(r,m->x,m->y)) {
        const uint8_t *bad=SmRoomRuntimeRam(r);
        fprintf(stderr,"wrong beam opened %04X state %04X header %04X cap %u,%u hit %u BTS %02X\n",
            m->room,m->state,m->plm,m->x,m->y,hit,bad[0x16402+m->y*word(bad+0x7a5)+m->x]);
        for (unsigned j=0; j<40; ++j) if (word(bad+0x1c37+2*j))
          fprintf(stderr,"slot %u h%04X b%04X pre%04X ins%04X trigger%04X\n",j,
              word(bad+0x1c37+2*j),word(bad+0x1c87+2*j),word(bad+0x1cd7+2*j),word(bad+0x1d27+2*j),word(bad+0x1d77+2*j));
        abort();
      }
    }
    if (m->detail==SM_CAP_MISSILE) {
      for (unsigned hit=0; hit<4; ++hit) {
        Shot(r,m->x,m->y,0x100,m->direction); Tick(r,16); assert(!Open(r,m->x,m->y));
      }
      size_t bytes=SmRoomRuntimeStateSize(); void *saved=malloc(bytes); assert(saved && SmRoomRuntimeSave(r,saved,bytes));
      Shot(r,m->x,m->y,0x100,m->direction); Tick(r,64); assert(Open(r,m->x,m->y));
      assert(SmRoomRuntimeLoad(r,saved,bytes)); assert(!Open(r,m->x,m->y));
      Shot(r,m->x,m->y,0x100,m->direction); Tick(r,64); assert(Open(r,m->x,m->y)); free(saved);
    } else {
      Shot(r,m->x,m->y,0x100,m->direction); Tick(r,32); assert(!Open(r,m->x,m->y));
      Shot(r,m->x,m->y,m->detail==SM_CAP_SUPER ? 0x200 : 0x300,m->direction); Tick(r,64);
      assert(Open(r,m->x,m->y));
    }
    printf("colored type %u room %04X cap %u,%u: native ammunition/count requirements and opening pass\n",m->detail,m->room,m->x,m->y);
    colored|=1u<<m->detail; SmRoomRuntimeDestroy(r);
  }
  assert(colored==14); assert(!memcmp(active,unchanged,sizeof(active)));
  bool quota_lock=false,boss_lock=false;
  for (unsigned i=0; i<world->marker_count && (!quota_lock || !boss_lock); ++i) {
    const SmWorldMarker *m=&world->markers[i]; const SmWorldRoom *room=SmWorldFindRoom(world,m->room);
    if (m->kind!=SM_MAP_CAP || m->detail!=SM_CAP_GREY || m->state!=room->default_state) continue;
    SmRoomRuntime *r=Create(room,m->state); Tick(r,32);
    const uint8_t *ram=SmRoomRuntimeRam(r); unsigned block=2*(m->y*word(ram+0x7a5)+m->x),pre=0;
    for (unsigned j=0; j<40; ++j) if (word(ram+0x1c87+2*j)==block && word(ram+0x1c37+2*j)) pre=word(ram+0x1cd7+2*j);
    bool quota=pre==0xbe01 && word(ram+0xe52)>0,boss=pre==0xbdd4 || pre==0xbde3 || pre==0xbdf2;
    if ((!quota || quota_lock) && (!boss || boss_lock)) { SmRoomRuntimeDestroy(r); continue; }
    for (unsigned hit=0; hit<6; ++hit) { assert(SmRoomRuntimeDoorShot(r,m->x,m->y,0x200)); Tick(r,16); assert(!Open(r,m->x,m->y)); }
    if (quota) quota_lock=true;
    if (boss) boss_lock=true;
    printf("grey room %04X pre %04X: native %s lock retained with enemies Off\n",m->room,pre,quota ? "kill quota" : "boss");
    SmRoomRuntimeDestroy(r);
  }
  assert(quota_lock && boss_lock); assert(!memcmp(active,unchanged,sizeof(active)));
  puts("all four blue orientations, red/green/yellow native rules, mid-hit save/load; active RAM unchanged");
  free(world); free(rom); return 0;
}
