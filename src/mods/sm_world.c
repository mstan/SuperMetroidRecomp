#include "sm_world.h"
#include <stdio.h>
#include <string.h>

typedef struct Reader { SmWorldRead byte; void *context; } Reader;
static uint8_t B(Reader r, uint32_t a) { return r.byte(r.context, a); }
static uint16_t W(Reader r, uint32_t a) { return (uint16_t)(B(r,a) | B(r,a+1) << 8); }
static uint32_t L(Reader r, uint32_t a) { return W(r,a) | (uint32_t)B(r,a+2) << 16; }
static uint16_t Ram(const uint8_t *p, unsigned a) { return (uint16_t)(p[a] | p[a+1] << 8); }
static bool Fail(SmWorld *w, const char *why, uint32_t at) {
  snprintf(w->error, sizeof(w->error), "%s at $%06X", why, (unsigned)at); return false;
}
static bool Header(Reader r, uint16_t h) {
  uint32_t a = 0x8f0000u | h;
  return h >= 0x9000 && h < 0xe000 && B(r,a+1) < SM_WORLD_AREAS &&
    B(r,a+2) < 64 && B(r,a+3) < 31 && B(r,a+4) && B(r,a+4) <= 16 &&
    B(r,a+5) && B(r,a+5) <= 16 && W(r,a+9) >= 0x8000;
}
const SmWorldRoom *SmWorldFindRoom(const SmWorld *w, uint16_t h) {
  for (unsigned i=0; i<w->room_count; ++i) if (w->rooms[i].header == h) return &w->rooms[i];
  return NULL;
}
/* All conditional selectors present in the supported ROM. The final marker
 * is followed by an inline 26-byte default state. */
static int Args(uint16_t selector) {
  switch (selector) {
    case 0xe5eb: return 2; /* incoming door */
    case 0xe612: case 0xe629: return 1; /* event / area boss mask */
    case 0xe5ff: case 0xe652: case 0xe669: return 0;
    default: return -1;
  }
}
static bool State(SmWorld *w, Reader r, uint16_t room, uint16_t state) {
  if (state < 0x8000 || state > 0xffe6) return Fail(w, "invalid room state", state);
  for (unsigned i=0; i<w->state_count; ++i)
    if (w->state_rooms[i] == room && w->states[i] == state) return true;
  if (w->state_count == SM_WORLD_STATES) return Fail(w, "state capacity", state);
  w->state_rooms[w->state_count] = room; w->states[w->state_count++] = state;
  (void)r; return true;
}
static bool States(SmWorld *w, Reader r, SmWorldRoom *room) {
  uint32_t p = 0x8f0000u + room->header + 11;
  for (unsigned i=0; i<32; ++i) {
    uint16_t selector = W(r,p);
    if (selector == 0xe5e6) {
      room->default_state = (uint16_t)(p+2);
      return State(w,r,room->header,room->default_state);
    }
    int n = Args(selector);
    if (n < 0) return Fail(w, "unknown room selector", p);
    if (!State(w,r,room->header,W(r,p+2+(unsigned)n))) return false;
    p += 4+(unsigned)n;
  }
  return Fail(w, "unterminated room selectors", p);
}
/* Cartridge compression commands: literal, byte/word fill, incrementing fill,
 * absolute/relative output copy, optionally complemented. Copies may overlap.
 * Bank progression skips the unmapped lower half, as the stock decoder does. */
