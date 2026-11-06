#include "sm_rooms.h"
#include "sm_room_runtime.h"
#include "sm_room_preview.h"
#include "sm_door.h"
#include "sm_shot_view.h"
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
        bool cap_a=SmDoorHasArrivalCap(reverse->direction),cap_b=SmDoorHasArrivalCap(out->direction);
        DoorLink current={out->definition,reverse->definition,(uint16_t)a->state,(uint16_t)b->state,
            (uint8_t)(cap_a ? SmDoorPhase(read_rom,NULL,a->ram,ax,ay) : SM_DOOR_OPEN),
            (uint8_t)(cap_b ? SmDoorPhase(read_rom,NULL,b->ram,bx,by) : SM_DOOR_OPEN),{0,0}};
        /* Retain the geometric link even for an uncapped entrance. Its
         * constant open phase must not unlock an authored cap on the other
         * side or insert a blue PLM into a placeholder room coordinate. */
        if (!cap_a || !cap_b) { next[next_count++]=current; continue; }
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
  for (unsigned i=0; i<count; ++i) if (residents[i].placement.incoming==definition) {
    if (!SmDoorHasArrivalCap(read_rom(NULL,0x830003u+definition))) return SM_DOOR_OPEN;
    return SmDoorPhase(read_rom,NULL,SmRoomRuntimeRam(residents[i].runtime),
        read_rom(NULL,0x830004u+definition),read_rom(NULL,0x830005u+definition));
  }
  return SM_DOOR_SHUT;
}
/* Only regular, reciprocal screen-edge hatches have this planar contract.
 * Sand falls, elevators and special warp headers retain their native path. */
