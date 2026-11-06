#include "sm_rooms.h"
#include "sm_room_runtime.h"
#include "sm_room_preview.h"
#include "sm_door.h"
#include "sm_shot_view.h"
#include "sm_activity.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { WORLD_ENEMIES=32,WORLD_EPROJS=18,WORLD_WEAPONS=10,WORLD_ENTITIES=60 };
enum { WORLD_ENVIRONMENT=1 };
typedef struct WorldEntity { uint16_t definition,room,pending,flags;int16_t x,y; } WorldEntity;
typedef struct WorldEntities { uint32_t loaded,open_rooms;WorldEntity slots[WORLD_ENTITIES]; } WorldEntities;
typedef struct Resident { SmRoomPlacement placement; SmRoomRuntime *runtime;WorldEntities world; } Resident;
static WorldEntities active_world;
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
static SmRoomEnemyReaction enemy_reaction;
static SmRoomScene *publication;
static SmRoomImage *native_root;
static unsigned word(const uint8_t *p) { return p[0] | p[1]<<8; }
static uint8_t read_rom(void *unused, uint32_t a) {
  (void)unused; size_t offset=((a>>16)&127)*32768u+(a&32767);
  return (a&0x8000) && offset<rom_bytes ? rom[offset] : 255;
}
void SmRoomsReset(void) {
  for (unsigned i=0; i<count; ++i) SmRoomRuntimeDestroy(residents[i].runtime);
  memset(residents,0,sizeof(residents)); count=root=0;
  memset(&active_world,0,sizeof(active_world));
  pending_entry=false;
  camera_dx=camera_dy=0; rebased=false;
  memset(links,0,sizeof(links)); link_count=0;
  free(publication); publication=NULL;
  free(native_root); native_root=NULL;
}
void SmRoomsSetRom(const uint8_t *image, size_t bytes) {
  SmRoomsReset(); rom=image; rom_bytes=bytes; SmRoomPreviewSetRom(image,bytes);
}
const SmRoomScene *SmRoomsScene(void) { return publication; }
void SmRoomsSetDoorEvent(SmRoomDoorEvent event) { door_event=event; }
void SmRoomsSetEnemyReaction(SmRoomEnemyReaction reaction) { enemy_reaction=reaction; }
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
static bool planar_portal(const SmWorldDoor *d,SmRoomPlacement *p);
bool SmRoomsDoor(uint16_t definition, SmRoomPlacement *destination) {
  if (!destination) return false;
  unsigned n=0;const SmWorldDoor *doors=SmRoomPreviewDoors((uint16_t)root,&n);
  for (unsigned i=0; i<n; ++i) if (doors[i].definition==definition && planar_portal(&doors[i],destination)) return true;
  for (unsigned i=0; i<count; ++i) if (residents[i].placement.incoming==definition) {
    *destination=residents[i].placement; return true;
  }
  return false;
}
unsigned SmRoomsDoorPhase(uint16_t definition) {
  SmRoomPlacement p;
  if (!SmRoomsDoor(definition,&p)) return SM_DOOR_SHUT;
  for (unsigned i=0; i<count; ++i) if (residents[i].placement.room==p.room &&
      residents[i].placement.x==p.x && residents[i].placement.y==p.y) {
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
      *p=*v;p->incoming=d->definition;
      /* A shared canvas can be exposed by several entrances. Handoff still
       * clips the departing room at this connection's actual plane. */
      int plane=dir<2 ? (d->block_x+(dir==0))*16 : (d->block_y+(dir==2))*16;
      if (dir==0 && p->left<plane) p->left=plane;
      if (dir==1 && p->right>plane) p->right=plane;
      if (dir==2 && p->top<plane) p->top=plane;
      if (dir==3 && p->bottom>plane) p->bottom=plane;
      return true;
    }
  }
  return false;
}
static bool active_trigger(const uint8_t *ram,const SmWorldDoor *d,int x,int y) {
  unsigned w=word(ram+0x7a5),h=word(ram+0x7a7);
  if (x<0 || y<0 || (unsigned)x>=w || (unsigned)y>=h) return false;
  unsigned block=(unsigned)y*w+(unsigned)x;
  return (word(ram+0x10002+2*block)>>12)==9 && (ram[0x16402+block]&127)==d->index;
}
static bool active_door(const uint8_t *ram,const SmWorldDoor *d) {
  return active_trigger(ram,d,d->block_x,d->block_y);
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
static bool world_context(const uint8_t *active,unsigned room,DoorContext *out) {
  if (room==root) { *out=(DoorContext){active,NULL,root,word(active+0x7bb),0,0};return true; }
  for (unsigned i=0; i<count; ++i) if (residents[i].placement.room==room) {
    const Resident *r=&residents[i];
    *out=(DoorContext){SmRoomRuntimeRam(r->runtime),r->runtime,room,r->placement.state,r->placement.x,r->placement.y};return true;
  }
  return false;
}
static bool world_connection(const uint8_t *active,const DoorContext *source,const SmWorldDoor *door,
    DoorContext *destination,bool *open) {
  if (!planar_shape(door) || !active_door(source->ram,door) || !world_context(active,door->destination,destination)) return false;
  static const int dx[4]={1,-1,0,0},dy[4]={0,0,1,-1};unsigned dir=door->direction&3;
  if (destination->x!=source->x+(door->block_x/16+dx[dir]-door->arrival_x/16)*256 ||
      destination->y!=source->y+(door->block_y/16+dy[dir]-door->arrival_y/16)*256) return false;
  for (unsigned i=0; i<link_count; ++i) {
    const DoorLink *link=&links[i];bool a=link->a==door->definition;
    if ((!a && link->b!=door->definition) || source->state!=(a ? link->state_a : link->state_b) ||
        destination->state!=(a ? link->state_b : link->state_a)) continue;
    unsigned opposite=a ? link->b : link->a,n=0;
    const SmWorldDoor *other=SmRoomPreviewDoors((uint16_t)destination->room,&n);
    for (unsigned j=0; j<n; ++j) if (other[j].definition==opposite && planar_shape(&other[j]) && active_door(destination->ram,&other[j])) {
      *open=interaction_entrance(source->ram,opposite,a ? link->phase_a : link->phase_b) &&
          interaction_entrance(destination->ram,door->definition,a ? link->phase_b : link->phase_a);
      return true;
    }
  }
  return false;
}
static void entity_values(const uint8_t *ram,unsigned slot,unsigned *definition,int *x,int *y) {
  unsigned at,px,py;
  if (slot<WORLD_ENEMIES) { at=0xf78+64*slot;px=at+2;py=at+6; }
  else if (slot<WORLD_ENEMIES+WORLD_EPROJS) { at=0x1997+2*(slot-WORLD_ENEMIES);px=at+0xb4;py=at+0xfc; }
  else { unsigned i=2*(slot-WORLD_ENEMIES-WORLD_EPROJS);at=0xc18+i;px=0xb64+i;py=0xb78+i; }
  *definition=word(ram+at);*x=(int16_t)word(ram+px);*y=(int16_t)word(ram+py);
  if ((slot<WORLD_ENEMIES && *definition==0xdaff) ||
      (slot>=WORLD_ENEMIES+WORLD_EPROJS && !word(ram+0xc40+2*(slot-WORLD_ENEMIES-WORLD_EPROJS)))) *definition=0;
}
static unsigned initial_entity_room(const uint8_t *active,const uint8_t *ram,int ox,int oy,int x,int y) {
  unsigned room=word(ram+0x79b);
  if (x>=0 && y>=0 && x<(int)word(ram+0x7a5)*16 && y<(int)word(ram+0x7a7)*16) return room;
  /* When enabling the feature, adopt an already-present out-of-allocation
   * object in its connected namespace. Native births and every subsequent
   * movement have explicit provenance and still require an open hatch. */
  DoorContext source;if (!world_context(active,room,&source)) return room;
  unsigned n=0;const SmWorldDoor *doors=SmRoomPreviewDoors((uint16_t)room,&n);
  int wx=ox+x,wy=oy+y;
  for (unsigned i=0; i<n; ++i) {
    const SmWorldDoor *d=&doors[i];unsigned dir=d->direction&3;
    int plane=dir<2 ? ox+(d->block_x+(dir==0))*16 : oy+(d->block_y+(dir==2))*16;
    int point=dir<2 ? wx : wy,along=dir<2 ? wy : wx,start=dir<2 ? oy+d->block_y*16 : ox+d->block_x*16;
    DoorContext destination;bool open=false;
    if (along>=start && along<start+64 && ((dir==0 || dir==2) ? point>=plane : point<plane) &&
        world_connection(active,&source,d,&destination,&open) && wx>=destination.x && wy>=destination.y &&
        wx<destination.x+(int)word(destination.ram+0x7a5)*16 && wy<destination.y+(int)word(destination.ram+0x7a7)*16)
      return destination.room;
  }
  return room;
}
static void sync_entity(const uint8_t *active,const uint8_t *ram,int ox,int oy,WorldEntity *entity,unsigned slot) {
  unsigned definition;int x,y;entity_values(ram,slot,&definition,&x,&y);
  if (!definition) { memset(entity,0,sizeof(*entity));return; }
  if (!entity->definition || !entity->room || abs(x-entity->x)>128 || abs(y-entity->y)>128) {
    unsigned ep=slot-WORLD_ENEMIES;
    bool environment=slot<WORLD_ENEMIES ? SmRoomEnvironmentEnemy(definition) :
        slot<WORLD_ENEMIES+WORLD_EPROJS && (word(ram+0x1bd7+ep*2)&0x2000) && !word(ram+0x1bb3+ep*2);
    *entity=(WorldEntity){.definition=(uint16_t)definition,.room=(uint16_t)initial_entity_room(active,ram,ox,oy,x,y),
        .flags=environment ? WORLD_ENVIRONMENT : 0,.x=(int16_t)x,.y=(int16_t)y};return;
  }
  if (slot<WORLD_ENEMIES && SmRoomEnvironmentEnemy(entity->definition)!=SmRoomEnvironmentEnemy(definition))
    entity->flags=(uint16_t)((entity->flags&~WORLD_ENVIRONMENT) | (SmRoomEnvironmentEnemy(definition) ? WORLD_ENVIRONMENT : 0));
  entity->definition=(uint16_t)definition;
  if (x==entity->x && y==entity->y) { entity->pending=0;return; }
  DoorContext source;
  if (world_context(active,entity->room,&source)) {
    unsigned n=0;const SmWorldDoor *doors=SmRoomPreviewDoors((uint16_t)source.room,&n);
    for (unsigned i=0; i<n; ++i) {
      const SmWorldDoor *d=&doors[i];unsigned dir=d->direction&3;bool horizontal=dir<2;
      int plane=(horizontal ? source.x+(d->block_x+(dir==0))*16 : source.y+(d->block_y+(dir==2))*16);
      int before=horizontal ? ox+entity->x : oy+entity->y,after=horizontal ? ox+x : oy+y;
      bool crossing=(dir==0 || dir==2) ? before<plane && after>=plane : before>=plane && after<plane;
      if (!crossing || after==before) continue;
      int along_before=horizontal ? oy+entity->y : ox+entity->x,along_after=horizontal ? oy+y : ox+x;
      int along=along_before+(int)((int64_t)(along_after-along_before)*(plane-before)/(after-before));
      int start=horizontal ? source.y+d->block_y*16 : source.x+d->block_x*16;
      DoorContext destination;bool open=false;
      if (along>=start && along<start+64 && world_connection(active,&source,d,&destination,&open) &&
          (open || entity->pending==d->definition)) { entity->room=(uint16_t)destination.room;break; }
    }
  }
  entity->pending=0;entity->x=(int16_t)x;entity->y=(int16_t)y;
}
static void sync_entities(const uint8_t *active,const uint8_t *ram,int ox,int oy,WorldEntities *world,bool weapons) {
  for (unsigned i=0; i<(weapons ? WORLD_ENTITIES : WORLD_ENEMIES+WORLD_EPROJS); ++i)
    sync_entity(active,ram,ox,oy,&world->slots[i],i);
}
static void retain_entity_rooms(const WorldEntities *world,unsigned resource,bool required[SM_ROOM_RESIDENTS]) {
  unsigned birthplace=resource<count ? residents[resource].placement.room : root;
  for (unsigned i=0; i<WORLD_ENTITIES; ++i) {
    const WorldEntity *e=&world->slots[i];
    if (!e->definition || !e->room || e->room==birthplace) continue;
    /* Native slots and graphics stay in their original machine. Retain both
     * that resource pool and the foreign terrain namespace until deletion. */
    if (resource<count) required[resource]=true;
    for (unsigned j=0; j<count; ++j) if (residents[j].placement.room==e->room) required[j]=true;
  }
}
static WorldEntities *resource_entities(const uint8_t *source,int ox,int oy) {
  if (word(source+0x79b)==root && !ox && !oy) return &active_world;
  for (unsigned i=0; i<count; ++i) if (SmRoomRuntimeRam(residents[i].runtime)==source &&
      residents[i].placement.x==ox && residents[i].placement.y==oy) return &residents[i].world;
  return NULL;
}
void SmRoomsEntityBorn(const uint8_t active[0x20000],const uint8_t source[0x20000],int ox,int oy,unsigned slot,unsigned phase) {
  if (!rom || word(active+0x79b)!=root || slot>=WORLD_ENTITIES) return;
  WorldEntities *world=resource_entities(source,ox,oy);if (!world) return;
  unsigned definition;int x,y;entity_values(source,slot,&definition,&x,&y);
  if (!definition) return;
  unsigned parent=WORLD_ENTITIES;
  if (phase==1 && word(source+0xe54)<2048 && !(word(source+0xe54)&63)) parent=word(source+0xe54)/64;
  if (phase==2 && word(source+0x1991)<36 && !(word(source+0x1991)&1)) parent=WORLD_ENEMIES+word(source+0x1991)/2;
  unsigned room=word(source+0x79b),flags=(slot<WORLD_ENEMIES ? SmRoomEnvironmentEnemy(definition) : phase==0) ? WORLD_ENVIRONMENT : 0;
  if (parent<WORLD_ENTITIES && world->slots[parent].definition && parent!=slot) {
    unsigned parent_def;int px,py;entity_values(source,parent,&parent_def,&px,&py);
    if (parent_def) sync_entity(active,source,ox,oy,&world->slots[parent],parent);
    room=world->slots[parent].room;flags|=world->slots[parent].flags&WORLD_ENVIRONMENT;
  }
  world->slots[slot]=(WorldEntity){.definition=(uint16_t)definition,.room=(uint16_t)room,
      .flags=(uint16_t)flags,.x=(int16_t)x,.y=(int16_t)y};
  if (phase==3) {
    world->slots[slot].x=(int16_t)word(source+0xaf6);world->slots[slot].y=(int16_t)word(source+0xafa);
    sync_entity(active,source,ox,oy,&world->slots[slot],slot);
  }
}
static bool visible_entity(const uint8_t *active,const DoorContext *resource,const WorldEntity *entity) {
  if (!entity->definition) return false;
  int x=resource->x+entity->x,y=resource->y+entity->y;DoorContext owner;
  if (entity->room==root) {
    const SmWorldRoom *room=SmRoomPreviewRoom((uint16_t)root);
    if (!room || x<0 || y<0 || x>=room->width*256 || y>=room->height*256) return false;
    for (unsigned i=0; i<count; ++i) {
      const SmRoomPlacement *p=&residents[i].placement;
      if (x>=p->x && y>=p->y && x<p->x+p->width && y<p->y+p->height &&
          x>=p->left && y>=p->top && x<p->right && y<p->bottom) return false;
    }
    return true;
  }
  if (!world_context(active,entity->room,&owner) || x<owner.x || y<owner.y ||
      x>=owner.x+(int)word(owner.ram+0x7a5)*16 || y>=owner.y+(int)word(owner.ram+0x7a7)*16) return false;
  for (unsigned i=0; i<count; ++i) if (residents[i].placement.room==owner.room) {
    const SmRoomPlacement *p=&residents[i].placement;
    return x>=p->left && y>=p->top && x<p->right && y<p->bottom;
  }
  return false;
}
typedef struct ShotOrigin { int x,y; } ShotOrigin;
static bool resident_shot_air(void *context,const uint8_t ram[0x20000],unsigned block) {
  const ShotOrigin *p=context;
  return SmRoomsShotDoorAir(ram,p->x,p->y,block);
}
static bool sample_room(const uint8_t *active,const uint8_t *resource,int ox,int oy,int bx,int by,
    WorldEntity *entity,DoorContext *owner,int *x,int *y,bool *air) {
  unsigned room=entity && entity->room ? entity->room : word(resource+0x79b);
  if (!world_context(active,room,owner)) return false;
  int wx=ox/16+bx,wy=oy/16+by;
  int cx=ox+(entity ? entity->x : 0),cy=oy+(entity ? entity->y : 0);
  bool visited[SM_ROOM_RESIDENTS+1]={0};
  for (unsigned pass=0; pass<=count; ++pass) {
    unsigned key=0;
    if (owner->room!=root) for (unsigned i=0; i<count; ++i) if (residents[i].placement.room==owner->room) key=i+1;
    if (visited[key]) return false;
    visited[key]=true;
    unsigned n=0;const SmWorldDoor *doors=SmRoomPreviewDoors((uint16_t)owner->room,&n);
    bool advanced=false;
    for (unsigned i=0; i<n; ++i) {
      const SmWorldDoor *d=&doors[i];unsigned dir=d->direction&3;bool horizontal=dir<2;
      int plane=horizontal ? owner->x+(d->block_x+(dir==0))*16 : owner->y+(d->block_y+(dir==2))*16;
      int point=horizontal ? wx*16+8 : wy*16+8,centre=horizontal ? cx : cy;
      bool inside=(dir==0 || dir==2) ? centre<plane : centre>=plane;
      bool beyond=(dir==0 || dir==2) ? point>=plane : point<plane;
      int along=horizontal ? wy*16+8 : wx*16+8,start=horizontal ? owner->y+d->block_y*16 : owner->x+d->block_x*16;
      bool trigger=horizontal ? wx==owner->x/16+d->block_x : wy==owner->y/16+d->block_y;
      DoorContext destination;bool open=false;
      if (!inside || (!beyond && !trigger) || along<start || along>=start+64 ||
          !world_connection(active,owner,d,&destination,&open)) continue;
      if (entity && owner->room==entity->room && open) entity->pending=d->definition;
      if (trigger && !beyond) { *x=wx-owner->x/16;*y=wy-owner->y/16;*air=true;return true; }
      *owner=destination;advanced=true;break;
    }
    if (advanced) continue;
    *x=wx-owner->x/16;*y=wy-owner->y/16;*air=false;
    return *x>=0 && *y>=0 && *x<(int)word(owner->ram+0x7a5) && *y<(int)word(owner->ram+0x7a7);
  }
  return false;
}
int SmRoomsGrappleBlock(uint8_t active[0x20000],SmGrappleResult *result) {
  if (!result || !rom || word(active+0x79b)!=root || word(active+0x998)!=8) return -1;
  int bx=SmShotCell((int16_t)word(active+0xd08)),by=SmShotCell((int16_t)word(active+0xd0c));
  WorldEntity player={.room=(uint16_t)root,.x=(int16_t)word(active+0xaf6),.y=(int16_t)word(active+0xafa)};
  DoorContext owner;int x,y;bool air=false;
  if (!sample_room(active,active,0,0,bx,by,&player,&owner,&x,&y,&air)) {
    result->a=result->x=16;result->flags=0x81;return 0; /* native solid response at an unsupported allocation edge */
  }
  unsigned block=(unsigned)y*word(owner.ram+0x7a5)+(unsigned)x;
  if (air || ((word(owner.ram+0x10002+2*block)>>12)==9 && SmRoomsShotDoorAir(owner.ram,owner.x,owner.y,block))) {
    result->a=result->x=0;result->flags=0x80;return 0;
  }
  if (!owner.runtime) return -1;
  return SmRoomRuntimeGrappleBlock(owner.runtime,active,owner.x,owner.y,result) ? 0 : -2;
}
int SmRoomsShotBlock(uint8_t active[0x20000],bool horizontal,uint16_t *index) {
  int bx,by;
  if (!index || !SmRoomsShotsActive(active) || !SmShotSample(active,horizontal,&bx,&by)) return -1;
  unsigned slot=WORLD_ENEMIES+WORLD_EPROJS+word(active+0xdde)/2;
  WorldEntity *entity=&active_world.slots[slot];sync_entity(active,active,0,0,entity,slot);
  DoorContext owner;int x,y;bool air=false;
  if (sample_room(active,active,0,0,bx,by,entity,&owner,&x,&y,&air)) {
    if (air) return 0;
    if (!owner.runtime) { *index=(uint16_t)(2*(y*(int)word(active+0x7a5)+x));return -1; }
    ShotOrigin origin={owner.x,owner.y};
    return SmRoomRuntimeShot(owner.runtime,active,owner.x,owner.y,x,y,horizontal,resident_shot_air,&origin);
  }
  /* A room allocation ends at real walls, except at a registered connection.
   * Do not let an unsigned row/column alias hit an unrelated native block. */
  SmShotPut(active,0x28,(int)word(active+0x28)-1);
  return 1;
}
int SmRoomsEnemyBlock(uint8_t active[0x20000],uint8_t source[0x20000],int ox,int oy,bool horizontal,uint16_t *index) {
  int bx,by;
  if (!rom || word(active+0x79b)!=root || word(active+0x998)!=8 || !index ||
      !SmEnemySample(source,horizontal,&bx,&by)) return -1;
  unsigned slot=word(source+0xe54)/64;WorldEntities *world=resource_entities(source,ox,oy);
  WorldEntity *entity=world ? &world->slots[slot] : NULL;
  if (entity) sync_entity(active,source,ox,oy,entity,slot);
  DoorContext owner;int x,y;bool air=false;
  if (!sample_room(active,source,ox,oy,bx,by,entity,&owner,&x,&y,&air)) return 1;
  if (air) return 0;
  unsigned block=(unsigned)y*word(owner.ram+0x7a5)+(unsigned)x;
  if ((word(owner.ram+0x10002+2*block)>>12)==9 && SmRoomsShotDoorAir(owner.ram,owner.x,owner.y,block)) return 0;
  if (owner.ram==source) {
    *index=(uint16_t)(2*(y*word(source+0x7a5)+x));return -1;
  }
  if (owner.runtime) return SmRoomRuntimeEnemyBlock(owner.runtime,source,ox-owner.x,oy-owner.y,x,y,horizontal);
  return enemy_reaction ? enemy_reaction(source,ox,oy,x,y,horizontal) : -2;
}
bool SmRoomsEprojTile(const uint8_t active[0x20000],const uint8_t source[0x20000],
    int origin_x,int origin_y,int bx,int by,SmEprojTile *tile) {
  if (!rom || !tile || word(active+0x79b)!=root || word(active+0x998)!=8) return false;
  WorldEntities *world=resource_entities(source,origin_x,origin_y);WorldEntity *entity=NULL;
  unsigned index=word(source+0x1991);
  if (world && index<36 && !(index&1) && word(source+0x1997+index)) {
    entity=&world->slots[WORLD_ENEMIES+index/2];sync_entity(active,source,origin_x,origin_y,entity,WORLD_ENEMIES+index/2);
  }
  DoorContext context;int x,y;bool air=false;
  if (!sample_room(active,source,origin_x,origin_y,bx,by,entity,&context,&x,&y,&air)) {
    *tile=(SmEprojTile){bx,by,0x8000,0};return true;
  }
  if (air) { *tile=(SmEprojTile){bx,by,0,0};return true; }
  const uint8_t *owner=context.ram;
  int ox=context.x/16,oy=context.y/16,w=(int)word(owner+0x7a5),h=(int)word(owner+0x7a7);
  bool native=owner==source;
  for (unsigned i=0; i<16; ++i) {
    if (x<0 || y<0 || x>=w || y>=h) break;
    unsigned block=(unsigned)(y*w+x),value=word(owner+0x10002+2*block),type=value>>12;
    uint8_t bts=owner[0x16402+block];
    if (type==9 && SmRoomsShotDoorAir(owner,ox*16,oy*16,block)) value=0;
    else if (native) return false;
    else if (type==5 && bts) { x+=(int8_t)bts;continue; }
    else if (type==13 && bts) { y+=(int8_t)bts;continue; }
    else if (type==5 || type==13) break;
    *tile=(SmEprojTile){ox+x-origin_x/16,oy+y-origin_y/16,(uint16_t)value,bts};
    return true;
  }
  *tile=(SmEprojTile){bx,by,0x8000,0};return true;
}
typedef struct EprojOrigin { uint8_t *active;int x,y;const bool *reachable; } EprojOrigin;
static bool resident_eproj_tile(void *context,const uint8_t source[0x20000],int x,int y,SmEprojTile *tile) {
  const EprojOrigin *origin=context;
  return SmRoomsEprojTile(origin->active,source,origin->x,origin->y,x,y,tile);
}
static int resident_enemy_block(void *context,uint8_t source[0x20000],bool horizontal,uint16_t *index) {
  const EprojOrigin *origin=context;
  return SmRoomsEnemyBlock(origin->active,source,origin->x,origin->y,horizontal,index);
}
static void resident_birth(void *context,const uint8_t *source,unsigned slot,unsigned phase) {
  const EprojOrigin *origin=context;SmRoomsEntityBorn(origin->active,source,origin->x,origin->y,slot,phase);
}
static bool room_reachable(unsigned room,const bool *reachable) {
  if (room==root) return true;
  for (unsigned i=0; i<count; ++i) if (residents[i].placement.room==room) return reachable[i+1];
  return false;
}
unsigned SmRoomsActiveRules(uint8_t active[0x20000],unsigned slot) {
  if (!rom || word(active+0x79b)!=root || word(active+0x998)!=8 || slot>=WORLD_ENEMIES+WORLD_EPROJS)
    return SM_ROOM_SIMULATES|SM_ROOM_INTERACTS;
  if (slot<WORLD_ENEMIES && word(active+0xf78+64*slot)==0xdaff)
    return SM_ROOM_SIMULATES|SM_ROOM_INTERACTS; /* native reserved/death slots */
  WorldEntity *e=&active_world.slots[slot];sync_entity(active,active,0,0,e,slot);
  if (!e->definition) return 0;
  unsigned rules=logic || e->room==root || (e->flags&WORLD_ENVIRONMENT) ? SM_ROOM_SIMULATES : 0;
  if (e->room==root) return rules|SM_ROOM_INTERACTS; /* preserve native own-room scripts/helpers */
  DoorContext resource={active,NULL,root,word(active+0x7bb),0,0};
  if (!visible_entity(active,&resource,e)) return rules;
  if (logic) {
    bool reachable[SM_ROOM_RESIDENTS+1];interaction_rooms(active,reachable);
    if (room_reachable(e->room,reachable)) rules|=SM_ROOM_INTERACTS;
  }
  return rules;
}
bool SmRoomsFilterActiveQueues(uint8_t active[0x20000]) {
  if (!rom || word(active+0x79b)!=root || word(active+0x998)!=8) return true;
  uint32_t simulation=0,interaction=0;
  for (unsigned i=0; i<32; ++i) {
    unsigned rules=SmRoomsActiveRules(active,i);
    if (rules&SM_ROOM_SIMULATES) simulation|=1u<<i;
    if (rules&SM_ROOM_INTERACTS) interaction|=1u<<i;
  }
  return SmActivityFilterQueues(active,simulation,interaction);
}
static bool resident_player_eligible(void *context,const uint8_t source[0x20000],unsigned slot) {
  const EprojOrigin *origin=context;
  WorldEntities *world=resource_entities(source,origin->x,origin->y);
  if (!world || slot>=WORLD_ENEMIES+WORLD_EPROJS) return false;
  WorldEntity *e=&world->slots[slot];
  sync_entity(origin->active,source,origin->x,origin->y,e,slot);
  DoorContext resource={source,NULL,word(source+0x79b),word(source+0x7bb),origin->x,origin->y};
  return visible_entity(origin->active,&resource,e) &&
      (e->room==root || (logic && room_reachable(e->room,origin->reachable)));
}
static uint32_t simulation_mask(const Resident *resident,unsigned first,unsigned n) {
  uint32_t mask=0;
  for (unsigned i=0; i<n; ++i) {
    const WorldEntity *e=&resident->world.slots[first+i];
    if (!e->definition || logic || e->room==root || (e->flags&WORLD_ENVIRONMENT)) mask|=1u<<i;
  }
  return mask;
}
static uint32_t interaction_mask(const uint8_t *active,const Resident *resident,const bool *reachable,
    unsigned first,unsigned n) {
  const SmRoomPlacement *p=&resident->placement;
  DoorContext resource={SmRoomRuntimeRam(resident->runtime),resident->runtime,p->room,p->state,p->x,p->y};
  uint32_t mask=0;
  for (unsigned i=0; i<n; ++i) {
    const WorldEntity *e=&resident->world.slots[first+i];
    if (visible_entity(active,&resource,e) && (e->room==root || (logic && room_reachable(e->room,reachable)))) mask|=1u<<i;
  }
  return mask;
}
bool SmRoomsGrapple(uint8_t active[0x20000],uint16_t *response,uint16_t *definition,uint8_t *flags) {
  if (!response || !definition || !flags) return false;
  if (*definition || !rom || word(active+0x79b)!=root || word(active+0x998)!=8) return true;
  bool reachable[SM_ROOM_RESIDENTS+1];interaction_rooms(active,reachable);
  for (unsigned i=0; i<count; ++i) {
    Resident *r=&residents[i];const SmRoomPlacement *p=&r->placement;
    sync_entities(active,SmRoomRuntimeRam(r->runtime),p->x,p->y,&r->world,false);
    uint32_t a=interaction_mask(active,r,reachable,0,32),ep=interaction_mask(active,r,reachable,32,18);
    SmRoomRuntimeSetMasks(r->runtime,simulation_mask(r,0,32),simulation_mask(r,32,18),a,ep);
    if (!SmRoomRuntimeGrapple(r->runtime,active,p->x,p->y,response,definition,flags)) return false;
    if (*definition) break; /* native NoInteract is still the first target */
  }
  return true;
}
static unsigned world_shots(const uint8_t *active,unsigned target,const bool *reachable) {
  unsigned mask=0;
  for (unsigned i=0; i<10; ++i) {
    const WorldEntity *e=&active_world.slots[WORLD_ENEMIES+WORLD_EPROJS+i];
    if (e->definition && word(active+0xc40+2*i) && !(word(active+0xc04+2*i)&16) &&
        (e->room==target || (room_reachable(e->room,reachable) && room_reachable(target,reachable)))) mask|=1u<<i;
  }
  return mask;
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
bool SmRoomsEntryFrameAir(const uint8_t active[0x20000],unsigned slot) {
  if (!seamless || word(active+0x998)!=8 || word(active+0x79b)!=root ||
      word(active+0x79f)!=6 || (active[0x56]&7)!=7 || slot>=32) return false;
  const uint8_t *e=active+0xf78+64*slot;
  /* Parameter4 is the shaft's permanent Mode7 doorway frame. It is solid
   * behind a native arrival; a center-crossing arrival still overlaps it. */
  if (word(e)!=0xe23f || word(e+60)!=4) return false;
  unsigned n=0;const SmWorldDoor *doors=SmRoomPreviewDoors((uint16_t)root,&n);
  for (unsigned i=0;i<n;++i) {
    const SmWorldDoor *d=&doors[i];unsigned dir=d->direction&3,nb=0;
    if (!d->reciprocal || dir>=2) continue;
    const SmWorldDoor *back=SmRoomPreviewDoors(d->destination,&nb);
    bool arriving=false;
    for (unsigned j=0;j<nb;++j) if (back[j].definition==word(active+0x78d) &&
        back[j].destination==root && (back[j].direction&3)==(dir^1)) arriving=true;
    int px=(int16_t)word(active+0xaf6),py=(int16_t)word(active+0xafa),rx=(int)word(active+0xafe);
    int ex=(int16_t)word(e+2),er=(int)word(e+10),plane=(d->block_x+(dir==0))*16;
    if (arriving && py>=d->block_y*16 && py<(d->block_y+4)*16 &&
        abs(ex-plane)<=48 && (dir==0 ? px+rx>ex-er && px<plane : px-rx<ex+er && px>=plane)) return true;
  }
  return false;
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
    /* The Ceres Mode7 setup replaces its incoming trigger with scenic wall
     * cells. Native entry places the body inside that column; a continuous
     * body still straddles it. Admit only the remainder of that actual entry.
     * Once the trailing radius clears the inner edge, approaching from inside
     * hits the original wall again. No tile, lock or progression bit changes. */
    if (span && near && d->reciprocal && word(active+0x79f)==6 && (active[0x56]&7)==7 &&
        !SmDoorHasArrivalCap(read_rom(NULL,0x830003u+word(active+0x78d)))) {
      unsigned back_count=0;const SmWorldDoor *back=SmRoomPreviewDoors(d->destination,&back_count);
      bool arriving=false;
      for (unsigned j=0;j<back_count;++j) if (back[j].definition==word(active+0x78d) &&
          back[j].destination==root && (back[j].direction&3)==(dir^1)) arriving=true;
      int px=(int16_t)word(active+0xaf6),py=(int16_t)word(active+0xafa);
      int rx=(int)word(active+0xafe),ry=(int)word(active+0xb00);
      int inner=(dir<2 ? d->block_x : d->block_y)*16+((dir==1 || dir==3) ? 16 : 0);
      bool straddles=dir==0 ? px+rx>inner && px<inner+16 :
          dir==1 ? px-rx<inner && px>=inner-16 :
          dir==2 ? py+ry>inner && py<inner+16 : py-ry<inner && py>=inner-16;
      bool column=dir<2 ? bx==d->block_x : by==d->block_y;
      if (arriving && straddles && column && bx>=0 && by>=0 && bx<w && by<h &&
          (word(active+0x10002+2*(by*w+bx))>>12)==8) return 0;
    }
    if (!span || !near || !active_door(active,d) || !planar_portal(d,&p)) continue;
    /* The source trigger itself becomes ordinary hatch air. Beyond its
     * plane sample the destination's real cap/wall cells, including when
     * authored room allocations overlap. */
    bool source=dir<2 ? bx==d->block_x : by==d->block_y;
    if (source) return active_trigger(active,d,bx,by) ? 0 : -1;
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
    bool trigger=dir<2 ? active_trigger(active,d,d->block_x,y/16) : active_trigger(active,d,x/16,d->block_y);
    bool crossed=dir==0 ? x>=plane : dir==1 ? x<plane : dir==2 ? y>=plane : y<plane;
    if (!span || !trigger || !crossed || !active_door(active,d) || !planar_portal(d,&p) || x-p.x<0 || x-p.x>=p.width || y-p.y<0 || y-p.y>=p.height) continue;
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
    uint8_t active[0x20000],uint16_t vram[0x8000],const SmRoomPlacement *p,unsigned direction,bool *redraw) {
  if (redraw) *redraw=false;
  unsigned old_room=word(previous+0x79b),old_state=word(previous+0x7bb);
  const SmWorldRoom *room=SmRoomPreviewRoom((uint16_t)old_room);
  if (!room || !p || word(active+0x79b)!=p->room) return false;
  if (count==SM_ROOM_RESIDENTS) {
    bool arriving=false;
    for (unsigned i=0; i<count; ++i) if (residents[i].placement.room==p->room) arriving=true;
    if (!arriving) return false; /* never evict a live resource pool to hand off */
  }
  sync_entities(previous,previous,0,0,&active_world,true);
  WorldEntities departing_world=active_world,arriving_world={.loaded=1};
  SmRoomRuntime *departing=SmRoomRuntimeCreateConfigured(read_rom,NULL,room,(uint16_t)old_state,0,previous,false);
  if (!departing || !SmRoomRuntimeCaptureActive(departing,previous,previous_vram)) {
    SmRoomRuntimeDestroy(departing); return false;
  }
  for (unsigned i=0; i<count;) {
    if (residents[i].placement.room==p->room) {
      if (residents[i].placement.state==word(active+0x7bb)) {
        if (!SmRoomRuntimePromoteDoors(residents[i].runtime,active,redraw)) { SmRoomRuntimeDestroy(departing); return false; }
        SmRoomRuntimePromote(residents[i].runtime,active,vram);
        if (SmRoomRuntimeActors(residents[i].runtime) && !SmRoomRuntimeEntryOnly(residents[i].runtime,active))
          arriving_world=residents[i].world;
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
  memset(departing_world.slots+WORLD_ENEMIES+WORLD_EPROJS,0,WORLD_WEAPONS*sizeof(WorldEntity));
  departing_world.loaded=1;
  residents[count++]=(Resident){q,departing,departing_world}; pending_entry=true;
  for (unsigned i=WORLD_ENEMIES+WORLD_EPROJS; i<WORLD_ENTITIES; ++i) {
    arriving_world.slots[i]=active_world.slots[i];
    if (arriving_world.slots[i].definition) {
      arriving_world.slots[i].x=(int16_t)(arriving_world.slots[i].x-p->x);
      arriving_world.slots[i].y=(int16_t)(arriving_world.slots[i].y-p->y);
    }
  }
  active_world=arriving_world;
  camera_dx+=(int16_t)word(previous+0x911)-p->x-(int16_t)word(active+0x911);
  camera_dy+=(int16_t)word(previous+0x915)-p->y-(int16_t)word(active+0x915);
  rebased=true;
  return true;
}
static void update_connections(const uint8_t *active) {
  for (unsigned i=0; i<=count; ++i) {
    WorldEntities *world=i ? &residents[i-1].world : &active_world;
    unsigned room=i ? residents[i-1].placement.room : root,n=0;DoorContext source;
    world->open_rooms=0;if (!world_context(active,room,&source)) continue;
    const SmWorldDoor *doors=SmRoomPreviewDoors((uint16_t)room,&n);
    for (unsigned j=0; j<n; ++j) {
      DoorContext target;bool open=false;
      if (!world_connection(active,&source,&doors[j],&target,&open) || !open) continue;
      if (target.room==root) world->open_rooms|=1;
      else for (unsigned k=0; k<count; ++k) if (residents[k].placement.room==target.room) world->open_rooms|=1u<<(k+1);
    }
  }
}
static bool publish(void) {
  size_t bytes=SmRoomSceneBytes(count);
  SmRoomScene *next=calloc(1,bytes); if (!next) return false;
  const SmWorldRoom *room=SmRoomPreviewRoom((uint16_t)root);
  *next=(SmRoomScene){.version=4,.count=count,.logic=logic,.seamless=seamless,.root=root,
      .camera_dx=camera_dx,.camera_dy=camera_dy,.ownership=room!=NULL,.open_rooms=active_world.open_rooms,
      .root_width=room ? room->width*256u : 0,.root_height=room ? room->height*256u : 0};
  if (native_root) { next->native=1;next->native_root=*native_root; }
  for (unsigned j=0; j<WORLD_ENTITIES; ++j) next->object_rooms[j]=active_world.slots[j].room;
  for (unsigned i=0; i<count; ++i) {
    SmRoomImage *out=&next->rooms[i];
    out->placement=residents[i].placement;
    memcpy(out->ram,SmRoomRuntimeRam(residents[i].runtime),sizeof(out->ram));
    memcpy(out->vram,SmRoomRuntimeVram(residents[i].runtime),sizeof(out->vram));
    out->open_rooms=residents[i].world.open_rooms;
    for (unsigned j=0; j<WORLD_ENEMIES+WORLD_EPROJS; ++j) {
      const WorldEntity *e=&residents[i].world.slots[j];
      if (logic || e->room==root || (e->flags&WORLD_ENVIRONMENT)) out->object_rooms[j]=e->room;
    }
  }
  free(publication); publication=next; return true;
}
bool SmRoomsNativeFrame(const uint8_t active[0x20000],const uint16_t vram[0x8000],bool enabled) {
  unsigned state=word(active+0x998);
  if (!enabled || state<9 || state>13) { free(native_root);native_root=NULL;return true; }
  if (native_root || word(active+0x79b)!=root) return true;
  SmRoomPlacement destination;
  if (!SmRoomsDoor((uint16_t)word(active+0x78d),&destination)) return true;
  native_root=calloc(1,sizeof(*native_root));if (!native_root) return false;
  native_root->placement=(SmRoomPlacement){.room=(uint16_t)root,.state=(uint16_t)word(active+0x7bb),
      .width=(int)word(active+0x7a5)*16,.height=(int)word(active+0x7a7)*16,
      .left=-65536,.top=-65536,.right=65536,.bottom=65536};
  memcpy(native_root->ram,active,sizeof(native_root->ram));
  for (unsigned i=0;i<0x8000;++i) { native_root->vram[2*i]=(uint8_t)vram[i];native_root->vram[2*i+1]=(uint8_t)(vram[i]>>8); }
  native_root->open_rooms=active_world.open_rooms;
  for (unsigned i=0;i<50;++i) native_root->object_rooms[i]=active_world.slots[i].room;
  return true;
}
bool SmRoomsFrame(uint8_t active[0x20000], bool enabled, unsigned extra,
                    bool requested_logic, bool requested_seamless) {
  if (!enabled || !rom || rom_bytes!=0x300000) { SmRoomsReset(); return true; }
  unsigned game_state=word(active+0x998);
  if (game_state>=9 && game_state<=13) pending_entry=true;
  if (game_state!=8) return native_root ? publish() : true;
  unsigned current=word(active+0x79b);
  bool continuous_context=current==root || rebased;
  if ((current!=root || pending_entry) && !rebased) {
    camera_dx=camera_dy=0;memset(&active_world,0,sizeof(active_world));
  }
  if (current!=root || pending_entry) { root=current;logic=requested_logic;seamless=requested_seamless; }
  pending_entry=false;active_world.loaded=1;
  sync_entities(active,active,0,0,&active_world,true);
  if (word(active+0xa78)) {
    /* A native time freeze suspends actor motion while door PLMs continue.
     * An active blast still uses the same spatial interaction gate. */
    if (current!=root) return true;
    if (!mirror_doors(active)) return false;
    bool reachable[SM_ROOM_RESIDENTS+1];interaction_rooms(active,reachable);
    for (unsigned i=0; i<count; ++i) {
      Resident *r=&residents[i];const SmRoomPlacement *p=&r->placement;
      SmRoomRuntimeProgress(r->runtime,active);
      sync_entities(active,SmRoomRuntimeRam(r->runtime),p->x,p->y,&r->world,false);
      uint32_t a=interaction_mask(active,r,reachable,0,32),ep=interaction_mask(active,r,reachable,32,18);
      SmRoomRuntimeSetMasks(r->runtime,simulation_mask(r,0,32),simulation_mask(r,32,18),a,ep);
      EprojOrigin origin={active,p->x,p->y,reachable};
      SmRoomRuntimeSetWorld(r->runtime,resident_eproj_tile,&origin);
      SmRoomRuntimeSetBirth(r->runtime,resident_birth);
      SmRoomRuntimeSetPlayer(r->runtime,active,p->x,p->y,resident_player_eligible);
      bool ok=SmRoomRuntimePowerBomb(r->runtime,active,p->x,p->y);
      SmRoomRuntimeSetWorld(r->runtime,NULL,NULL);
      if (!ok || !SmRoomRuntimeStepDoors(r->runtime)) return false;
      sync_entities(active,SmRoomRuntimeRam(r->runtime),p->x,p->y,&r->world,false);
      SmRoomRuntimeDoorBits(r->runtime,active);
    }
    update_connections(active);return publish();
  }
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
  bool required[SM_ROOM_RESIDENTS]={0};
  if (continuous_context) {
    for (unsigned i=0; i<count; ++i) {
      Resident *r=&residents[i];
      sync_entities(active,SmRoomRuntimeRam(r->runtime),r->placement.x,r->placement.y,&r->world,false);
    }
    retain_entity_rooms(&active_world,count,required);
    for (unsigned i=0; i<count; ++i) retain_entity_rooms(&residents[i].world,i,required);
  }
  Resident old[SM_ROOM_RESIDENTS]; unsigned old_count=count;
  memcpy(old,residents,sizeof(old)); memset(residents,0,sizeof(residents)); count=0;
  for (unsigned i=0; i<old_count; ++i) if (required[i]) {
    residents[count++]=old[i];old[i].runtime=NULL;
  }
  bool ok=true;
  for (unsigned i=0; i<SmRoomPreviewCount(); ++i) {
    SmRoomPlacement p; if (!SmRoomPreviewPlacement(i,&p)) continue;
    bool retained=false;
    for (unsigned j=0; j<count; ++j) if (residents[j].placement.room==p.room) {
      /* Preserve an occupied native pool until its normal entry/state change;
       * matching previews may refresh portal clipping and incoming metadata. */
      if (residents[j].placement.state==p.state) residents[j].placement=p;
      retained=true;break;
    }
    if (retained || count==SM_ROOM_RESIDENTS) continue;
    SmRoomRuntime *runtime=NULL;WorldEntities world={0};
    for (unsigned j=0; j<old_count; ++j)
      if (old[j].runtime && old[j].placement.room==p.room && old[j].placement.state==p.state &&
          (old[j].placement.incoming==p.incoming || !old[j].placement.incoming)) {
        runtime=old[j].runtime;world=old[j].world;old[j].runtime=NULL;break;
      }
    if (!runtime) {
      const SmWorldRoom *room=SmRoomPreviewRoom(p.room);
      runtime=SmRoomRuntimeCreateConfigured(read_rom,NULL,room,p.state,p.incoming,active,logic);
      SmRoomRuntimeBackground(runtime,SmRoomPreviewVram(i));
    }
    if (runtime && logic && !SmRoomRuntimeEnableActors(runtime)) ok=false;
    if (!runtime || *SmRoomRuntimeError(runtime)) {
      fprintf(stderr,"[sm-rooms] root=%04X resident=%04X state=%04X: %s\n",root,p.room,p.state,SmRoomRuntimeError(runtime));
      SmRoomRuntimeDestroy(runtime); ok=false; break;
    }
    world.loaded=SmRoomRuntimeActors(runtime);
    residents[count++]=(Resident){p,runtime,world};
  }
  for (unsigned i=0; i<old_count; ++i) SmRoomRuntimeDestroy(old[i].runtime);
  if (!ok || !mirror_doors(active)) return false;
  bool reachable[SM_ROOM_RESIDENTS+1];interaction_rooms(active,reachable);
  for (unsigned i=0; i<count; ++i) {
    Resident *resident=&residents[i];const SmRoomPlacement *p=&resident->placement;SmRoomRuntime *runtime=resident->runtime;
    SmRoomRuntimeProgress(runtime,active);
    sync_entities(active,SmRoomRuntimeRam(runtime),p->x,p->y,&resident->world,false);
    uint32_t sim_a=simulation_mask(resident,0,32),sim_ep=simulation_mask(resident,32,18);
    uint32_t a=interaction_mask(active,resident,reachable,0,32),ep=interaction_mask(active,resident,reachable,32,18);
    SmRoomRuntimeSetMasks(runtime,sim_a,sim_ep,a,ep);
    EprojOrigin origin={active,p->x,p->y,reachable};SmRoomRuntimeSetWorld(runtime,resident_eproj_tile,&origin);
    SmRoomRuntimeSetBirth(runtime,resident_birth);
    SmRoomRuntimeSetEnemyWorld(runtime,resident_enemy_block);
    SmRoomRuntimeSetPlayer(runtime,active,p->x,p->y,resident_player_eligible);
    unsigned shots=world_shots(active,root,reachable);
    if (reachable[i+1]) shots|=world_shots(active,p->room,reachable);
    bool failed=!SmRoomRuntimeHitShots(runtime,active,p->x,p->y,shots) ||
        (a && !SmRoomRuntimeTouch(runtime,active,p->x,p->y)) ||
        !SmRoomRuntimeStepVisible(runtime,(int16_t)word(active+0xaf6)-p->x,(int16_t)word(active+0xafa)-p->y,
        x-p->x,y-p->y,extra) || !SmRoomRuntimeStepEffects(runtime);
    sync_entities(active,SmRoomRuntimeRam(runtime),p->x,p->y,&resident->world,false);
    a=interaction_mask(active,resident,reachable,0,32);ep=interaction_mask(active,resident,reachable,32,18);
    SmRoomRuntimeSetMasks(runtime,sim_a,sim_ep,a,ep);
    if (!failed) failed=!SmRoomRuntimeProjectiles(runtime,active,p->x,p->y,shots,ep!=0) ||
        !SmRoomRuntimePowerBomb(runtime,active,p->x,p->y);
    SmRoomRuntimeSetWorld(runtime,NULL,NULL);
    if (failed) {
      fprintf(stderr,"[sm-rooms] root=%04X resident=%04X: %s\n",root,p->room,SmRoomRuntimeError(runtime)); return false;
    }
    SmRoomRuntimeDoorBits(runtime,active);
  }
  update_connections(active);return ok && publish();
}

typedef struct RoomsState {
  uint32_t magic,version,count,root,logic,seamless,unit,pending_entry;
  int32_t camera_dx,camera_dy;
  uint32_t links;
} RoomsState;
size_t SmRoomsStateBound(void) {
  return sizeof(RoomsState)+SM_ROOM_RESIDENTS*(sizeof(SmRoomPlacement)+SmRoomRuntimeStateSize()+sizeof(WorldEntities))+sizeof(links)+sizeof(WorldEntities)+sizeof(SmRoomImage);
}
size_t SmRoomsSave(void *out, size_t capacity) {
  size_t unit=sizeof(SmRoomPlacement)+SmRoomRuntimeStateSize()+sizeof(WorldEntities);
  size_t bytes=sizeof(RoomsState)+count*unit+link_count*sizeof(DoorLink)+sizeof(WorldEntities)+(native_root ? sizeof(SmRoomImage) : 0);
  if (!out || capacity<bytes) return 0;
  /* pending_entry bit1 owns the optional immutable native-transition root. */
  RoomsState h={0x534d5257,6,count,root,logic,seamless,(uint32_t)unit,(pending_entry ? 1u : 0u)|(native_root ? 2u : 0u),camera_dx,camera_dy,link_count};
  memcpy(out,&h,sizeof(h)); uint8_t *p=(uint8_t *)out+sizeof(h);
  for (unsigned i=0; i<count; ++i,p+=unit) {
    memcpy(p,&residents[i].placement,sizeof(SmRoomPlacement));
    if (!SmRoomRuntimeSave(residents[i].runtime,p+sizeof(SmRoomPlacement),SmRoomRuntimeStateSize())) return 0;
    memcpy(p+sizeof(SmRoomPlacement)+SmRoomRuntimeStateSize(),&residents[i].world,sizeof(WorldEntities));
  }
  memcpy(p,links,link_count*sizeof(DoorLink));
  memcpy(p+link_count*sizeof(DoorLink),&active_world,sizeof(active_world));
  if (native_root) memcpy(p+link_count*sizeof(DoorLink)+sizeof(active_world),native_root,sizeof(*native_root));
  return bytes;
}
static bool valid_entities(const WorldEntities *world,bool weapons,uint32_t room_mask) {
  if (world->loaded>1 || (world->open_rooms&~room_mask)) return false;
  for (unsigned i=0; i<WORLD_ENTITIES; ++i) {
    const WorldEntity *e=&world->slots[i];
    if (e->flags&~WORLD_ENVIRONMENT) return false;
    if (!e->definition) { if (e->room || e->pending || e->flags || e->x || e->y) return false; }
    else if ((!weapons && i>=WORLD_ENEMIES+WORLD_EPROJS) || e->room<0x8000 ||
        (e->pending && (e->pending<0x88fc || e->pending>=0xacf0))) return false;
  }
  return true;
}
bool SmRoomsValidate(const void *in, size_t size) {
  RoomsState h;
  if (!in || size<sizeof(h)) return false;
  memcpy(&h,in,sizeof(h));
  if (h.magic!=0x534d5257 || h.version!=6 || h.count>SM_ROOM_RESIDENTS || h.root>65535 || h.links>MAX_LINKS ||
      h.camera_dx < -4096 || h.camera_dx>4096 || h.camera_dy < -4096 || h.camera_dy>4096 ||
      h.logic>1 || h.seamless>1 || h.pending_entry>3 || h.unit!=sizeof(SmRoomPlacement)+SmRoomRuntimeStateSize()+sizeof(WorldEntities) ||
      size!=sizeof(h)+(size_t)h.count*h.unit+h.links*sizeof(DoorLink)+sizeof(WorldEntities)+((h.pending_entry&2) ? sizeof(SmRoomImage) : 0)) return false;
  const uint8_t *p=(const uint8_t *)in+sizeof(h);
  for (unsigned i=0; i<h.count; ++i,p+=h.unit) {
    SmRoomPlacement v; memcpy(&v,p,sizeof(v));
    if (v.reserved || v.room<0x8000 || v.room==h.root || v.state<0x8000 || v.width<=0 || v.width>4096 || v.width%256 ||
        v.height<=0 || v.height>4096 || v.height%256 || v.left>=v.right || v.top>=v.bottom ||
        v.x < -65536 || v.x>65536 || v.y < -65536 || v.y>65536) return false;
    WorldEntities world;memcpy(&world,p+sizeof(v)+SmRoomRuntimeStateSize(),sizeof(world));
    if (!valid_entities(&world,false,(1u<<(h.count+1))-1) ||
        !SmRoomRuntimeValidate(p+sizeof(v),SmRoomRuntimeStateSize(),v.room,v.state,world.loaded!=0)) return false;
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
  WorldEntities world;memcpy(&world,p+h.links*sizeof(DoorLink),sizeof(world));
  if (h.pending_entry&2) {
    SmRoomImage *image=malloc(sizeof(*image));if (!image) return false;
    memcpy(image,p+h.links*sizeof(DoorLink)+sizeof(world),sizeof(*image));
    bool valid=image->placement.room==h.root && SmRoomImageValid(image,(1u<<(h.count+1))-1);
    free(image);if (!valid) return false;
  }
  return valid_entities(&world,true,(1u<<(h.count+1))-1);
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
    next[i].runtime=SmRoomRuntimeCreateConfigured(read_rom,NULL,&room,v->state,v->incoming,seed,false);
    if (!next[i].runtime || !SmRoomRuntimeLoad(next[i].runtime,p+sizeof(SmRoomPlacement),SmRoomRuntimeStateSize())) { ok=false; break; }
    memcpy(&next[i].world,p+sizeof(SmRoomPlacement)+SmRoomRuntimeStateSize(),sizeof(WorldEntities));
  }
  free(seed);
  if (!ok) { for (unsigned i=0; i<h.count; ++i) SmRoomRuntimeDestroy(next[i].runtime); return false; }
  SmRoomImage *next_native=NULL;
  if (h.pending_entry&2) {
    next_native=malloc(sizeof(*next_native));
    if (!next_native) { for (unsigned i=0;i<h.count;++i) SmRoomRuntimeDestroy(next[i].runtime);return false; }
    memcpy(next_native,p+h.links*sizeof(DoorLink)+sizeof(active_world),sizeof(*next_native));
  }
  SmRoomsReset();native_root=next_native; memcpy(residents,next,sizeof(next)); count=h.count; root=h.root;
  logic=h.logic!=0; seamless=h.seamless!=0;
  pending_entry=(h.pending_entry&1)!=0;
  camera_dx=h.camera_dx; camera_dy=h.camera_dy;
  link_count=h.links; memcpy(links,p,link_count*sizeof(DoorLink));
  memcpy(&active_world,p+link_count*sizeof(DoorLink),sizeof(active_world));
  return publish();
}
