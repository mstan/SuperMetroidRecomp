#include "sm_rooms.h"
#include "sm_room_runtime.h"
#include "sm_room_preview.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct Resident { SmRoomPlacement placement; SmRoomRuntime *runtime; } Resident;
static const uint8_t *rom;
static size_t rom_bytes;
static Resident residents[SM_ROOM_RESIDENTS];
static unsigned count, root;
static bool logic, seamless, pending_entry;
static int camera_dx,camera_dy;
static bool rebased;
static SmRoomScene *publication;
static unsigned word(const uint8_t *p) { return p[0] | p[1]<<8; }
static uint8_t read_rom(void *unused, uint32_t a) {
  (void)unused; size_t offset=((a>>16)&127)*32768u+(a&32767);
  return (a&0x8000) && offset<rom_bytes ? rom[offset] : 255;
}
void SmRoomsReset(void) {
  for (unsigned i=0; i<count; ++i) SmRoomRuntimeDestroy(residents[i].runtime);
  memset(residents,0,sizeof(residents)); count=root=0;
  pending_entry=false;
  camera_dx=camera_dy=0; rebased=false;
  free(publication); publication=NULL;
}
void SmRoomsSetRom(const uint8_t *image, size_t bytes) {
  SmRoomsReset(); rom=image; rom_bytes=bytes; SmRoomPreviewSetRom(image,bytes);
}
const SmRoomScene *SmRoomsScene(void) { return publication; }
bool SmRoomsDoor(uint16_t definition, SmRoomPlacement *destination) {
  if (!destination) return false;
  for (unsigned i=0; i<count; ++i) if (residents[i].placement.incoming==definition) {
    *destination=residents[i].placement; return true;
  }
  return false;
}
bool SmRoomsHandoff(const uint8_t previous[0x20000],const uint16_t previous_vram[0x8000],
    uint8_t active[0x20000],uint16_t vram[0x8000],const SmRoomPlacement *p,unsigned direction) {
  unsigned old_room=word(previous+0x79b),old_state=word(previous+0x7bb);
  const SmWorldRoom *room=SmRoomPreviewRoom((uint16_t)old_room);
  if (!room || !p || word(active+0x79b)!=p->room) return false;
  SmRoomRuntime *departing=SmRoomRuntimeCreateConfigured(read_rom,NULL,room,(uint16_t)old_state,0,previous,logic);
  if (!departing || !SmRoomRuntimeCapture(departing,previous,previous_vram)) {
    SmRoomRuntimeDestroy(departing); return false;
  }
  for (unsigned i=0; i<count;) {
    if (residents[i].placement.room==p->room) {
      SmRoomRuntimePromote(residents[i].runtime,active,vram);
      SmRoomRuntimeDestroy(residents[i].runtime);
      residents[i]=residents[--count];
    } else ++i;
  }
  for (unsigned i=0; i<count; ++i) {
    SmRoomPlacement *q=&residents[i].placement;
    q->x-=p->x; q->y-=p->y;
    q->left-=p->x; q->right-=p->x; q->top-=p->y; q->bottom-=p->y;
  }
  SmRoomPlacement q={.room=(uint16_t)old_room,.state=(uint16_t)old_state,
      .x=-p->x,.y=-p->y,.width=room->width*256,.height=room->height*256,
      .left=-65536,.top=-65536,.right=65536,.bottom=65536};
  if (direction==0) q.right=p->left-p->x;
  if (direction==1) q.left=p->right-p->x;
  if (direction==2) q.bottom=p->top-p->y;
  if (direction==3) q.top=p->bottom-p->y;
  if (count==SM_ROOM_RESIDENTS) SmRoomRuntimeDestroy(residents[--count].runtime);
  residents[count++]=(Resident){q,departing}; pending_entry=true;
  camera_dx+=(int16_t)word(previous+0x911)-p->x-(int16_t)word(active+0x911);
  camera_dy+=(int16_t)word(previous+0x915)-p->y-(int16_t)word(active+0x915);
  rebased=true;
  return true;
}
static bool publish(void) {
  size_t bytes=SmRoomSceneBytes(count);
  SmRoomScene *next=calloc(1,bytes); if (!next) return false;
  *next=(SmRoomScene){.version=2,.count=count,.logic=logic,.seamless=seamless,.root=root,
      .camera_dx=camera_dx,.camera_dy=camera_dy};
  for (unsigned i=0; i<count; ++i) {
    SmRoomImage *out=&next->rooms[i];
    out->placement=residents[i].placement;
    memcpy(out->ram,SmRoomRuntimeRam(residents[i].runtime),sizeof(out->ram));
    memcpy(out->vram,SmRoomRuntimeVram(residents[i].runtime),sizeof(out->vram));
  }
  free(publication); publication=next; return true;
}
bool SmRoomsFrame(const uint8_t active[0x20000], bool enabled, unsigned extra,
                    bool requested_logic, bool requested_seamless) {
  if (!enabled || !rom || rom_bytes!=0x300000) { SmRoomsReset(); return true; }
  unsigned game_state=word(active+0x998);
  if (game_state>=9 && game_state<=13) pending_entry=true;
  if (game_state!=8 || word(active+0xa78)) return true;
  unsigned current=word(active+0x79b);
  bool previous_logic=logic;
  if (current!=root && !rebased) camera_dx=camera_dy=0;
  if (current!=root || pending_entry) { root=current; logic=requested_logic; seamless=requested_seamless; }
  pending_entry=false;
  if (!seamless) camera_dx=camera_dy=0;
  if (!rebased) {
    camera_dx+=camera_dx < -8 ? 8 : camera_dx>8 ? -8 : -camera_dx;
    camera_dy+=camera_dy < -8 ? 8 : camera_dy>8 ? -8 : -camera_dy;
  }
  rebased=false;
  int x=(int16_t)word(active+0x911)+camera_dx, y=(int16_t)word(active+0x915)+camera_dy;
  SmRoomPreviewPrepare(active,x-(int)extra-32,y-32,x+256+(int)extra+32,y+256);
  Resident old[SM_ROOM_RESIDENTS]; unsigned old_count=count;
  memcpy(old,residents,sizeof(old)); memset(residents,0,sizeof(residents)); count=0;
  bool ok=true;
  for (unsigned i=0; i<SmRoomPreviewCount(); ++i) {
    SmRoomPlacement p; if (!SmRoomPreviewPlacement(i,&p)) continue;
    SmRoomRuntime *runtime=NULL;
    for (unsigned j=0; j<old_count; ++j)
      if (previous_logic==logic && old[j].runtime && old[j].placement.room==p.room && old[j].placement.state==p.state &&
          (old[j].placement.incoming==p.incoming || !old[j].placement.incoming)) {
        runtime=old[j].runtime; old[j].runtime=NULL; break;
      }
    if (!runtime) {
      const SmWorldRoom *room=SmRoomPreviewRoom(p.room);
      runtime=SmRoomRuntimeCreateConfigured(read_rom,NULL,room,p.state,p.incoming,active,logic);
      SmRoomRuntimeBackground(runtime,SmRoomPreviewVram(i));
    }
    if (!runtime || *SmRoomRuntimeError(runtime) ||
        !SmRoomRuntimeStepVisible(runtime,(int)word(active+0xaf6)-p.x,(int)word(active+0xafa)-p.y,
                                  x-p.x,y-p.y,extra) || !SmRoomRuntimeStepEffects(runtime)) {
      fprintf(stderr,"[sm-rooms] root=%04X resident=%04X state=%04X: %s\n",root,p.room,p.state,SmRoomRuntimeError(runtime));
      SmRoomRuntimeDestroy(runtime); ok=false; break;
    }
    residents[count++]=(Resident){p,runtime};
  }
  for (unsigned i=0; i<old_count; ++i) SmRoomRuntimeDestroy(old[i].runtime);
  return ok && publish();
}