static uint8_t Next(Reader r, uint32_t *p) {
  uint8_t v = B(r,*p);
  ++*p; if (!(*p & 0xffff)) *p += 0x8000;
  return v;
}
static size_t Unpack(Reader r, uint32_t p, uint8_t *dst, size_t cap) {
  size_t out = 0;
  for (unsigned commands=0; commands<65536; ++commands) {
    unsigned b = Next(r,&p); if (b == 255) return out;
    unsigned command = b >> 5, count = (b & 31)+1;
    if (command == 7) { command = (b >> 2)&7; count = (((b&3)<<8) | Next(r,&p))+1; }
    if (count > cap-out) return 0;
    if (command >= 4) {
      size_t from;
      if (command >= 6) { unsigned back=Next(r,&p); if (!back || back>out) return 0; from=out-back; }
      else { from=Next(r,&p); from |= (size_t)Next(r,&p)<<8; }
      for (unsigned i=0; i<count; ++i) {
        if (from >= out) return 0;
        dst[out++] = dst[from++] ^ ((command&1) ? 255 : 0);
      }
    } else if (!command) {
      while (count--) dst[out++] = Next(r,&p);
    } else {
      uint8_t first=Next(r,&p), second=command==2 ? Next(r,&p) : first;
      for (unsigned i=0; i<count; ++i)
        dst[out++] = command==3 ? (uint8_t)(first+i) : ((i&1) ? second : first);
    }
  }
  return 0;
}
static bool Drop(SmWorld *w,uint16_t room,uint16_t state,uint16_t plm,unsigned bts,unsigned x,unsigned y) {
  for (unsigned i=0; i<w->marker_count; ++i) {
    const SmWorldMarker *m=&w->markers[i];
    if (m->kind==SM_MAP_DROP && m->room==room && m->state==state && m->x/16==x/16 && m->y/16==y/16) return true;
  }
  if (w->marker_count==SM_WORLD_MARKERS) return Fail(w,"drop marker capacity",room);
  w->markers[w->marker_count++]=(SmWorldMarker){room,state,plm,(uint16_t)bts,(uint8_t)x,(uint8_t)y,SM_MAP_DROP,0,3};
  return true;
}
static bool LevelDoors(SmWorld *w, Reader r, SmWorldRoom *room, uint16_t state) {
  uint8_t data[65536];
  uint32_t source=L(r,0x8f0000u+state);
  size_t size=Unpack(r,source,data,sizeof(data));
  if (size<2) return Fail(w,"invalid compressed level",source);
  unsigned bytes=data[0] | data[1]<<8;
  unsigned blocks=(unsigned)room->width*room->height*256;
  /* Some stock rooms retain padded rows from a larger source layout (e.g.
   * West Wrecked Ship). The size word locates BTS; room dimensions bound
   * playable cells. Do not reinterpret that padding as another room row. */
  if ((bytes&1) || bytes < blocks*2 || 2u+bytes+bytes/2 > size) {
    snprintf(w->error,sizeof(w->error),"level dimensions room $%04X data $%06X: bytes %u blocks %u decoded %u",
      room->header,(unsigned)source,bytes,blocks,(unsigned)size); return false;
  }
  for (unsigned i=0; i<blocks; ++i) {
    unsigned tile=data[2+2*i] | data[3+2*i]<<8;
    unsigned bts=data[2+bytes+i];
    if ((tile&0xf000)==0xb000) {
      /* Resolve the same BTS -> PLM -> setup-function path as stock vertical
       * collision. Only CE37 is a down-only crumble; other special blocks
       * (speed blocks, stations, area scripts) must not become false arrows. */
      uint32_t table=(bts&0x80) ? 0x940000u|W(r,0x9492e9+2*room->area) : 0x949139;
      uint16_t plm=W(r,table+2*(bts&0x7f));
      if (plm>=0x8000 && W(r,0x840000u|plm)==0xce37 &&
          !Drop(w,room->header,state,plm,bts,i%(room->width*16),i/(room->width*16))) return false;
    }
    if ((tile&0xf000) != 0x9000) continue;
    unsigned index=bts&0x7f;
    if (index >= room->door_count) return Fail(w,"level door index outside list",room->header);
    SmWorldDoor *d=&w->doors[room->first_door+index];
    if (d->block_x<0) { d->block_x=(int16_t)(i%(room->width*16)); d->block_y=(int16_t)(i/(room->width*16)); }
  }
  return true;
}
static bool Marker(SmWorld *w, uint16_t room, uint16_t state, uint16_t plm,
                    uint8_t x, uint8_t y, uint16_t argument) {
  int kind=-1, detail=0, direction=0;
  if (plm>=0xeed7 && plm<=0xefcf && !((plm-0xeed7)&3)) {
    kind=SM_MAP_ITEM; detail=((plm-0xeed7)/4)%21;
  } else if (plm==0xb76f) kind=SM_MAP_SAVE;
  else if (plm==0xb6d3) kind=SM_MAP_MAP;
  else if (plm==0xb6df) kind=SM_MAP_ENERGY;
  else if (plm==0xb6eb) kind=SM_MAP_AMMO;
  else if (plm==0xdb48 || plm==0xdb56) {
    kind=SM_MAP_CAP; detail=SM_CAP_EYE; direction=plm==0xdb48;
  } else if (plm>=0xc842 && plm<=0xc8b4 && (plm-0xc842)%6==0) {
    static const uint8_t colors[]={SM_CAP_GREY,SM_CAP_POWER_BOMB,SM_CAP_SUPER,SM_CAP_MISSILE,SM_CAP_BLUE};
    unsigned n=(plm-0xc842)/6; kind=SM_MAP_CAP; detail=colors[n/4]; direction=n%4;
  } else if ((plm==0xc836 || plm==0xc83a) && argument<16 && !(argument&1)) {
    static const uint8_t colors[]={SM_CAP_SUPER,SM_CAP_MISSILE,SM_CAP_BLUE,SM_CAP_POWER_BOMB};
    kind=SM_MAP_GATE; detail=colors[argument/4]; direction=(argument&2)!=0;
  }
  if (kind<0) return true;
  for (unsigned i=0; i<w->marker_count; ++i) {
    const SmWorldMarker *m=&w->markers[i];
    if (m->room==room && m->state==state && m->plm==plm && m->x==x && m->y==y && m->argument==argument) return true;
  }
  if (w->marker_count==SM_WORLD_MARKERS) return Fail(w,"marker capacity",plm);
  w->markers[w->marker_count++]=(SmWorldMarker){room,state,plm,argument,x,y,(uint8_t)kind,(uint8_t)detail,(uint8_t)direction};
  return true;
}
/* Mother Brain creates her escape door after the fight, rather than storing
 * its block in the compressed room. Read the stock SpawnHardcodedPlm record
 * and its typed setup operand; never infer an exit from an unused header. */
