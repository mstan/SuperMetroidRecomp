#include "sm_rooms.h"
#include "sm_room_runtime.h"
#include "sm_room_preview.h"
#include "sm_door.h"
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
enum { MAX_LINKS=1024 };
typedef struct DoorLink { uint16_t a,b,state_a,state_b; uint8_t phase_a,phase_b,reserved[2]; } DoorLink;
static DoorLink links[MAX_LINKS];
static unsigned link_count;
static SmRoomDoorEvent door_event;
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
  memset(links,0,sizeof(links)); link_count=0;
  free(publication); publication=NULL;
}
void SmRoomsSetRom(const uint8_t *image, size_t bytes) {
  SmRoomsReset(); rom=image; rom_bytes=bytes; SmRoomPreviewSetRom(image,bytes);
}
const SmRoomScene *SmRoomsScene(void) { return publication; }
void SmRoomsSetDoorEvent(SmRoomDoorEvent event) { door_event=event; }
typedef struct DoorContext { const uint8_t *ram; SmRoomRuntime *runtime; unsigned room,state; int x,y; } DoorContext;
static bool door_change(const DoorContext *c,unsigned x,unsigned y,unsigned orientation,bool closing) {
  if (c->runtime) return closing ? SmRoomRuntimeDoorClose(c->runtime,x,y,orientation) :
      SmRoomRuntimeDoorOpen(c->runtime,x,y,orientation);
  return door_event && door_event(x,y,orientation,closing);
}
static bool mirror_doors(uint8_t active[0x20000]) {
  DoorContext contexts[SM_ROOM_RESIDENTS+1]={{active,NULL,word(active+0x79b),word(active+0x7bb),0,0}};
  for (unsigned i=0; i<count; ++i) {
    const SmRoomPlacement *p=&residents[i].placement;
    contexts[i+1]=(DoorContext){SmRoomRuntimeRam(residents[i].runtime),residents[i].runtime,p->room,p->state,p->x,p->y};
  }
  DoorLink next[MAX_LINKS]={0}; unsigned next_count=0;
  static const int dx[4]={1,-1,0,0},dy[4]={0,0,1,-1};
  for (unsigned i=0; i<=count; ++i) {
    const DoorContext *a=&contexts[i]; unsigned n=0;
    const SmWorldDoor *doors=SmRoomPreviewDoors((uint16_t)a->room,&n);
    for (unsigned d=0; d<n; ++d) {
      const SmWorldDoor *out=&doors[d]; unsigned dir=out->direction&3;
      if ((out->flags&0x80) || out->block_x<0 || !out->reciprocal) continue;
      int target_x=a->x+(out->block_x/16+dx[dir]-out->arrival_x/16)*256;
      int target_y=a->y+(out->block_y/16+dy[dir]-out->arrival_y/16)*256;
      for (unsigned j=0; j<=count; ++j) {
        const DoorContext *b=&contexts[j];
        if (b->room!=out->destination || b->x!=target_x || b->y!=target_y) continue;
        unsigned back_count=0; const SmWorldDoor *back=SmRoomPreviewDoors((uint16_t)b->room,&back_count),*reverse=NULL;
        for (unsigned k=0; k<back_count; ++k) {
          const SmWorldDoor *candidate=&back[k]; unsigned back_dir=candidate->direction&3;
          if (candidate->destination!=a->room || back_dir!=(dir^1) || candidate->block_x<0 ||
              (candidate->flags&0x80) || abs((int)candidate->arrival_x-out->block_x)>3 ||
              abs((int)candidate->arrival_y-out->block_y)>3) continue;
          if (b->x+(candidate->block_x/16+dx[back_dir]-candidate->arrival_x/16)*256==a->x &&
              b->y+(candidate->block_y/16+dy[back_dir]-candidate->arrival_y/16)*256==a->y) { reverse=candidate; break; }
        }
        if (!reverse || out->definition>=reverse->definition) continue;
        if (next_count==MAX_LINKS) return false;
        unsigned ax=reverse->arrival_x,ay=reverse->arrival_y,bx=out->arrival_x,by=out->arrival_y;
        DoorLink current={out->definition,reverse->definition,(uint16_t)a->state,(uint16_t)b->state,
            (uint8_t)SmDoorPhase(read_rom,NULL,a->ram,ax,ay),(uint8_t)SmDoorPhase(read_rom,NULL,b->ram,bx,by),{0,0}};
        const DoorLink *old=NULL;
        for (unsigned k=0; k<link_count; ++k) if (links[k].a==current.a && links[k].b==current.b &&
            links[k].state_a==current.state_a && links[k].state_b==current.state_b) { old=&links[k]; break; }
        bool changed_a=!old || old->phase_a!=current.phase_a,changed_b=!old || old->phase_b!=current.phase_b;
        const DoorContext *target=NULL; unsigned x=0,y=0,orientation=0; bool closing=false;
        if (changed_a && (current.phase_a==SM_DOOR_CLOSING ||
            (old && current.phase_a==SM_DOOR_SHUT && old->phase_a!=SM_DOOR_SHUT && old->phase_a!=SM_DOOR_CLOSING))) {
          target=b; x=bx; y=by; orientation=dir^1; closing=true;
        } else if (changed_b && (current.phase_b==SM_DOOR_CLOSING ||
            (old && current.phase_b==SM_DOOR_SHUT && old->phase_b!=SM_DOOR_SHUT && old->phase_b!=SM_DOOR_CLOSING))) {
          target=a; x=ax; y=ay; orientation=dir; closing=true;
        } else if (changed_a && (current.phase_a==SM_DOOR_OPENING || current.phase_a==SM_DOOR_OPEN)) {
          target=b; x=bx; y=by; orientation=dir^1;
        } else if (changed_b && (current.phase_b==SM_DOOR_OPENING || current.phase_b==SM_DOOR_OPEN)) {
          target=a; x=ax; y=ay; orientation=dir;
        }
        if (target) {
          unsigned phase=SmDoorPhase(read_rom,NULL,target->ram,x,y);
          bool needs=closing ? phase!=SM_DOOR_SHUT && phase!=SM_DOOR_CLOSING : phase!=SM_DOOR_OPEN && phase!=SM_DOOR_OPENING;
          if (needs && !door_change(target,x,y,orientation,closing)) return false;
        }
        current.phase_a=(uint8_t)SmDoorPhase(read_rom,NULL,a->ram,ax,ay);
        current.phase_b=(uint8_t)SmDoorPhase(read_rom,NULL,b->ram,bx,by);
        next[next_count++]=current;
      }
    }
  }
  memcpy(links,next,next_count*sizeof(*next)); link_count=next_count; return true;
}
bool SmRoomsDoor(uint16_t definition, SmRoomPlacement *destination) {
  if (!destination) return false;
  for (unsigned i=0; i<count; ++i) if (residents[i].placement.incoming==definition) {
    *destination=residents[i].placement; return true;
  }
  return false;
}
unsigned SmRoomsDoorPhase(uint16_t definition) {
  for (unsigned i=0; i<count; ++i) if (residents[i].placement.incoming==definition)
    return SmDoorPhase(read_rom,NULL,SmRoomRuntimeRam(residents[i].runtime),
        read_rom(NULL,0x830004u+definition),read_rom(NULL,0x830005u+definition));
  return SM_DOOR_SHUT;
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
      if (!SmRoomRuntimePromoteDoors(residents[i].runtime,active)) { SmRoomRuntimeDestroy(departing); return false; }
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
bool SmRoomsFrame(uint8_t active[0x20000], bool enabled, unsigned extra,
                    bool requested_logic, bool requested_seamless) {
  if (!enabled || !rom || rom_bytes!=0x300000) { SmRoomsReset(); return true; }
  unsigned game_state=word(active+0x998);
  if (game_state>=9 && game_state<=13) pending_entry=true;
  if (game_state!=8) return true;
  unsigned current=word(active+0x79b);
  if (word(active+0xa78)) {
    /* Power bombs freeze actors, but the cartridge still processes door PLMs.
     * Mirror and animate those faces without advancing dormant gameplay/FX. */
    if (current!=root) return true;
    if (!mirror_doors(active)) return false;
    for (unsigned i=0; i<count; ++i) {
      SmRoomRuntimeProgress(residents[i].runtime,active);
      if (!SmRoomRuntimeStepDoors(residents[i].runtime)) return false;
      SmRoomRuntimeDoorBits(residents[i].runtime,active);
    }
    return publish();
  }
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
    if (!runtime || *SmRoomRuntimeError(runtime)) {
      fprintf(stderr,"[sm-rooms] root=%04X resident=%04X state=%04X: %s\n",root,p.room,p.state,SmRoomRuntimeError(runtime));
      SmRoomRuntimeDestroy(runtime); ok=false; break;
    }
    residents[count++]=(Resident){p,runtime};
  }
  for (unsigned i=0; i<old_count; ++i) SmRoomRuntimeDestroy(old[i].runtime);
  if (!ok || !mirror_doors(active)) return false;
  for (unsigned i=0; i<count; ++i) {
    const SmRoomPlacement *p=&residents[i].placement; SmRoomRuntime *runtime=residents[i].runtime;
    SmRoomRuntimeProgress(runtime,active);
    if (!SmRoomRuntimeStepVisible(runtime,(int)word(active+0xaf6)-p->x,(int)word(active+0xafa)-p->y,
        x-p->x,y-p->y,extra) || !SmRoomRuntimeStepEffects(runtime)) {
      fprintf(stderr,"[sm-rooms] root=%04X resident=%04X: %s\n",root,p->room,SmRoomRuntimeError(runtime)); return false;
    }
    SmRoomRuntimeDoorBits(runtime,active);
  }
  return ok && publish();
}

