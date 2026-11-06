#include "sm_room_runtime.h"
#include "sm_activity.h"
#include "sm_door.h"
#include "sm_shot_view.h"
#include "snes/interp816.h"
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { PLAYER_VIEW=0x9a2,PLAYER_VIEW_BYTES=0xb64-PLAYER_VIEW };
typedef struct PlayerLoan {
  uint8_t player[PLAYER_VIEW_BYTES],input[24],timers[4];
  uint16_t grapple[3];
  bool active,commit;
} PlayerLoan;
struct SmRoomRuntime {
  SmWorldRead read;
  void *context;
  Interp816 *cpu;
  uint8_t ram[0x20000], math[8];
  uint8_t vram[65536];
  uint8_t ppu_registers[0x34], dma_registers[128];
  uint16_t product, quotient, remainder;
  uint16_t population, tileset;
  uint8_t population_data[32*16+3], tileset_data[32*4+2];
  bool actors;
  uint16_t door_blocks[128];
  uint8_t door_orientations[128];
  unsigned door_count;
  SmRoomShotAir shot_air;
  void *shot_context;
  SmEprojQuery world_query;
  void *world_context;
  SmWorldBirth world_birth;
  SmEnemyQuery enemy_query;
  bool enemy_reaction;
  unsigned world_phase;
  uint32_t simulation_enemies,simulation_projectiles,interaction_enemies,interaction_projectiles;
  SmEprojLoan eproj_loan;
  uint8_t *live_player;
  SmWorldPlayer player_eligible;
  int player_x,player_y;
  PlayerLoan player_loan;
  char error[128];
};
static unsigned word(const uint8_t *p) { return p[0] | p[1]<<8; }
static void put(SmRoomRuntime *r, unsigned a, int v) {
  r->ram[a]=(uint8_t)v; r->ram[a+1]=(uint8_t)((unsigned)v>>8);
}
static void player_end(SmRoomRuntime *r,bool success) {
  PlayerLoan *loan=&r->player_loan;if (!loan->active) return;
  if (success && loan->commit) {
    const unsigned positions[]={0xaf6,0xafa,0xb10,0xb14};
    for (unsigned i=0; i<4; ++i) put(r,positions[i],(int)word(r->ram+positions[i])+(i&1 ? r->player_y : r->player_x));
    memcpy(r->live_player+PLAYER_VIEW,r->ram+PLAYER_VIEW,PLAYER_VIEW_BYTES);
    memcpy(r->live_player+0x18a8,r->ram+0x18a8,4);
    SmShotPut(r->live_player,0xd08,(int)word(r->ram+0xd08)+r->player_x);
    SmShotPut(r->live_player,0xd0c,(int)word(r->ram+0xd0c)+r->player_y);
    SmShotPut(r->live_player,0xd32,(int)word(r->ram+0xd32));
  }
  memcpy(r->ram+PLAYER_VIEW,loan->player,PLAYER_VIEW_BYTES);
  memcpy(r->ram+0x8b,loan->input,24);memcpy(r->ram+0x18a8,loan->timers,4);
  put(r,0xd08,loan->grapple[0]);put(r,0xd0c,loan->grapple[1]);put(r,0xd32,loan->grapple[2]);
  loan->active=false;
}
static void player_begin(SmRoomRuntime *r,bool eligible) {
  if (!r->live_player) return;
  PlayerLoan *loan=&r->player_loan;
  memcpy(loan->player,r->ram+PLAYER_VIEW,PLAYER_VIEW_BYTES);
  memcpy(loan->input,r->ram+0x8b,24);memcpy(loan->timers,r->ram+0x18a8,4);
  loan->grapple[0]=(uint16_t)word(r->ram+0xd08);loan->grapple[1]=(uint16_t)word(r->ram+0xd0c);
  loan->grapple[2]=(uint16_t)word(r->ram+0xd32);
  memcpy(r->ram+PLAYER_VIEW,r->live_player+PLAYER_VIEW,PLAYER_VIEW_BYTES);
  memcpy(r->ram+0x8b,r->live_player+0x8b,24);memcpy(r->ram+0x18a8,r->live_player+0x18a8,4);
  const unsigned positions[]={0xaf6,0xafa,0xb10,0xb14};
  for (unsigned i=0; i<4; ++i) put(r,positions[i],eligible ?
      (int)word(r->ram+positions[i])-(i&1 ? r->player_y : r->player_x) : 0x8000);
  put(r,0xd08,eligible ? (int)word(r->live_player+0xd08)-r->player_x : 0x8000);
  put(r,0xd0c,eligible ? (int)word(r->live_player+0xd0c)-r->player_y : 0x8000);
  put(r,0xd32,eligible ? (int)word(r->live_player+0xd32) : 0xc4f0);
  loan->active=true;loan->commit=eligible;
}
static int ram_offset(uint32_t a) {
  unsigned bank=a>>16, lo=a&65535;
  if (bank==0x7e || bank==0x7f) return (int)(a-0x7e0000);
  if (!(bank&0x40) && lo<0x2000) return (int)lo;
  return -1;
}
static void fail(SmRoomRuntime *r, const char *kind, uint32_t a) {
  if (!r->error[0]) snprintf(r->error,sizeof(r->error),"%s $%06X at $%02X:%04X",kind,
      (unsigned)a,r->cpu->k,r->cpu->pc);
}
static uint8_t read_bus(void *context, uint32_t a) {
  SmRoomRuntime *r=context; int off=ram_offset(a);
  if (off>=0) return r->ram[off];
  if (a&0x8000) {
    uint32_t key=a&0x7fffff;
    if (r->population && key>=0x210000u+r->population && key-0x210000u-r->population<sizeof(r->population_data))
      return r->population_data[key-0x210000u-r->population];
    if (r->tileset && key>=0x340000u+r->tileset && key-0x340000u-r->tileset<sizeof(r->tileset_data))
      return r->tileset_data[key-0x340000u-r->tileset];
    return r->read(r->context,a);
  }
  unsigned lo=a&65535;
  if (!((a>>16)&0x40)) {
    if (lo>=0x4300 && lo<0x4380) return r->dma_registers[lo-0x4300];
    switch (lo) {
      case 0x4214: return (uint8_t)r->quotient;
      case 0x4215: return (uint8_t)(r->quotient>>8);
      case 0x4216: return (uint8_t)r->remainder;
      case 0x4217: return (uint8_t)(r->remainder>>8);
    }
  }
  fail(r,"unsupported room read",a); return 0;
}
static void write_bus(void *context, uint32_t a, uint8_t v) {
  SmRoomRuntime *r=context; int off=ram_offset(a);
  if (off>=0) { r->ram[off]=v; return; }
  unsigned lo=a&65535;
  if (!((a>>16)&0x40)) {
    if (lo>=0x2100 && lo<0x2134) { r->ppu_registers[lo-0x2100]=v; return; }
    if (lo>=0x4300 && lo<0x4380) { r->dma_registers[lo-0x4300]=v; return; }
  }
  if (!((a>>16)&0x40) && lo>=0x4202 && lo<=0x4206) {
    r->math[lo-0x4200]=v;
    if (lo==0x4203) r->remainder=r->product=r->math[2]*v;
    if (lo==0x4206) {
      unsigned dividend=r->math[4] | r->math[5]<<8;
      r->quotient=v ? (uint16_t)(dividend/v) : 65535;
      r->remainder=v ? (uint16_t)(dividend%v) : (uint16_t)dividend;
    }
    return;
  }
  fail(r,"unsupported room write",a);
}
/* Bounded native routine call, with an actual cartridge JSR/JSL return frame.
 * No host dispatch, mod hooks, IRQs, NMI or active-machine bus is involved. */