static bool ScriptedDoors(SmWorld *w, Reader r) {
  const SmWorldRoom *room=SmWorldFindRoom(w,0xdd58);
  if (!room) return true;
  uint32_t call=0xa9b333, args=call+4;
  if (B(r,call)!=0x22 || L(r,call+1)!=0x8483d7)
    return Fail(w,"escape door spawn",call);
  uint16_t plm=W(r,args+2), setup=W(r,0x840000u|plm);
  if (setup!=0xb5f8 || B(r,0x840000u+setup+3)!=0xa9)
    return Fail(w,"escape door setup",0x840000u|plm);
  uint16_t block=W(r,0x840000u+setup+4);
  unsigned index=block&0x7f, x=B(r,args), y=B(r,args+1);
  if ((block&0xf000)!=0x9000 || index>=room->door_count ||
      x>=room->width*16u || y>=room->height*16u)
    return Fail(w,"escape door block",0x840000u+setup+4);
  SmWorldDoor *door=&w->doors[room->first_door+index];
  door->block_x=(int16_t)x; door->block_y=(int16_t)y;
  return true;
}
bool SmWorldLoad(SmWorld *w, SmWorldRead read, void *context) {
  if (!w || !read) return false;
  memset(w,0,sizeof(*w)); Reader r={read,context};
  if (!Header(r,0x91f8)) return Fail(w,"stock Landing Site missing",0x8f91f8);
  w->rooms[w->room_count++].header=0x91f8;
  for (unsigned i=0; i<w->room_count; ++i) {
    SmWorldRoom *room=&w->rooms[i]; uint32_t a=0x8f0000u|room->header;
    room->area=B(r,a+1); room->x=B(r,a+2); room->y=B(r,a+3)+1;
    room->width=B(r,a+4); room->height=B(r,a+5); room->first_door=w->door_count;
    if (!States(w,r,room)) return false;
    uint16_t list=W(r,a+9);
    for (unsigned j=0; j<64; ++j) {
      uint16_t ptr=W(r,0x8f0000u+list+2*j);
      if (ptr<0x88fc || ptr>=0xacf0) break;
      uint32_t da=0x830000u|ptr; uint16_t target=W(r,da);
      /* Elevator dummy entries have no room pointer but still occupy a slot. */
      if (target && !Header(r,target)) break;
      if (w->door_count==SM_WORLD_DOORS) return Fail(w,"door capacity",ptr);
      w->doors[w->door_count++]=(SmWorldDoor){ptr,room->header,target,(uint16_t)j,-1,-1,
        B(r,da+3),B(r,da+2),B(r,da+4),B(r,da+5),B(r,da+6),B(r,da+7),false};
      ++room->door_count;
      if (target && !SmWorldFindRoom(w,target)) {
        if (w->room_count==SM_WORLD_ROOMS) return Fail(w,"room capacity",target);
        w->rooms[w->room_count++].header=target;
      }
      if (j==63) return Fail(w,"unterminated door list",a);
    }
    if (!LevelDoors(w,r,room,room->default_state)) return false;
  }
  for (unsigned i=0; i<w->state_count; ++i) {
    uint16_t state=w->states[i], room=w->state_rooms[i];
    SmWorldRoom *owner=NULL;
    for (unsigned j=0; j<w->room_count; ++j) if (w->rooms[j].header==room) { owner=&w->rooms[j]; break; }
    if (!owner) return Fail(w,"state room missing",state);
    if (state!=owner->default_state && !LevelDoors(w,r,owner,state)) return false;
    uint32_t p=0x8f0000u+W(r,0x8f0000u+state+20);
    if (!(p&0x8000)) continue;
    for (unsigned j=0; j<256; ++j,p+=6) {
      uint16_t plm=W(r,p); if (!plm) break;
      if (!Marker(w,room,state,plm,B(r,p+2),B(r,p+3),W(r,p+4))) return false;
      if (j==255) return Fail(w,"unterminated PLM population",p);
    }
  }
  if (!ScriptedDoors(w,r)) return false;
  for (unsigned i=0; i<w->door_count; ++i) {
    SmWorldDoor *d=&w->doors[i];
    if (d->block_x<0) continue;
    const SmWorldRoom *to=SmWorldFindRoom(w,d->destination);
    if (to) for (unsigned j=0; j<to->door_count; ++j) {
      const SmWorldDoor *reverse=&w->doors[to->first_door+j];
      if (reverse->block_x>=0 && reverse->destination==d->source) { d->reciprocal=true; break; }
    }
  }
  for (unsigned area=0; area<SM_WORLD_AREAS; ++area) {
    uint32_t tiles=L(r,0x82964a+3*area), bits=0x820000u|W(r,0x829717+2*area);
    for (unsigned i=0; i<2048; ++i) w->tiles[area][i]=W(r,tiles+2*i);
    for (unsigned i=0; i<256; ++i) w->station_map[area][i]=B(r,bits+i);
  }
  for (unsigned n=0; n<w->room_count; ++n) {
    const SmWorldRoom *room=&w->rooms[n];
    for (unsigned y=room->y; y<room->y+room->height && y<32; ++y)
      for (unsigned x=room->x; x<room->x+room->width && x<64; ++x) {
        unsigned i=(x&31)+y*32+(x>=32?1024:0);
        if (!w->room_at[room->area][i] && (w->tiles[room->area][i]&1023)!=31)
          w->room_at[room->area][i]=(uint16_t)(n+1);
      }
  }
  return true;
}
uint16_t SmWorldRoomState(const SmWorldRoom *room, SmWorldRead read, void *context, const uint8_t *ram) {
  if (!room || !ram || !read) return 0;
  if (Ram(ram,0x79b)==room->header) return Ram(ram,0x7bb);
  Reader r={read,context}; uint32_t p=0x8f0000u+room->header+11;
  for (unsigned i=0; i<32; ++i) {
    uint16_t fn=W(r,p); if (fn==0xe5e6) return (uint16_t)(p+2);
    int n=Args(fn); if (n<0) return 0;
    unsigned arg=B(r,p+2); bool selected=false;
    switch (fn) {
      case 0xe5ff: selected=(ram[0xd82d]&1)!=0; break;
      case 0xe612: selected=(ram[0xd820+arg/8]&(1u<<(arg&7)))!=0; break;
      case 0xe629: selected=(ram[0xd828+room->area]&arg)!=0; break;
      case 0xe652: selected=(Ram(ram,0x9a4)&4) && Ram(ram,0x9c8); break;
      case 0xe669: selected=Ram(ram,0x9d0)!=0; break;
      default: break; /* arrival-door condition is unknown for another room */
    }
    if (selected) return W(r,p+2+(unsigned)n);
    p+=4+(unsigned)n;
  }
  return room->default_state;
}
static int Index(int x, int y) { return x<0 || x>=64 || y<0 || y>=32 ? -1 : (x&31)+y*32+(x>=32?1024:0); }
uint16_t SmWorldTile(const SmWorld *w, unsigned area, int x, int y) {
  int i=Index(x,y); return area<SM_WORLD_AREAS && i>=0 ? w->tiles[area][i] : 31;
}
const SmWorldRoom *SmWorldCellRoom(const SmWorld *w, unsigned area, int x, int y) {
  int i=Index(x,y); if (area>=SM_WORLD_AREAS || i<0) return NULL;
  unsigned n=w->room_at[area][i]; return n && n<=w->room_count ? &w->rooms[n-1] : NULL;
}
unsigned SmWorldLayout(const SmWorld *w, int16_t xy[SM_WORLD_ROOMS][2]) {
  bool placed[SM_WORLD_ROOMS]={false}; unsigned queue[SM_WORLD_ROOMS], head=0, count=0, conflicts=0;
  for (unsigned i=0; i<w->room_count; ++i) { xy[i][0]=w->rooms[i].x; xy[i][1]=w->rooms[i].y; }
  if (!w->room_count) return 0;
  placed[0]=true; queue[count++]=0;
  while (head<count) {
    unsigned from=queue[head++]; const SmWorldRoom *room=&w->rooms[from];
    for (unsigned i=0; i<room->door_count; ++i) {
      const SmWorldDoor *door=&w->doors[room->first_door+i];
      const SmWorldRoom *other=SmWorldFindRoom(w,door->destination);
      if (!other || door->block_x<0) continue;
      int x=door->block_x, y=door->block_y;
      static const int dx[]={1,-1,0,0},dy[]={0,0,1,-1};
      unsigned direction=door->direction&3;
      int ax=(door->flags&0x80) ? door->arrival_screen_x : door->arrival_x/16;
      int ay=(door->flags&0x80) ? door->arrival_screen_y : door->arrival_y/16;
      int gx=xy[from][0]+x/16+dx[direction]-ax, gy=xy[from][1]+y/16+dy[direction]-ay;
      unsigned to=(unsigned)(other-w->rooms);
      if (!placed[to]) { xy[to][0]=(int16_t)gx; xy[to][1]=(int16_t)gy; placed[to]=true; queue[count++]=to; }
      else if (xy[to][0]!=gx || xy[to][1]!=gy) ++conflicts;
    }
  }
  return conflicts;
}
bool SmWorldExplored(const uint8_t *ram, unsigned area, int x, int y) {
  int i=Index(x,y); if (!ram || area>=SM_WORLD_AREAS || i<0) return false;
  unsigned base=Ram(ram,0x79f)==area ? 0x7f7 : 0xcd52+area*256;
  return (ram[base+(unsigned)i/8] & (0x80u>>(i&7)))!=0;
}
bool SmWorldKnown(const SmWorld *w, const uint8_t *ram, unsigned area, int x, int y, bool reveal, bool secrets) {
  int i=Index(x,y); if (!ram || area>=SM_WORLD_AREAS || i<0 || (SmWorldTile(w,area,x,y)&1023)==31) return false;
  return SmWorldExplored(ram,area,x,y) || ((reveal || ram[0xd908+area]) &&
    (secrets || (w->station_map[area][(unsigned)i/8]&(0x80u>>(i&7)))));
}
bool SmWorldItemCollected(const uint8_t *ram, const SmWorldMarker *m) {
  return ram && m && m->kind==SM_MAP_ITEM && m->argument<512 &&
    (ram[0xd870+m->argument/8]&(1u<<(m->argument&7)))!=0;
}