typedef struct RoomsState {
  uint32_t magic,version,count,root,logic,seamless,unit,pending_entry;
  int32_t camera_dx,camera_dy;
  uint32_t links;
} RoomsState;
size_t SmRoomsStateBound(void) {
  return sizeof(RoomsState)+SM_ROOM_RESIDENTS*(sizeof(SmRoomPlacement)+SmRoomRuntimeStateSize())+sizeof(links);
}
size_t SmRoomsSave(void *out, size_t capacity) {
  size_t unit=sizeof(SmRoomPlacement)+SmRoomRuntimeStateSize(), bytes=sizeof(RoomsState)+count*unit+link_count*sizeof(DoorLink);
  if (!out || capacity<bytes) return 0;
  RoomsState h={0x534d5257,4,count,root,logic,seamless,(uint32_t)unit,pending_entry,camera_dx,camera_dy,link_count};
  memcpy(out,&h,sizeof(h)); uint8_t *p=(uint8_t *)out+sizeof(h);
  for (unsigned i=0; i<count; ++i,p+=unit) {
    memcpy(p,&residents[i].placement,sizeof(SmRoomPlacement));
    if (!SmRoomRuntimeSave(residents[i].runtime,p+sizeof(SmRoomPlacement),SmRoomRuntimeStateSize())) return 0;
  }
  memcpy(p,links,link_count*sizeof(DoorLink));
  return bytes;
}
bool SmRoomsValidate(const void *in, size_t size) {
  RoomsState h;
  if (!in || size<sizeof(h)) return false;
  memcpy(&h,in,sizeof(h));
  if (h.magic!=0x534d5257 || h.version!=4 || h.count>SM_ROOM_RESIDENTS || h.root>65535 || h.links>MAX_LINKS ||
      h.camera_dx < -4096 || h.camera_dx>4096 || h.camera_dy < -4096 || h.camera_dy>4096 ||
      h.logic>1 || h.seamless>1 || h.pending_entry>1 || h.unit!=sizeof(SmRoomPlacement)+SmRoomRuntimeStateSize() ||
      size!=sizeof(h)+(size_t)h.count*h.unit+h.links*sizeof(DoorLink)) return false;
  const uint8_t *p=(const uint8_t *)in+sizeof(h);
  for (unsigned i=0; i<h.count; ++i,p+=h.unit) {
    SmRoomPlacement v; memcpy(&v,p,sizeof(v));
    if (v.reserved || v.room<0x8000 || v.room==h.root || v.state<0x8000 || v.width<=0 || v.width>4096 || v.width%256 ||
        v.height<=0 || v.height>4096 || v.height%256 || v.left>=v.right || v.top>=v.bottom ||
        v.x < -65536 || v.x>65536 || v.y < -65536 || v.y>65536) return false;
    if (!SmRoomRuntimeValidate(p+sizeof(v),SmRoomRuntimeStateSize(),v.room,v.state,h.logic!=0)) return false;
  }
  for (unsigned i=0; i<h.links; ++i) {
    DoorLink link; memcpy(&link,p+i*sizeof(link),sizeof(link));
    if (link.a<0x88fc || link.b>=0xacf0 || link.a>=link.b || link.state_a<0x8000 || link.state_b<0x8000 ||
        link.phase_a>SM_DOOR_CLOSING || link.phase_b>SM_DOOR_CLOSING || link.reserved[0] || link.reserved[1]) return false;
    for (unsigned j=0; j<i; ++j) {
      DoorLink other; memcpy(&other,p+j*sizeof(other),sizeof(other));
      if (other.a==link.a && other.b==link.b && other.state_a==link.state_a && other.state_b==link.state_b) return false;
    }
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
  link_count=h.links; memcpy(links,p,link_count*sizeof(DoorLink));
  return publish();
}