static bool call(SmRoomRuntime *r, uint32_t entry, unsigned frame) {
  Interp816 *c=r->cpu;
  if (r->error[0]) return false;
  c->sp=0x1fff; c->k=(uint8_t)(entry>>16); c->pc=(uint16_t)entry;
  if (frame==3) r->ram[c->sp--]=c->k;
  r->ram[c->sp--]=0xfe; r->ram[c->sp--]=0xff;
  for (unsigned i=0; i<200000 && !r->error[0]; ++i) {
    if (c->pc==0xff00 && c->sp==0x1fff) { player_end(r,true);return true; }
    if (c->waiting || c->stopped) { player_end(r,false);fail(r,"room routine suspended",entry); return false; }
    if ((c->k&127)==6) {
      if (c->pc==0x811b) player_end(r,true);
      if (c->pc==0x8125 && c->x<36 && !(c->x&1))
        player_begin(r,r->player_eligible ? r->player_eligible(r->world_context,r->ram,32+c->x/2) :
            (r->interaction_projectiles&(1u<<(c->x/2)))!=0);
      if (c->pc==0x8104) r->world_phase=2;
      if (c->pc==0x8124) r->world_phase=0;
      if (c->pc==0x8118 && c->x<36 && !(r->simulation_projectiles&(1u<<(c->x/2)))) c->pc=0x811e;
      if ((c->pc==0x8091 || c->pc==0x80fe) && r->world_birth && c->a<36 && !(c->a&1))
        r->world_birth(r->world_context,r->ram,32+c->a/2,r->world_phase);
      if (c->pc==0x8886 || c->pc==0x889e)
        SmEprojBegin(r->ram,&c->x,c->pc==0x8886,r->world_query,r->world_context,&r->eproj_loan);
      else if ((c->pc==0x889d || c->pc==0x88b5) &&
          SmEprojEnd(r->ram,&c->x,c->pc==0x889d,&r->eproj_loan)) {
        c->n=(c->x&0x8000)!=0;c->z=c->x==0;
      }
    }
    if ((c->k&127)==0x20) {
      if ((c->pc==0xc845 || c->pc==0xc879) && r->enemy_query && !r->enemy_reaction) {
        bool horizontal=c->pc==0xc845;uint16_t original=c->x;
        int result=r->enemy_query(r->world_context,r->ram,horizontal,&c->x);
        if (result==-2) { fail(r,"native enemy terrain reaction",entry);break; }
        if (result>=0) {
          c->c=result!=0;c->n=(c->x&0x8000)!=0;c->z=c->x==0;
          c->pc=horizontal ? 0xc858 : 0xc88c;
        } else if (c->x!=original) c->a=(uint16_t)word(r->ram+0x10002+c->x);
      }
      if (c->pc==0x8fff || c->pc==0x9145) player_end(r,true);
      if (c->pc==0x9010 && c->a<2048 && !(c->a&63))
        player_begin(r,r->player_eligible ? r->player_eligible(r->world_context,r->ram,c->a/64) :
            (r->interaction_enemies&(1u<<(c->a/64)))!=0);
      if (c->pc==0x8fd4) r->world_phase=1;
      if (c->pc==0x9168) r->world_phase=0;
      if (c->pc==0x9419 && r->world_birth && c->x<2048 && !(c->x&63))
        r->world_birth(r->world_context,r->ram,c->x/64,r->world_phase);
    }
    if (r->shot_air && (c->k&127)==0x14 && (c->pc==0xa1cd || c->pc==0xa1ee) &&
        c->x==18 && r->shot_air(r->shot_context,r->ram,word(r->ram+0xdc4)))
      c->x=0; /* this registered connection's trigger is ordinary shot air */
    interp816_runOpcode(c);
    if (r->error[0]) {
      size_t n=strlen(r->error);
      snprintf(r->error+n,sizeof(r->error)-n," (entry $%06X)",(unsigned)entry);
    }
  }
  player_end(r,false);
  if (!r->error[0]) fail(r,"room instruction limit",entry);
  return false;
}
static uint32_t rom_long(const SmRoomRuntime *r,uint32_t a) {
  return r->read(r->context,a) | r->read(r->context,a+1)<<8 |
      (uint32_t)r->read(r->context,a+2)<<16;
}
static uint32_t ram_long(const SmRoomRuntime *r,unsigned a) {
  return word(r->ram+a) | (uint32_t)r->ram[a+2]<<16;
}
static bool remember_door(SmRoomRuntime *r,unsigned block,unsigned orientation) {
  for (unsigned i=0; i<r->door_count; ++i) if (r->door_blocks[i]==block) return true;
  if (r->door_count==128) { fail(r,"resident door capacity",block); return false; }
  r->door_blocks[r->door_count]=(uint16_t)block;
  r->door_orientations[r->door_count++]=(uint8_t)orientation; return true;
}
static bool initialize_doors(SmRoomRuntime *r,uint16_t state) {
  unsigned cursor=(unsigned)rom_long(r,0x8f0014u+state)&65535;
  if (!(cursor&0x8000)) return true;
  for (unsigned i=0; i<256; ++i,cursor+=6) {
    unsigned header=(unsigned)rom_long(r,0x8f0000u+cursor)&65535;
    if (!header) { put(r,0x1c23,0x8000); return true; }
    if (!SmDoorHeader(header)) continue;
    if (header>=0xc842 && header<=0xc8b4) {
      unsigned x=r->read(r->context,0x8f0002u+cursor),y=r->read(r->context,0x8f0003u+cursor);
      if (!remember_door(r,y*word(r->ram+0x7a5)+x,((header-0xc842)/6)%4)) return false;
    }
    r->cpu->x=(uint16_t)cursor;
    if (!call(r,0x84846a,3)) return false;
  }
  fail(r,"unterminated resident door PLMs",state); return false;
}
SmRoomRuntime *SmRoomRuntimeCreate(SmWorldRead read, void *context,
    const SmWorldRoom *room, uint16_t state, const uint8_t active[0x20000]) {
  return SmRoomRuntimeCreateConfigured(read,context,room,state,0,active,true);
}
SmRoomRuntime *SmRoomRuntimeCreateConfigured(SmWorldRead read, void *context,
    const SmWorldRoom *room, uint16_t state, uint16_t incoming,
    const uint8_t active[0x20000], bool actors) {
  if (!read || !room || !state || !active) return NULL;
  SmRoomRuntime *r=calloc(1,sizeof(*r)); if (!r) return NULL;
  r->read=read; r->context=context;
  r->simulation_enemies=r->interaction_enemies=UINT32_MAX;
  r->simulation_projectiles=r->interaction_projectiles=(1u<<18)-1;
  memcpy(r->ram,active,sizeof(r->ram));
  memset(r->ram+0x10000,0,0x10000);
  /* Room-local actors, projectiles, PLMs, FX and sprite submissions cannot
   * inherit the active room's occupants. Keep equipment/progression globals. */
  memset(r->ram+0xc0c,0,0x2000-0xc0c);
  memset(r->ram+0x370,0,0x400);
  memset(r->ram+0x619,0,0x24);
  r->ram[0x52]=3; r->ram[0x55]=1; r->ram[0x5d]=0; r->ram[0x5e]=4;
  put(r,0xcadc,0); put(r,0xcade,0);
  r->cpu=interp816_init(r,read_bus,write_bus);
  if (!r->cpu) { free(r); return NULL; }
  r->cpu->db=0x7e; r->cpu->i=true;
  interp816_set_brk_hook_enabled(r->cpu,false);
  put(r,0x79b,room->header); put(r,0x7bb,state); put(r,0x79f,room->area);
  put(r,0x78d,incoming);
  put(r,0x7a5,room->width*16); put(r,0x7a7,room->height*16);
  put(r,0x7a9,room->width); put(r,0x7ab,room->height);
  put(r,0x7b9,room->width*room->height*512); /* native bound is a byte index */
  if (!call(r,0x82def2,2)) return r;
  for (unsigned i=0; i<512; ++i) r->ram[0xc200+i]=read(context,0x9a8000u+i);
  for (unsigned i=0; i<0x2000; ++i) r->vram[0x8000+i]=read(context,0x9ab200u+i);
  for (unsigned i=0; i<0x2e00; ++i) r->vram[0xc000+i]=read(context,0x9ad200u+i);
  if (!(room->area==6 ?
      SmWorldUnpack(read,context,ram_long(r,0x7c0),r->ram+0xa000,8192) :
      SmWorldUnpack(read,context,0xb9a09d,r->ram+0xa000,2048) &&
      SmWorldUnpack(read,context,ram_long(r,0x7c0),r->ram+0xa800,6144)) ||
      !SmWorldUnpack(read,context,ram_long(r,0x7c6),r->ram+0xc200,512) ||
      !SmWorldUnpack(read,context,0xb98000,r->vram+0x5000,0x3000) ||
      !SmWorldUnpack(read,context,ram_long(r,0x7c3),r->vram,32768)) {
    fail(r,"invalid resident graphics",state); return r;
  }
  if (!call(r,0x89ab82,3)) return r; /* cartridge FX selection and initialization */
  unsigned fx_map=word(r->ram+0x1964);
  if (fx_map) for (unsigned i=0; i<0x840; ++i) r->vram[0xb7c0+i]=read(context,0x8a0000u+fx_map+i);
  put(r,0x1ef1,0x8000);
  put(r,0x18b0,0x8000); put(r,0x1e79,0x8000);
  uint8_t level[65536];
  size_t bytes=SmWorldUnpack(read,context,rom_long(r,0x8f0000u+state),level,sizeof(level));
  unsigned n=bytes>=2 ? word(level) : 0;
  if (!n || n>0x6400 || 2u+n+n/2>bytes) { fail(r,"invalid resident level",state); return r; }
  memcpy(r->ram+0x10000,level,2+n);
  memcpy(r->ram+0x16402,level+2+n,n/2);
  if (2u+2*n+n/2<=bytes) memcpy(r->ram+0x19602,level+2+n+n/2,n);
  for (unsigned block=0; block<room->width*room->height*256u; ++block) {
    unsigned type=word(r->ram+0x10002+2*block)>>12,bts=r->ram[0x16402+block];
    if ((type==12 || type==4) && bts>=0x40 && bts<=0x43 && !remember_door(r,block,bts-0x40)) return r;
  }
  memcpy(r->ram+0xc000,r->ram+0xc200,512);
  if (!initialize_doors(r,state)) return r;
  /* A disabled population must not make kill-quota doors believe that the
   * room has already been cleared. Preserve the cartridge's actual quota. */
  unsigned quota=0;
  for (unsigned i=0; i<32; ++i) {
    uint32_t at=0xa10000u+word(r->ram+0x7cf)+16*i;
    if (((unsigned)rom_long(r,at)&65535)==65535) { quota=read(context,at+2); break; }
  }
  put(r,0xe52,(int)quota);
  if (!actors) return r;
  SmRoomRuntimeEnableActors(r);
  return r;
}
static void prepare_population(SmRoomRuntime *r) {
  SmWorldRead read=r->read;void *context=r->context;
  unsigned quota=word(r->ram+0xe52);
  /* Ordinary populations retain the cartridge's complete initialization.
   * Boss definitions stay dormant individually; they must not suppress other
   * residents or visual effects in the same room. Filtering is local to this
   * interpreter bus and never patches the ROM or active machine. */
  r->population=(uint16_t)word(r->ram+0x7cf); r->tileset=(uint16_t)word(r->ram+0x7d1);
  memset(r->population_data,255,sizeof(r->population_data)); memset(r->tileset_data,255,sizeof(r->tileset_data));
  unsigned used=0;
  for (unsigned i=0; i<32; ++i) {
    uint32_t a=0xa10000u+r->population+16*i;
    unsigned id=read(context,a) | read(context,a+1)<<8;
    if (id==65535) break;
    if (read(context,0xa00010u+id) || read(context,0xa00011u+id)) continue;
    for (unsigned j=0; j<16; ++j) r->population_data[used*16+j]=read(context,a+j);
    ++used;
  }
  r->population_data[used*16+2]=(uint8_t)quota;
  unsigned tiles=0;
  for (unsigned i=0; i<32 && r->tileset; ++i) {
    uint32_t a=0xb40000u+r->tileset+4*i;
    unsigned id=read(context,a) | read(context,a+1)<<8;
    if (id==65535) break;
    if (read(context,0xa00010u+id) || read(context,0xa00011u+id)) continue;
    for (unsigned j=0; j<4; ++j) r->tileset_data[tiles*4+j]=read(context,a+j);
    ++tiles;
  }
}
bool SmRoomRuntimeEnableActors(SmRoomRuntime *r) {
  if (!r || r->error[0]) return false;
  if (r->actors) return true;
  r->actors=true;prepare_population(r);
  put(r,0xe1c,65535); put(r,0xe12,1); /* no active-player collision in the isolated probe */
  if (!call(r,0xa08a1e,3)) return false;
  memcpy(r->vram+0xd800,r->ram+0x7000,0x2800);
  memcpy(r->ram+0xc000,r->ram+0xc200,512);
  return call(r,0xa08a9e,3);
}
void SmRoomRuntimeDestroy(SmRoomRuntime *r) {
  if (r) { interp816_free(r->cpu); free(r); }
}
void SmRoomRuntimeSetWorld(SmRoomRuntime *r,SmEprojQuery query,void *context) {
  if (r) { r->world_query=query;r->world_context=context;if (!query) { r->world_birth=NULL;r->enemy_query=NULL;r->live_player=NULL;r->player_eligible=NULL; } }
}
void SmRoomRuntimeSetEnemyWorld(SmRoomRuntime *r,SmEnemyQuery query) { if (r) r->enemy_query=query; }
void SmRoomRuntimeSetPlayer(SmRoomRuntime *r,uint8_t active[0x20000],int x,int y,SmWorldPlayer eligible) {
  if (r) { r->live_player=active;r->player_x=x;r->player_y=y;r->player_eligible=eligible; }
}
void SmRoomRuntimeSetBirth(SmRoomRuntime *r,SmWorldBirth birth) { if (r) r->world_birth=birth; }
void SmRoomRuntimeSetMasks(SmRoomRuntime *r,uint32_t a,uint32_t ep,uint32_t contact_a,uint32_t contact_ep) {
  if (r) { r->simulation_enemies=a;r->simulation_projectiles=ep;r->interaction_enemies=contact_a;r->interaction_projectiles=contact_ep; }
}
bool SmRoomRuntimeActors(const SmRoomRuntime *r) { return r && r->actors; }
bool SmRoomRuntimeStep(SmRoomRuntime *r, int samus_x, int samus_y, int camera_x, int camera_y) {
  return SmRoomRuntimeStepVisible(r,samus_x,samus_y,camera_x,camera_y,0);
}
bool SmRoomRuntimeStepVisible(SmRoomRuntime *r, int samus_x, int samus_y, int camera_x, int camera_y, unsigned extra) {
  if (!r || r->error[0]) return false;
  put(r,0xaf6,samus_x); put(r,0xafa,samus_y);
  put(r,0x911,camera_x); put(r,0x915,camera_y);
  if (!r->actors) return true;
  if (!call(r,0xa08eb6,3) || !SmActivityAugmentQueues(r->ram,extra)) return false;
  if (r->simulation_enemies!=UINT32_MAX) for (unsigned list=0; list<2; ++list) {
    unsigned base=list ? 0x17ec : 0x17ac,count=0;
    for (unsigned i=0; i<32; ++i) {
      unsigned index=word(r->ram+base+2*i);if (index==65535) break;
      if (index>=2048 || (index&63)) { fail(r,"invalid resident activity index",index);return false; }
      if (r->simulation_enemies&(1u<<(index/64))) { put(r,base+2*count,(int)index);++count; }
    }
    put(r,base+2*count,65535);put(r,list ? 0x17a6 : 0x17a4,(int)count*2);
  }
  /* Native DrawEnemySprites consumes these eight per-layer queues. Our
   * immutable compositor draws the actors instead; retaining queue sizes
   * would append each frame until indices overwrite adjacent enemy data. */
  memset(r->ram+0xf68,0,16);
  bool ok=call(r,0xa08fd4,3);
  memset(r->ram+0xf68,0,16);
  return ok && call(r,0x868104,3);
}
bool SmRoomRuntimeStepDoors(SmRoomRuntime *r) {
  if (!r || r->error[0]) return false;
  uint16_t pre[40]={0},timer[40]={0}; bool suspended[40]={0};
  for (unsigned i=0; i<40; ++i) {
    unsigned header=word(r->ram+0x1c37+2*i);
    if (!header || SmDoorHeader(header)) continue;
    /* Captured departing rooms can contain items, stations and scripts.
     * Reserve their slots without executing any pre-instruction or draw. */
    suspended[i]=true; pre[i]=(uint16_t)word(r->ram+0x1cd7+2*i);
    timer[i]=(uint16_t)word(r->ram+0xde1c+2*i);
    put(r,0x1cd7+2*i,0x8469); put(r,0xde1c+2*i,65535);
  }
  bool ok=call(r,0x8485b4,3);
  for (unsigned i=0; i<40; ++i) if (suspended[i]) {
    put(r,0x1cd7+2*i,pre[i]); put(r,0xde1c+2*i,timer[i]);
  }
  return ok;
}
bool SmRoomRuntimeDoorShot(SmRoomRuntime *r,unsigned x,unsigned y,uint16_t type) {
  if (!r || r->error[0] || x>=word(r->ram+0x7a5) || y>=word(r->ram+0x7a7)) return false;
  unsigned block=y*word(r->ram+0x7a5)+x;
  unsigned tile=word(r->ram+0x10002+2*block),bts=r->ram[0x16402+block];
  if (((tile>>12)!=12 && (tile>>12)!=4) || bts<0x40 || bts>0x44) return false;
  unsigned header=(unsigned)rom_long(r,0x949ea6u+2*bts)&65535;
  unsigned previous_index=word(r->ram+0xdde),previous_type=word(r->ram+0xc18);
  put(r,0xdc4,(int)block); put(r,0xdde,0); put(r,0xc18,type); r->cpu->a=(uint16_t)header;
  bool ok=call(r,0x8484e7,3);
  put(r,0xdde,(int)previous_index); put(r,0xc18,(int)previous_type);
  return ok;
}
int SmRoomRuntimeEnemyBlock(SmRoomRuntime *r,uint8_t source[0x20000],int dx,int dy,int x,int y,bool horizontal) {
  unsigned index=word(source+0xe54);
  if (!r || r->error[0] || index>=2048 || (index&63) || x<0 || y<0 ||
      x>=(int)word(r->ram+0x7a5) || y>=(int)word(r->ram+0x7a7)) return -2;
  uint8_t scratch[64],actor[SM_ENEMY_BYTES],stack[256];
  memcpy(scratch,r->ram,sizeof(scratch));memcpy(actor,r->ram+SM_ENEMY_POOL+index,sizeof(actor));
  memcpy(stack,r->ram+0x1f00,sizeof(stack));
  const unsigned work[]={0xe54,0xdc4,0xdd4,0xdd6,0x1e77,0x1c27,0x1c29,0x1c2b};
  uint16_t saved[sizeof(work)/sizeof(*work)];
  for (unsigned i=0; i<sizeof(work)/sizeof(*work); ++i) saved[i]=(uint16_t)word(r->ram+work[i]);
  Interp816 previous=*r->cpu;SmEnemyInput(r->ram,source,dx,dy,horizontal);
  r->cpu->x=(uint16_t)(2*(y*word(r->ram+0x7a5)+x));r->cpu->a=(uint16_t)word(r->ram+0x10002+r->cpu->x);
  r->cpu->db=0xa0;r->cpu->dp=0;r->cpu->mf=r->cpu->xf=r->cpu->e=r->cpu->d=false;
  r->cpu->i=true;r->cpu->waiting=r->cpu->stopped=false;r->enemy_reaction=true;
  bool ok=call(r,horizontal ? 0xa0c845 : 0xa0c879,2);
  int result=ok ? (int)r->cpu->c : -2;
  if (ok) SmEnemyOutput(source,r->ram,index,-dx,-dy,horizontal);
  r->enemy_reaction=false;
  memcpy(r->ram,scratch,sizeof(scratch));memcpy(r->ram+SM_ENEMY_POOL+index,actor,sizeof(actor));
  memcpy(r->ram+0x1f00,stack,sizeof(stack));
  for (unsigned i=0; i<sizeof(work)/sizeof(*work); ++i) put(r,work[i],saved[i]);
  *r->cpu=previous;return result;
}
int SmRoomRuntimeShot(SmRoomRuntime *r,uint8_t active[0x20000],int origin_x,int origin_y,
    int x,int y,bool horizontal,SmRoomShotAir air,void *context) {
  if (!r || r->error[0] || x<0 || y<0 || x>=(int)word(r->ram+0x7a5) ||
      y>=(int)word(r->ram+0x7a7)) return -2;
  uint8_t scratch[64],pool[SM_SHOT_POOL_BYTES];
  memcpy(scratch,r->ram,sizeof(scratch)); memcpy(pool,r->ram+SM_SHOT_POOL,sizeof(pool));
  const unsigned work[]={0xdde,0xdc4,0x1c27,0x1c29,0x1c2b};
  uint16_t saved[sizeof(work)/sizeof(*work)];
  for (unsigned i=0; i<sizeof(work)/sizeof(*work); ++i) saved[i]=(uint16_t)word(r->ram+work[i]);
  Interp816 previous=*r->cpu;
  SmShotInput(r->ram,active,-origin_x,-origin_y,horizontal);
  r->cpu->x=(uint16_t)(2*(y*word(r->ram+0x7a5)+x));
  r->cpu->db=0x94; r->cpu->dp=0; r->cpu->mf=r->cpu->xf=r->cpu->e=r->cpu->d=false;
  r->cpu->i=true; r->cpu->waiting=r->cpu->stopped=false;
  r->shot_air=air; r->shot_context=context;
  bool ok=call(r,horizontal ? 0x94a1b5 : 0x94a1d6,2);
  int result=ok ? (int)r->cpu->c : -2;
  if (ok) SmShotOutput(active,r->ram,origin_x,origin_y);
  r->shot_air=NULL; r->shot_context=NULL;
  memcpy(r->ram,scratch,sizeof(scratch)); memcpy(r->ram+SM_SHOT_POOL,pool,sizeof(pool));
  for (unsigned i=0; i<sizeof(work)/sizeof(*work); ++i) put(r,work[i],saved[i]);
  *r->cpu=previous;
  return result;
}
bool SmRoomRuntimeDoorOpen(SmRoomRuntime *r,unsigned x,unsigned y,unsigned orientation) {
  if (!r || r->error[0] || orientation>=4 || x>=word(r->ram+0x7a5) || y>=word(r->ram+0x7a7)) return false;
  unsigned block=y*word(r->ram+0x7a5)+x;
  for (unsigned i=0; i<40; ++i)
    if (word(r->ram+0x1c87+2*i)==2*block && SmDoorHeader(word(r->ram+0x1c37+2*i))) put(r,0x1c37+2*i,0);
  unsigned index=word(r->ram+0xdde);
  put(r,0xdc4,(int)block); put(r,0xdde,65535);
  r->cpu->a=(uint16_t)((unsigned)rom_long(r,0x949ea6u+2*(0x40+orientation))&65535);
  bool ok=call(r,0x8484e7,3); put(r,0xdde,(int)index); return ok;
}
static bool hit_shots(SmRoomRuntime *r,uint8_t active[0x20000],int origin_x,int origin_y,unsigned mask,bool projectiles) {
  if (!r || r->error[0] || !active) return false;
  if ((!r->actors && !projectiles) || word(active+0xa78) || word(active+0x185e)) return true;
  /* An ordinary impact has already consumed this shot's collision. Plasma's
   * native piercing response leaves that bit clear and retains its own enemy
   * invincibility timer. Never hit twice merely because ownership is remote. */
  for (unsigned i=0; i<10; ++i)
    if (!word(active+0xc18+2*i) || (word(active+0xc04+2*i)&16)) mask&=~(1u<<i);
  mask&=1023;
  if (!mask) return true;
  if (projectiles) {
    bool candidate=false;
    for (unsigned i=0; i<36; i+=2)
      if (word(r->ram+0x1997+i) && (word(r->ram+0x1bd7+i)&0x8000) && word(r->ram+0xf380+i)!=2)
        { candidate=true;break; }
    if (!candidate) return true;
  }
  uint8_t scratch[64],pool[SM_SHOT_POOL_BYTES],borrowed[SM_SHOT_POOL_BYTES];
  memcpy(scratch,r->ram,sizeof(scratch));memcpy(pool,r->ram+SM_SHOT_POOL,sizeof(pool));
  memcpy(borrowed,active+SM_SHOT_POOL,sizeof(borrowed));
  const unsigned work[]={0xdde,0xe54,0x18a6,0xf378,0x183e,0x1840};
  uint16_t saved[sizeof(work)/sizeof(*work)];
  for (unsigned i=0; i<sizeof(work)/sizeof(*work); ++i) saved[i]=(uint16_t)word(r->ram+work[i]);
  Interp816 previous=*r->cpu;
  memcpy(r->ram+SM_SHOT_POOL,borrowed,sizeof(borrowed));SmShotTranslate(r->ram,-origin_x,-origin_y);
  for (unsigned i=0; i<10; ++i) if (!(mask&(1u<<i))) {
    put(r,0xc18+2*i,0);put(r,0xc40+2*i,0);
  }
  put(r,0x183e,(int)word(active+0x183e));put(r,0x1840,(int)word(active+0x1840));
  r->cpu->db=0xa0;r->cpu->dp=0;r->cpu->mf=r->cpu->xf=r->cpu->e=r->cpu->d=false;
  r->cpu->i=true;r->cpu->waiting=r->cpu->stopped=false;
  bool ok=true;unsigned previous_phase=r->world_phase;
  if (projectiles) ok=call(r,0xa0996c,3);
  else r->world_phase=1;
  for (unsigned i=0; !projectiles && ok && i<32; ++i) {
    unsigned index=word(r->ram+0x17ac+2*i);
    if (index==65535) break;
    if (index>=2048 || (index&63)) { fail(r,"invalid resident collision index",index);ok=false;break; }
    if (!(r->interaction_enemies&(1u<<(index/64)))) continue;
    unsigned at=0xf78+index;
    if (!word(r->ram+at) || (word(r->ram+at+14)&0x400) || word(r->ram+at+40)) continue;
    bool multi=(word(r->ram+at+16)&4)!=0;
    put(r,0xe54,(int)index);
    ok=call(r,multi ? 0xa09b7f : 0xa0a143,2);
    if (ok && word(r->ram+at)) ok=call(r,multi ? 0xa09d23 : 0xa0a236,2);
  }
  if (ok) {
    SmShotTranslate(r->ram,origin_x,origin_y);
    memcpy(active+SM_SHOT_POOL,r->ram+SM_SHOT_POOL,sizeof(pool));
    /* Restore filtered slots instead of exporting their temporary zero types.
     * All eighteen native per-slot arrays (including linked supers) stay in
     * their original slots; shared counters retain native mutations. */
    for (unsigned a=0; a<18; ++a) for (unsigned i=0; i<10; ++i) if (!(mask&(1u<<i)))
      memcpy(active+SM_SHOT_POOL+20*a+2*i,borrowed+20*a+2*i,2);
    memcpy(active+0x183e,r->ram+0x183e,4);
  }
  memcpy(r->ram,scratch,sizeof(scratch));memcpy(r->ram+SM_SHOT_POOL,pool,sizeof(pool));
  for (unsigned i=0; i<sizeof(work)/sizeof(*work); ++i) put(r,work[i],saved[i]);
  r->world_phase=previous_phase;*r->cpu=previous;
  return ok;
}
bool SmRoomRuntimeHitShots(SmRoomRuntime *r,uint8_t active[0x20000],int origin_x,int origin_y,unsigned mask) {
  return hit_shots(r,active,origin_x,origin_y,mask,false);
}
static bool touch(SmRoomRuntime *r,uint8_t active[0x20000],int origin_x,int origin_y,bool projectiles) {
  if (!r || r->error[0] || !active) return false;
  if ((!r->actors && !projectiles) || word(active+0xa78) || word(active+0x185e)) return true;
  if (projectiles) {
    if (word(active+0x18a8) || word(active+0xa6e)) return true;
    bool candidate=false;
    for (unsigned i=0; i<36; i+=2)
      if (word(r->ram+0x1997+i) && !(word(r->ram+0x1bd7+i)&0x2000) && word(r->ram+0x1bb3+i))
        { candidate=true;break; }
    if (!candidate) return true;
  }
  enum { PLAYER=0x9a2, PLAYER_BYTES=0xb64-PLAYER };
  uint8_t scratch[64],player[PLAYER_BYTES],input[24],timers[4];
  memcpy(scratch,r->ram,sizeof(scratch));memcpy(player,r->ram+PLAYER,sizeof(player));
  memcpy(input,r->ram+0x8b,sizeof(input));memcpy(timers,r->ram+0x18a8,sizeof(timers));
  const unsigned work[]={0xe54,0x18a6,0xf378};uint16_t saved[3];
  for (unsigned i=0; i<3; ++i) saved[i]=(uint16_t)word(r->ram+work[i]);
  Interp816 previous=*r->cpu;
  memcpy(r->ram+PLAYER,active+PLAYER,sizeof(player));memcpy(r->ram+0x8b,active+0x8b,sizeof(input));
  memcpy(r->ram+0x18a8,active+0x18a8,sizeof(timers));
  const unsigned positions[]={0xaf6,0xafa,0xb10,0xb14};
  for (unsigned i=0; i<4; ++i) put(r,positions[i],(int)word(r->ram+positions[i])-(i&1 ? origin_y : origin_x));
  r->cpu->db=0xa0;r->cpu->dp=0;r->cpu->mf=r->cpu->xf=r->cpu->e=r->cpu->d=false;
  r->cpu->i=true;r->cpu->waiting=r->cpu->stopped=false;
  bool ok=true;unsigned previous_phase=r->world_phase;
  if (projectiles) ok=call(r,0xa09894,3);
  else r->world_phase=1;
  for (unsigned i=0; !projectiles && ok && i<32; ++i) {
    unsigned index=word(r->ram+0x17ac+2*i);
    if (index==65535) break;
    if (index>=2048 || (index&63)) { fail(r,"invalid resident contact index",index);ok=false;break; }
    if (!(r->interaction_enemies&(1u<<(index/64)))) continue;
    unsigned at=0xf78+index;
    if (!word(r->ram+at) || (word(r->ram+at+14)&0x400) || word(r->ram+at+40)) continue;
    put(r,0xe54,(int)index);
    ok=call(r,(word(r->ram+at+16)&4) ? 0xa09a5a : 0xa0a07a,2);
  }
  if (ok) {
    for (unsigned i=0; i<4; ++i) put(r,positions[i],(int)word(r->ram+positions[i])+(i&1 ? origin_y : origin_x));
    memcpy(active+PLAYER,r->ram+PLAYER,sizeof(player));memcpy(active+0x18a8,r->ram+0x18a8,sizeof(timers));
  }
  memcpy(r->ram,scratch,sizeof(scratch));memcpy(r->ram+PLAYER,player,sizeof(player));
  memcpy(r->ram+0x8b,input,sizeof(input));memcpy(r->ram+0x18a8,timers,sizeof(timers));
  for (unsigned i=0; i<3; ++i) put(r,work[i],saved[i]);
  r->world_phase=previous_phase;*r->cpu=previous;return ok;
}
bool SmRoomRuntimeTouch(SmRoomRuntime *r,uint8_t active[0x20000],int origin_x,int origin_y) {
  return touch(r,active,origin_x,origin_y,false);
}
bool SmRoomRuntimeProjectiles(SmRoomRuntime *r,uint8_t active[0x20000],int origin_x,int origin_y,
    unsigned mask,bool body) {
  /* The cartridge resolves these after enemy-projectile movement, with
   * Samus contact before shootable-projectile reactions. */
  if (!r || r->error[0]) return false;
  uint8_t ids[36];memcpy(ids,r->ram+0x1997,sizeof(ids));
  for (unsigned i=0; i<18; ++i) if (!(r->interaction_projectiles&(1u<<i))) put(r,0x1997+2*i,0);
  bool ok=(!body || touch(r,active,origin_x,origin_y,true)) && hit_shots(r,active,origin_x,origin_y,mask,true);
  for (unsigned i=0; i<18; ++i) if (!(r->interaction_projectiles&(1u<<i))) memcpy(r->ram+0x1997+2*i,ids+2*i,2);
  return ok;
}
bool SmRoomRuntimeDoorClose(SmRoomRuntime *r,unsigned x,unsigned y,unsigned orientation) {
  if (!r || r->error[0] || orientation>=4 || x>=word(r->ram+0x7a5) || y>=word(r->ram+0x7a7)) return false;
  unsigned block=y*word(r->ram+0x7a5)+x;
  for (unsigned i=0; i<40; ++i)
    if (word(r->ram+0x1c87+2*i)==2*block && SmDoorHeader(word(r->ram+0x1c37+2*i))) put(r,0x1c37+2*i,0);
  unsigned cursor=(unsigned)rom_long(r,0x8f0014u+word(r->ram+0x7bb))&65535;
  for (unsigned i=0; (cursor&0x8000) && i<256; ++i,cursor+=6) {
    unsigned header=(unsigned)rom_long(r,0x8f0000u+cursor)&65535;
    if (!header) break;
    if (!SmDoorHeader(header) || r->read(r->context,0x8f0002u+cursor)!=x ||
        r->read(r->context,0x8f0003u+cursor)!=y) continue;
    r->cpu->x=(uint16_t)cursor;
    if (!call(r,0x84846a,3)) return false;
    unsigned slot=word(r->ram+0x1c27);
    put(r,0x1d27+slot,(int)((unsigned)rom_long(r,0x840004u+header)&65535));
    return true;
  }
  put(r,0xdc4,(int)block); r->cpu->a=(uint16_t)(0xc8ba+4*orientation);
  return call(r,0x8484e7,3);
}
void SmRoomRuntimeProgress(SmRoomRuntime *r,const uint8_t active[0x20000]) {
  if (!r) return;
  memcpy(r->ram+0xd820,active+0xd820,16);
  for (unsigned i=0; i<64; ++i) r->ram[0xd8b0+i]|=active[0xd8b0+i];
}
void SmRoomRuntimeDoorBits(const SmRoomRuntime *r,uint8_t active[0x20000]) {
  if (r) for (unsigned i=0; i<64; ++i) active[0xd8b0+i]|=r->ram[0xd8b0+i];
}
void SmRoomRuntimeBackground(SmRoomRuntime *r, const uint8_t vram[65536]) {
  if (r && vram) memcpy(r->vram+0x8000,vram+0x8000,0x37c0);
}
bool SmRoomRuntimeCapture(SmRoomRuntime *r,const uint8_t ram[0x20000],const uint16_t vram[0x8000]) {
  if (!r || r->error[0] || word(ram+0x79b)!=word(r->ram+0x79b) || word(ram+0x7bb)!=word(r->ram+0x7bb)) return false;
  memcpy(r->ram,ram,sizeof(r->ram));
  for (unsigned i=0; i<0x8000; ++i) { r->vram[2*i]=(uint8_t)vram[i]; r->vram[2*i+1]=(uint8_t)(vram[i]>>8); }
  /* A suspended resident has no authority over the actual player's body. */
  put(r,0xe12,1);
  if (!r->actors) {
    memset(r->ram+0xf78,0,2048);
    memset(r->ram+0x1997,0,36);
  }
  return true;
}
bool SmRoomRuntimeCaptureActive(SmRoomRuntime *r,const uint8_t ram[0x20000],const uint16_t vram[0x8000]) {
  if (!r) return false;
  bool previous=r->actors;r->actors=true;
  bool ok=SmRoomRuntimeCapture(r,ram,vram);
  if (!ok) r->actors=previous;
  return ok;
}
void SmRoomRuntimePromote(const SmRoomRuntime *r,uint8_t ram[0x20000],uint16_t vram[0x8000]) {
  if (!r || r->error[0] || word(ram+0x79b)!=word(r->ram+0x79b) || word(ram+0x7bb)!=word(r->ram+0x7bb)) return;
  /* Entry-only encounters are initialized by the active cartridge loader.
   * Ordinary populations keep their resident health, AI, animation and death
   * state, including deleted slots. Never respawn a killed preview actor. */
  bool boss=false;
  for (unsigned i=0; i<32; ++i) {
    unsigned id=word(ram+0xf78+64*i);
    if (id && (r->read(r->context,0xa00010u+id) || r->read(r->context,0xa00011u+id))) boss=true;
  }
  if (r->actors && !boss) {
    memcpy(ram+0xe4c,r->ram+0xe4c,10);
    memcpy(ram+0xe84,r->ram+0xe84,0xf78-0xe84);
    memcpy(ram+0xf78,r->ram+0xf78,0x18aa-0xf78);
    memcpy(ram+0x7000,r->ram+0x7000,0x2800);
    memcpy(ram+0x198d,r->ram+0x198d,0x1c23-0x198d);
    for (unsigned i=0x6c00; i<0x8000; ++i) vram[i]=(uint16_t)word(r->vram+2*i);
  }
  if (!boss) {
    /* Restore the room's running liquid/animation phase after entry setup;
     * the native loader retains ownership of its DMA channels and scripts. */
    memcpy(ram+0x195c,r->ram+0x195c,0x198d-0x195c);
    memcpy(ram+0x1e79,r->ram+0x1e79,0x1f4b-0x1e79);
    for (unsigned i=0; i<6; ++i) {
      unsigned destination=word(r->ram+0x1f3d+2*i)*2,bytes=word(r->ram+0x1f31+2*i);
      if ((destination&1) || (bytes&1) || destination>65536 || bytes>65536-destination) continue;
      for (unsigned j=0; j<bytes; j+=2) vram[(destination+j)/2]=(uint16_t)word(r->vram+destination+j);
    }
  }
}
bool SmRoomRuntimePromoteDoors(const SmRoomRuntime *r,uint8_t ram[0x20000]) {
  if (!r || r->error[0] || word(ram+0x79b)!=word(r->ram+0x79b) || word(ram+0x7bb)!=word(r->ram+0x7bb)) return false;
  /* Encounter doors retain the actual entry scripts/lock initialization. */
  for (unsigned i=0; i<32; ++i) {
    unsigned id=word(ram+0xf78+64*i);
    if (id && (r->read(r->context,0xa00010u+id) || r->read(r->context,0xa00011u+id))) return true;
  }
  unsigned w=word(r->ram+0x7a5),h=word(r->ram+0x7a7);
  for (unsigned door=0; door<r->door_count; ++door) {
    unsigned block=r->door_blocks[door],orientation=r->door_orientations[door];
    unsigned step=orientation<2 ? w : 1;
    if (block+3*step>=w*h) return false;
    for (unsigned k=0; k<4; ++k) {
      unsigned at=block+k*step;
      ram[0x10002+2*at]=r->ram[0x10002+2*at]; ram[0x10003+2*at]=r->ram[0x10003+2*at];
      ram[0x16402+at]=r->ram[0x16402+at];
    }
    for (unsigned i=0; i<40; ++i) if (word(ram+0x1c87+2*i)==2*block && SmDoorHeader(word(ram+0x1c37+2*i)))
      { ram[0x1c37+2*i]=ram[0x1c38+2*i]=0; }
    for (unsigned i=0; i<40; ++i) if (word(r->ram+0x1c87+2*i)==2*block &&
        SmDoorHeader(word(r->ram+0x1c37+2*i)) && word(r->ram+0x1c37+2*i)!=0xc83e) {
      int slot=39; while (slot>=0 && word(ram+0x1c37+2*(unsigned)slot)) --slot;
      if (slot<0) return false;
      const unsigned arrays[]={0x1c37,0x1c87,0x1cd7,0x1d27,0x1d77,0x1dc7,0x1e17,0xde1c,0xde6c,0xdebc,0xdf0c};
      for (unsigned j=0; j<sizeof(arrays)/sizeof(*arrays); ++j) {
        unsigned src=arrays[j]+2*i,dst=arrays[j]+2*(unsigned)slot;
        ram[dst]=r->ram[src]; ram[dst+1]=r->ram[src+1];
      }
    }
  }
  return true;
}
bool SmRoomRuntimeStepEffects(SmRoomRuntime *r) {
  if (!r || r->error[0]) return false;
  if (!SmRoomRuntimeStepDoors(r)) return false;
  if (!r->actors && !call(r,0x868104,3)) return false;
  if (!call(r,0x878064,3) || !call(r,0x8dc527,3) || !call(r,0x8884b9,3)) return false;
  /* Publish the native animated-tile requests into this room's private
   * VRAM. The active machine's NMI and DMA queues are never involved. */
  for (unsigned i=0; i<6; ++i) {
    unsigned source=word(r->ram+0x1f25+2*i), bytes=word(r->ram+0x1f31+2*i);
    unsigned destination=word(r->ram+0x1f3d+2*i)*2;
    if (!source) continue;
    if (destination>sizeof(r->vram) || bytes>sizeof(r->vram)-destination || source+bytes>65536) {
      fail(r,"invalid resident animated tiles",source); return false;
    }
    for (unsigned j=0; j<bytes; ++j) r->vram[destination+j]=r->read(r->context,0x870000u+source+j);
    put(r,0x1f25+2*i,0);
  }
  ++r->ram[0x5b5];
  put(r,0x5b6,(int)word(r->ram+0x5b6)+1);
  put(r,0x5b8,(int)word(r->ram+0x5b8)+1);
  return true;
}
const uint8_t *SmRoomRuntimeRam(const SmRoomRuntime *r) { return r ? r->ram : NULL; }
const uint8_t *SmRoomRuntimeVram(const SmRoomRuntime *r) { return r ? r->vram : NULL; }
const char *SmRoomRuntimeError(const SmRoomRuntime *r) { return r ? r->error : "allocation failed"; }