static bool planar_shape(const SmWorldDoor *d) {
  unsigned dir=d->direction&3;
  return d->reciprocal && !(d->flags&0x80) && d->block_x>=0 &&
      (dir<2 ? (unsigned)d->block_x%16 : (unsigned)d->block_y%16)==(dir==0 || dir==2 ? 15u : 0u);
}
static bool linked_door(uint16_t definition) {
  for (unsigned i=0; i<link_count; ++i)
    if (links[i].a==definition || links[i].b==definition) return true;
  return false;
}
static bool planar_portal(const SmWorldDoor *d,SmRoomPlacement *p) {
  unsigned dir=d->direction&3;
  if (!planar_shape(d)) return false;
  bool paired=false;
  for (unsigned i=0; i<link_count; ++i) if (links[i].a==d->definition || links[i].b==d->definition) { paired=true; break; }
  if (!paired) return false;
  static const int dx[4]={1,-1,0,0},dy[4]={0,0,1,-1};
  int x=(d->block_x/16+dx[dir]-d->arrival_x/16)*256;
  int y=(d->block_y/16+dy[dir]-d->arrival_y/16)*256;
  for (unsigned i=0; i<count; ++i) {
    const SmRoomPlacement *v=&residents[i].placement;
    if (v->room==d->destination && v->x==x && v->y==y) {
      *p=*v; p->incoming=d->definition; return true;
    }
  }
  return false;
}
static bool active_door(const uint8_t *ram,const SmWorldDoor *d) {
  unsigned w=word(ram+0x7a5),h=word(ram+0x7a7);
  if (d->block_x<0 || d->block_y<0 || (unsigned)d->block_x>=w || (unsigned)d->block_y>=h) return false;
  unsigned block=(unsigned)d->block_y*w+(unsigned)d->block_x;
  return (word(ram+0x10002+2*block)>>12)==9 && (ram[0x16402+block]&127)==d->index;
}
static bool interaction_entrance(const uint8_t *ram,unsigned incoming,unsigned phase) {
  if (phase==SM_DOOR_SHUT || phase==SM_DOOR_CLOSING) return false;
  if (!SmDoorHasArrivalCap(read_rom(NULL,0x830003u+incoming))) return true;
  unsigned x=read_rom(NULL,0x830004u+incoming),y=read_rom(NULL,0x830005u+incoming);
  unsigned w=word(ram+0x7a5),h=word(ram+0x7a7);
  if (x>=w || y>=h) return false;
  /* A blue opening PLM can remain resident while its blocks are already air.
   * Physical hatch collision, rather than PLM lifetime, admits interaction. */
  unsigned type=word(ram+0x10002+2*(y*w+x))>>12;
  return type==0 || type==4;
}
static void interaction_rooms(const uint8_t *active,bool reachable[SM_ROOM_RESIDENTS+1]) {
  DoorContext contexts[SM_ROOM_RESIDENTS+1]={{active,NULL,root,word(active+0x7bb),0,0}};
  memset(reachable,0,(SM_ROOM_RESIDENTS+1)*sizeof(*reachable));reachable[0]=true;
  for (unsigned i=0; i<count; ++i) {
    const SmRoomPlacement *p=&residents[i].placement;
    contexts[i+1]=(DoorContext){SmRoomRuntimeRam(residents[i].runtime),residents[i].runtime,p->room,p->state,p->x,p->y};
  }
  for (unsigned pass=0; pass<count; ++pass) {
    bool changed=false;
    for (unsigned i=0; i<link_count; ++i) {
      const DoorLink *link=&links[i];
      unsigned room_a=read_rom(NULL,0x830000u+link->b) | read_rom(NULL,0x830001u+link->b)<<8;
      unsigned room_b=read_rom(NULL,0x830000u+link->a) | read_rom(NULL,0x830001u+link->a)<<8;
      int a=-1,b=-1;
      for (unsigned j=0; j<=count; ++j) {
        if (contexts[j].room==room_a && contexts[j].state==link->state_a) a=(int)j;
        if (contexts[j].room==room_b && contexts[j].state==link->state_b) b=(int)j;
      }
      if (a<0 || b<0 || reachable[a]==reachable[b]) continue;
      if (!interaction_entrance(contexts[a].ram,link->b,link->phase_a) ||
          !interaction_entrance(contexts[b].ram,link->a,link->phase_b)) continue;
      /* Links also describe scenery behind inactive/special exits. Gameplay
       * requires the actual current-state planar type-9 anchors on both sides. */
      unsigned n=0;const SmWorldDoor *out=SmRoomPreviewDoors((uint16_t)room_a,&n),*door_a=NULL,*door_b=NULL;
      for (unsigned j=0; j<n; ++j) if (out[j].definition==link->a) door_a=&out[j];
      out=SmRoomPreviewDoors((uint16_t)room_b,&n);
      for (unsigned j=0; j<n; ++j) if (out[j].definition==link->b) door_b=&out[j];
      if (!door_a || !door_b || !planar_shape(door_a) || !planar_shape(door_b) ||
          !active_door(contexts[a].ram,door_a) || !active_door(contexts[b].ram,door_b)) continue;
      reachable[a]=reachable[b]=true;changed=true;
    }
    if (!changed) break;
  }
}
static unsigned resident_shots(const uint8_t *active,const SmRoomPlacement *p) {
  unsigned mask=0;
  for (unsigned i=0; i<10; ++i) {
    if (!word(active+0xc18+2*i) || !word(active+0xc40+2*i) || (word(active+0xc04+2*i)&16)) continue;
    int x=(int16_t)word(active+0xb64+2*i),y=(int16_t)word(active+0xb78+2*i);
    if (x>=p->x && y>=p->y && x<p->x+p->width && y<p->y+p->height &&
        x>=p->left && y>=p->top && x<p->right && y<p->bottom) mask|=1u<<i;
  }
  return mask;
}
static bool resident_body(const uint8_t *active,const SmRoomPlacement *p) {
  int x=(int16_t)word(active+0xaf6),y=(int16_t)word(active+0xafa);
  int rx=(int)word(active+0xafe),ry=(int)word(active+0xb00);
  return x+rx>p->x && y+ry>p->y && x-rx<p->x+p->width && y-ry<p->y+p->height &&
      x+rx>p->left && y+ry>p->top && x-rx<p->right && y-ry<p->bottom;
}
bool SmRoomsShotsActive(const uint8_t active[0x20000]) {
  if (!rom || word(active+0x79b)!=root || word(active+0x998)!=8) return false;
  unsigned index=word(active+0xdde);
  if (index>=20 || (index&1) || !word(active+0xc40+index)) return false;
  unsigned category=word(active+0xc18+index)&0xf00;
  return category<0x300; /* beams, missiles and supers; explosions have other paths */
}
bool SmRoomsShotDoorAir(const uint8_t ram[0x20000],int origin_x,int origin_y,unsigned block) {
  unsigned w=word(ram+0x7a5),h=word(ram+0x7a7);
  if (!w || block>=w*h || (word(ram+0x10002+2*block)>>12)!=9) return false;
  int bx=(int)(block%w),by=(int)(block/w);
  unsigned n=0; const SmWorldDoor *doors=SmRoomPreviewDoors((uint16_t)word(ram+0x79b),&n);
  static const int dx[4]={1,-1,0,0},dy[4]={0,0,1,-1};
  for (unsigned i=0; i<n; ++i) {
    const SmWorldDoor *d=&doors[i]; unsigned dir=d->direction&3;
    bool span=dir<2 ? bx==d->block_x && by>=d->block_y && by<d->block_y+4 :
        by==d->block_y && bx>=d->block_x && bx<d->block_x+4;
    if (!span || !planar_shape(d) || !active_door(ram,d) || !linked_door(d->definition)) continue;
    int x=origin_x+(d->block_x/16+dx[dir]-d->arrival_x/16)*256;
    int y=origin_y+(d->block_y/16+dy[dir]-d->arrival_y/16)*256;
    if (d->destination==root && !x && !y) return true;
    for (unsigned j=0; j<count; ++j) {
      const SmRoomPlacement *p=&residents[j].placement;
      if (p->room==d->destination && p->x==x && p->y==y) return true;
    }
  }
  return false;
}
typedef struct ShotOrigin { int x,y; } ShotOrigin;
static bool resident_shot_air(void *context,const uint8_t ram[0x20000],unsigned block) {
  const ShotOrigin *p=context;
  return SmRoomsShotDoorAir(ram,p->x,p->y,block);
}
int SmRoomsShotBlock(uint8_t active[0x20000],bool horizontal,uint16_t *index) {
  int bx,by;
  if (!index || !SmRoomsShotsActive(active) || !SmShotSample(active,horizontal,&bx,&by)) return -1;
  int x=bx*16+8,y=by*16+8;
  for (unsigned i=0; i<count; ++i) {
    const SmRoomPlacement *p=&residents[i].placement;
    if (x<p->x || y<p->y || x>=p->x+p->width || y>=p->y+p->height ||
        x<p->left || x>=p->right || y<p->top || y>=p->bottom) continue;
    ShotOrigin origin={p->x,p->y};
    return SmRoomRuntimeShot(residents[i].runtime,active,p->x,p->y,
        bx-p->x/16,by-p->y/16,horizontal,resident_shot_air,&origin);
  }
  int w=(int)word(active+0x7a5),h=(int)word(active+0x7a7);
  if (bx>=0 && by>=0 && bx<w && by<h) {
    unsigned block=(unsigned)(by*w+bx);
    *index=(uint16_t)(block*2);
    if (!SmRoomsShotDoorAir(active,0,0,block)) return -1;
    return 0;
  }
  /* A room allocation ends at real walls, except at a registered connection.
   * Do not let an unsigned row/column alias hit an unrelated native block. */
  SmShotPut(active,0x28,(int)word(active+0x28)-1);
  return 1;
}
static int tile_collision(const uint8_t *ram,int x,int y) {
  int w=(int)word(ram+0x7a5),h=(int)word(ram+0x7a7);
  for (unsigned i=0; i<16; ++i) {
    if (x<0 || y<0 || x>=w || y>=h) return 16;
    unsigned block=(unsigned)(y*w+x),type=word(ram+0x10002+2*block)>>12;
    int bts=(int8_t)ram[0x16402+block];
    if (type==5 && bts) { x+=bts; continue; }
    if (type==13 && bts) { y+=bts; continue; }
    return type==0 || type==2 || type==3 || type==4 || type==6 || type==7 || type==9 ? 0 : 16;
  }
  return 16;
}
int SmRoomsCollisionBlock(const uint8_t active[0x20000],int bx,int by) {
  if (!seamless || word(active+0x79b)!=root || word(active+0x998)!=8) return -1;
  int w=(int)word(active+0x7a5),h=(int)word(active+0x7a7);
  unsigned n=0; const SmWorldDoor *doors=SmRoomPreviewDoors((uint16_t)root,&n);
  for (unsigned i=0; i<n; ++i) {
    const SmWorldDoor *d=&doors[i]; unsigned dir=d->direction&3; SmRoomPlacement p;
    bool span=dir<2 ? by>=d->block_y && by<d->block_y+4 : bx>=d->block_x && bx<d->block_x+4;
    bool near=dir==0 ? bx>=d->block_x && bx<=d->block_x+3 :
        dir==1 ? bx<=d->block_x && bx>=d->block_x-3 :
        dir==2 ? by>=d->block_y && by<=d->block_y+3 : by<=d->block_y && by>=d->block_y-3;
    if (!span || !near || !active_door(active,d) || !planar_portal(d,&p)) continue;
    /* The source trigger itself becomes ordinary hatch air. Beyond its
     * plane sample the destination's real cap/wall cells, including when
     * authored room allocations overlap. */
    bool source=dir<2 ? bx==d->block_x : by==d->block_y;
    if (source) return 0;
    for (unsigned j=0; j<count; ++j) if (residents[j].placement.room==p.room &&
        residents[j].placement.x==p.x && residents[j].placement.y==p.y)
      return tile_collision(SmRoomRuntimeRam(residents[j].runtime),bx-p.x/16,by-p.y/16);
  }
  return bx<0 || by<0 || bx>=w || by>=h ? 16 : -1;
}
bool SmRoomsCrossing(const uint8_t active[0x20000],uint16_t *definition,SmRoomPlacement *destination) {
  if (!seamless || word(active+0x79b)!=root || word(active+0x998)!=8) return false;
  int x=(int16_t)word(active+0xaf6),y=(int16_t)word(active+0xafa);
  unsigned n=0; const SmWorldDoor *doors=SmRoomPreviewDoors((uint16_t)root,&n);
  for (unsigned i=0; i<n; ++i) {
    const SmWorldDoor *d=&doors[i]; unsigned dir=d->direction&3; SmRoomPlacement p;
    int plane=dir<2 ? (d->block_x+(dir==0))*16 : (d->block_y+(dir==2))*16;
    bool span=dir<2 ? y>=d->block_y*16 && y<(d->block_y+4)*16 : x>=d->block_x*16 && x<(d->block_x+4)*16;
    bool crossed=dir==0 ? x>=plane : dir==1 ? x<plane : dir==2 ? y>=plane : y<plane;
    if (!span || !crossed || !active_door(active,d) || !planar_portal(d,&p) || x-p.x<0 || x-p.x>=p.width || y-p.y<0 || y-p.y>=p.height) continue;
    *definition=d->definition; *destination=p; return true;
  }
  return false;
}
bool SmRoomsUpwardDoorCarry(const uint8_t active[0x20000]) {
  if (!seamless || word(active+0x79b)!=root || word(active+0x998)!=8 || word(active+0xb36)!=1) return false;
  int x=(int16_t)word(active+0xaf6),y=(int16_t)word(active+0xafa);
  int rx=(int)word(active+0xafe),ry=(int)word(active+0xb00);
  unsigned n=0; const SmWorldDoor *doors=SmRoomPreviewDoors((uint16_t)root,&n);
  for (unsigned i=0; i<n; ++i) {
    const SmWorldDoor *d=&doors[i]; SmRoomPlacement p;
    int plane=d->block_y*16;
    if ((d->direction&3)!=3 || y<plane || y-ry>plane ||
        x-rx<d->block_x*16 || x+rx>=(d->block_x+4)*16 ||
        !active_door(active,d) || !planar_portal(d,&p)) continue;
    unsigned phase=SmRoomsDoorPhase(d->definition);
    if (phase!=SM_DOOR_OPEN && phase!=SM_DOOR_OPENING) continue;
    /* Vanilla up-doors capture the leading edge, so an ordinary Hi-Jump
     * can reach the trigger without its center clearing the outer plane.
     * Continue that accepted travel over real gameplay ticks instead of
     * introducing a coordinate jump or demanding a higher jump. */
    int by=d->block_y-1;
    if (SmRoomsCollisionBlock(active,(x-rx)/16,by)==0 &&
        SmRoomsCollisionBlock(active,(x+rx-1)/16,by)==0) return true;
  }
  return false;
}
static void follow_doorway(const uint8_t *active,unsigned extra) {
  if (!seamless) return;
  int px=(int16_t)word(active+0xaf6),py=(int16_t)word(active+0xafa);
  unsigned n=0; const SmWorldDoor *doors=SmRoomPreviewDoors((uint16_t)root,&n);
  for (unsigned i=0; i<n; ++i) {
    const SmWorldDoor *d=&doors[i]; unsigned dir=d->direction&3; SmRoomPlacement p;
    int plane=dir<2 ? (d->block_x+(dir==0))*16 : (d->block_y+(dir==2))*16;
    bool span=dir<2 ? py>=d->block_y*16 && py<(d->block_y+4)*16 : px>=d->block_x*16 && px<(d->block_x+4)*16;
    if (!span || abs((dir<2 ? px : py)-plane)>96 ||
        !active_door(active,d) || !planar_portal(d,&p)) continue;
    int sx=px-(int16_t)word(active+0x911)-camera_dx;
    int sy=py-(int16_t)word(active+0x915)-camera_dy;
    int left=8+(int)word(active+0xafe)-(int)extra;
    int right=256-left,top=32+(int)word(active+0xb00),bottom=224-(int)word(active+0xb00);
    camera_dx+=sx<left ? sx-left : sx>right ? sx-right : 0;
    camera_dy+=sy<top ? sy-top : sy>bottom ? sy-bottom : 0;
    return;
  }
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
      if (residents[i].placement.state==word(active+0x7bb)) {
        if (!SmRoomRuntimePromoteDoors(residents[i].runtime,active)) { SmRoomRuntimeDestroy(departing); return false; }
        SmRoomRuntimePromote(residents[i].runtime,active,vram);
      }
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
  follow_doorway(active,extra);
  int x=(int16_t)word(active+0x911)+camera_dx, y=(int16_t)word(active+0x915)+camera_dy;
  /* Match native projectile retention beyond the visible edges. A shot must
   * not lose its room's collision just before its ordinary offscreen cull. */
  SmRoomPreviewPrepare(active,x-(int)extra-64,y-64,x+256+(int)extra+64,y+288);
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
  bool reachable[SM_ROOM_RESIDENTS+1];interaction_rooms(active,reachable);
  for (unsigned i=0; i<count; ++i) {
    const SmRoomPlacement *p=&residents[i].placement; SmRoomRuntime *runtime=residents[i].runtime;
    SmRoomRuntimeProgress(runtime,active);
    if ((reachable[i+1] && !SmRoomRuntimeHitShots(runtime,active,p->x,p->y,resident_shots(active,p))) ||
        (reachable[i+1] && resident_body(active,p) && !SmRoomRuntimeTouch(runtime,active,p->x,p->y)) ||
        !SmRoomRuntimeStepVisible(runtime,(int16_t)word(active+0xaf6)-p->x,(int16_t)word(active+0xafa)-p->y,
        x-p->x,y-p->y,extra) ||
        (reachable[i+1] && !SmRoomRuntimeProjectiles(runtime,active,p->x,p->y,resident_shots(active,p),resident_body(active,p))) ||
        !SmRoomRuntimeStepEffects(runtime)) {
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