typedef struct RoomsState {
  uint32_t magic,version,count,root,logic,seamless,unit,pending_entry;
  int32_t camera_dx,camera_dy;
} RoomsState;
size_t SmRoomsStateBound(void) {
  return sizeof(RoomsState)+SM_ROOM_RESIDENTS*(sizeof(SmRoomPlacement)+SmRoomRuntimeStateSize());
}
size_t SmRoomsSave(void *out, size_t capacity) {
  size_t unit=sizeof(SmRoomPlacement)+SmRoomRuntimeStateSize(), bytes=sizeof(RoomsState)+count*unit;
  if (!out || capacity<bytes) return 0;
  RoomsState h={0x534d5257,3,count,root,logic,seamless,(uint32_t)unit,pending_entry,camera_dx,camera_dy};
  memcpy(out,&h,sizeof(h)); uint8_t *p=(uint8_t *)out+sizeof(h);
  for (unsigned i=0; i<count; ++i,p+=unit) {
    memcpy(p,&residents[i].placement,sizeof(SmRoomPlacement));
    if (!SmRoomRuntimeSave(residents[i].runtime,p+sizeof(SmRoomPlacement),SmRoomRuntimeStateSize())) return 0;
  }
  return bytes;
}
bool SmRoomsValidate(const void *in, size_t size) {
  RoomsState h;
  if (!in || size<sizeof(h)) return false;
  memcpy(&h,in,sizeof(h));
  if (h.magic!=0x534d5257 || h.version!=3 || h.count>SM_ROOM_RESIDENTS || h.root>65535 ||
      h.camera_dx < -4096 || h.camera_dx>4096 || h.camera_dy < -4096 || h.camera_dy>4096 ||
      h.logic>1 || h.seamless>1 || h.pending_entry>1 || h.unit!=sizeof(SmRoomPlacement)+SmRoomRuntimeStateSize() ||
      size!=sizeof(h)+(size_t)h.count*h.unit) return false;
  const uint8_t *p=(const uint8_t *)in+sizeof(h);
  for (unsigned i=0; i<h.count; ++i,p+=h.unit) {
    SmRoomPlacement v; memcpy(&v,p,sizeof(v));
    if (v.reserved || v.room<0x8000 || v.room==h.root || v.state<0x8000 || v.width<=0 || v.width>4096 || v.width%256 ||
        v.height<=0 || v.height>4096 || v.height%256 || v.left>=v.right || v.top>=v.bottom ||
        v.x < -65536 || v.x>65536 || v.y < -65536 || v.y>65536) return false;
    if (!SmRoomRuntimeValidate(p+sizeof(v),SmRoomRuntimeStateSize(),v.room,v.state,h.logic!=0)) return false;
  }
  return true;
}
bool SmRoomsLoad(const void *in, size_t size) {
  if (!SmRoomsValidate(in,size)) return false;
  RoomsState h; memcpy(&h,in,sizeof(h));
  Resident next[SM_ROOM_RESIDENTS]={0}; bool ok=true;
  const uint8_t *p=(const uint8_t *)in+sizeof(h);
  /* Recreate only immutable ROM metadata; restoration replaces the entire
   * private machine before any guest or renderer can observe it. */
  uint8_t *seed=calloc(1,0x20000); if (!seed) return false;
  for (unsigned i=0; i<h.count; ++i,p+=h.unit) {
    memcpy(&next[i].placement,p,sizeof(SmRoomPlacement));
    const SmRoomPlacement *v=&next[i].placement;
    SmWorldRoom room={.header=v->room,.width=(uint8_t)(v->width/256),.height=(uint8_t)(v->height/256)};
    room.area=read_rom(NULL,0x8f0001u+v->room);
    next[i].runtime=SmRoomRuntimeCreateConfigured(read_rom,NULL,&room,v->state,v->incoming,seed,h.logic!=0);
    if (!next[i].runtime || !SmRoomRuntimeLoad(next[i].runtime,p+sizeof(SmRoomPlacement),SmRoomRuntimeStateSize())) { ok=false; break; }
  }
  free(seed);
  if (!ok) { for (unsigned i=0; i<h.count; ++i) SmRoomRuntimeDestroy(next[i].runtime); return false; }
  SmRoomsReset(); memcpy(residents,next,sizeof(next)); count=h.count; root=h.root;
  logic=h.logic!=0; seamless=h.seamless!=0;
  pending_entry=h.pending_entry!=0;
  camera_dx=h.camera_dx; camera_dy=h.camera_dy;
  return publish();
}