/* Build-specific, pointer-free snapshots taken only between routine calls.
 * The enclosing game envelope supplies build identity and payload integrity. */
typedef struct RoomState {
  uint32_t magic, version;
  uint16_t room, state, a, x, y, sp, pc, dp, product, quotient, remainder;
  uint8_t bank, db, flags, emulation, actors;
  uint8_t ram[0x20000], vram[65536], math[8], ppu[0x34], dma[128];
  uint8_t overlays;
} RoomState;
size_t SmRoomRuntimeStateSize(void) { return sizeof(RoomState); }
bool SmRoomRuntimeSave(const SmRoomRuntime *r, void *out, size_t size) {
  if (!r || !out || size!=sizeof(RoomState) || r->error[0] || r->eproj_loan.active || r->player_loan.active || r->enemy_reaction ||
      r->cpu->sp!=0x1fff || r->cpu->pc!=0xff00) return false;
  RoomState *s=calloc(1,sizeof(*s)); if (!s) return false;
  const Interp816 *c=r->cpu;
  s->magic=0x534d5253; s->version=3; s->actors=r->actors;s->overlays=r->population!=0;
  s->room=(uint16_t)word(r->ram+0x79b); s->state=(uint16_t)word(r->ram+0x7bb);
  s->a=c->a; s->x=c->x; s->y=c->y; s->sp=c->sp; s->pc=c->pc; s->dp=c->dp;
  s->bank=c->k; s->db=c->db; s->emulation=c->e;
  s->flags=c->c | c->z<<1 | c->i<<2 | c->d<<3 | c->xf<<4 | c->mf<<5 | c->v<<6 | c->n<<7;
  s->product=r->product; s->quotient=r->quotient; s->remainder=r->remainder;
  memcpy(s->ram,r->ram,sizeof(s->ram)); memcpy(s->math,r->math,sizeof(s->math));
  memcpy(s->vram,r->vram,sizeof(s->vram));
  memcpy(s->ppu,r->ppu_registers,sizeof(s->ppu)); memcpy(s->dma,r->dma_registers,sizeof(s->dma));
  memcpy(out,s,sizeof(*s)); free(s); return true;
}
bool SmRoomRuntimeValidate(const void *in, size_t size, uint16_t room, uint16_t state, bool actors) {
  if (!in || size!=sizeof(RoomState)) return false;
  RoomState *s=malloc(sizeof(*s)); if (!s) return false;
  memcpy(s,in,sizeof(*s));
  bool ok=!(s->magic!=0x534d5253 || s->version!=3 || s->actors!=actors || s->overlays>1 ||
      (s->overlays && !s->actors) || s->sp!=0x1fff || s->pc!=0xff00 ||
      s->emulation>1 || (s->emulation && (s->flags&0x30)!=0x30) ||
      s->room!=room || s->state!=state ||
      s->room!=word(s->ram+0x79b) || s->state!=word(s->ram+0x7bb));
  free(s); return ok;
}
bool SmRoomRuntimeLoad(SmRoomRuntime *r, const void *in, size_t size) {
  if (!r || !in || size!=sizeof(RoomState)) return false;
  bool actors=((const uint8_t *)in)[offsetof(RoomState,actors)]!=0;
  if (!SmRoomRuntimeValidate(in,size,(uint16_t)word(r->ram+0x79b),(uint16_t)word(r->ram+0x7bb),actors)) return false;
  RoomState *s=malloc(sizeof(*s)); if (!s) return false;
  memcpy(s,in,sizeof(*s));
  Interp816 *c=r->cpu;
  c->a=s->a; c->x=s->x; c->y=s->y; c->sp=s->sp; c->pc=s->pc; c->dp=s->dp;
  c->k=s->bank; c->db=s->db; c->e=s->emulation!=0;
  interp816_setFlags(c,s->flags);
  c->waiting=c->stopped=c->irqWanted=c->nmiWanted=false;
  r->product=s->product; r->quotient=s->quotient; r->remainder=s->remainder;
  memcpy(r->ram,s->ram,sizeof(s->ram)); memcpy(r->math,s->math,sizeof(s->math));
  memcpy(r->vram,s->vram,sizeof(s->vram));
  memcpy(r->ppu_registers,s->ppu,sizeof(s->ppu)); memcpy(r->dma_registers,s->dma,sizeof(s->dma));
  r->actors=s->actors!=0;
  if (s->overlays) prepare_population(r);else r->population=r->tileset=0;
  r->simulation_enemies=r->interaction_enemies=UINT32_MAX;
  r->simulation_projectiles=r->interaction_projectiles=(1u<<18)-1;
  r->error[0]=0;
  free(s); return true;
}
