#include "sm_seamless.h"
#include "sm_rooms.h"
#include "sm_door.h"
#include "sm_activity.h"
#include "common_rtl.h"
#include "cpu_state.h"
#include "snes/interp816.h"
#include "snes/ppu.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern CpuState g_cpu;
typedef struct EntryBus {
  CpuState bus;
  Interp816 *cpu;
  uint8_t math[8];
  uint16_t quotient,remainder;
} EntryBus;
static unsigned word(const uint8_t *p) { return p[0] | p[1]<<8; }
static void put(unsigned at,int value) {
  g_ram[at]=(uint8_t)value; g_ram[at+1]=(uint8_t)((unsigned)value>>8);
}
static uint8_t read_entry(void *context,uint32_t address) {
  EntryBus *b=context; unsigned lo=address&65535;
  if (!((address>>16)&0x40)) switch (lo) {
    case 0x4214: return (uint8_t)b->quotient;
    case 0x4215: return (uint8_t)(b->quotient>>8);
    case 0x4216: return (uint8_t)b->remainder;
    case 0x4217: return (uint8_t)(b->remainder>>8);
  }
  return cpu_read8(&b->bus,(uint8_t)(address>>16),(uint16_t)address);
}
static void write_entry(void *context,uint32_t address,uint8_t value) {
  EntryBus *b=context; unsigned lo=address&65535;
  if (!((address>>16)&0x40) && lo>=0x4202 && lo<=0x4206) {
    b->math[lo-0x4200]=value;
    if (lo==0x4203) b->remainder=b->math[2]*value;
    if (lo==0x4206) {
      unsigned dividend=b->math[4] | b->math[5]<<8;
      b->quotient=value ? (uint16_t)(dividend/value) : 65535;
      b->remainder=value ? (uint16_t)(dividend%value) : (uint16_t)dividend;
    }
    return;
  }
  cpu_write8(&b->bus,(uint8_t)(address>>16),(uint16_t)address,value);
}
/* Entry work executes real ROM instructions on the active bus, without
 * advancing simulation ticks, NMI, gameplay timers or the SPC timeline.
 * Keep the suspended main line's registers and return frames intact. These
 * selected subroutines contain no waits; an unexpected wait is a hard error. */
static bool entry_call(EntryBus *b,uint32_t address,unsigned frame) {
  Interp816 *c=b->cpu; unsigned top=g_cpu.S;
  c->sp=(uint16_t)top; c->pc=(uint16_t)address; c->k=(uint8_t)(address>>16);
  /* These ROM entry points use the caller's system-bank mirror for absolute
   * PPU/DMA registers. DB=$7E would silently write $7E:2115/$7E:420B RAM,
   * leaving the native tilemap/OAM transfers unapplied. */
  c->db=c->k; c->dp=0; c->mf=c->xf=c->e=c->d=false; c->i=true;
  c->waiting=c->stopped=c->irqWanted=c->nmiWanted=false;
  if (frame==3) g_ram[c->sp--]=c->k;
  g_ram[c->sp--]=0xfe; g_ram[c->sp--]=0xff;
  for (unsigned i=0; i<2000000; ++i) {
    if (c->pc==0xff00) { if (c->sp==top) return true; break; }
    if (c->waiting || c->stopped || c->sp>top || c->sp<0x1f50 ||
        ((c->k&127)==0 && (c->pc==0x8338 || c->pc==0x8343))) break;
    interp816_runOpcode(c);
  }
  fprintf(stderr,"[sm-seamless] entry $%06X did not return: $%02X:%04X S=%04X\n",
      (unsigned)address,c->k,c->pc,c->sp);
  return false;
}
static bool enemy_tiles(EntryBus *b) {
  unsigned list=word(g_ram+0x7d1), destination=0x7000;
  if (!list) return true;
  for (unsigned i=0; i<32; ++i) {
    unsigned at=0xb40000u+list+i*4;
    unsigned id=read_entry(b,at) | read_entry(b,at+1)<<8;
    if (id==65535) return true;
    unsigned def=0xa00000u+id;
    unsigned size=read_entry(b,def) | read_entry(b,def+1)<<8;
    unsigned flags=read_entry(b,at+2) | read_entry(b,at+3)<<8;
    unsigned target=(size&0x8000) ? (((flags&0xf000)>>4)|0x6000) : destination;
    size&=0x7fff;
    if (target>0x8000 || size>0x10000-target*2) return false;
    uint32_t source=read_entry(b,def+0x36) | read_entry(b,def+0x37)<<8 |
        (uint32_t)read_entry(b,def+0x38)<<16;
    for (unsigned j=0; j<size; j+=2) g_ppu->vram[target+j/2]=
        (uint16_t)(read_entry(b,source+j) | read_entry(b,source+j+1)<<8);
    if (!(read_entry(b,def+1)&0x80)) destination+=size/2;
  }
  return false;
}
static int clamp(int v,int high) { return v<0 ? 0 : v>high ? high : v; }
int SmActiveEnemyBlock(uint8_t source[0x20000],int dx,int dy,int x,int y,bool horizontal) {
  unsigned index=word(source+0xe54),w=word(g_ram+0x7a5),h=word(g_ram+0x7a7);
  if (index>=2048 || (index&63) || x<0 || y<0 || x>=(int)w || y>=(int)h || (uint64_t)w*h>0x3200) return -2;
  EntryBus b={0};b.bus=g_cpu;b.cpu=interp816_init(&b,read_entry,write_entry);
  if (!b.cpu) return -2;
  interp816_set_brk_hook_enabled(b.cpu,false);
  uint8_t scratch[64],actor[SM_ENEMY_BYTES],stack[256];
  memcpy(scratch,g_ram,sizeof(scratch));memcpy(actor,g_ram+SM_ENEMY_POOL+index,sizeof(actor));
  memcpy(stack,g_ram+0x1f00,sizeof(stack));
  const unsigned work[]={0xe54,0xdc4,0xdd4,0xdd6,0x1e77,0x1c27,0x1c29,0x1c2b};
  uint16_t saved[sizeof(work)/sizeof(*work)];
  for (unsigned i=0; i<sizeof(work)/sizeof(*work); ++i) saved[i]=(uint16_t)word(g_ram+work[i]);
  SmEnemyInput(g_ram,source,dx,dy,horizontal);
  b.cpu->x=(uint16_t)(2*((unsigned)y*w+(unsigned)x));b.cpu->a=(uint16_t)word(g_ram+0x10002+b.cpu->x);
  bool ok=entry_call(&b,horizontal ? 0xa0c845 : 0xa0c879,2);int result=ok ? (int)b.cpu->c : -2;
  if (ok) SmEnemyOutput(source,g_ram,index,-dx,-dy,horizontal);
  memcpy(g_ram,scratch,sizeof(scratch));memcpy(g_ram+SM_ENEMY_POOL+index,actor,sizeof(actor));
  memcpy(g_ram+0x1f00,stack,sizeof(stack));
  for (unsigned i=0; i<sizeof(work)/sizeof(*work); ++i) put(work[i],saved[i]);
  interp816_free(b.cpu);return result;
}
bool SmActiveDoorEvent(unsigned x,unsigned y,unsigned orientation,bool closing) {
  unsigned w=word(g_ram+0x7a5),h=word(g_ram+0x7a7);
  if (orientation>=4 || x>=w || y>=h || (uint64_t)w*h>0x3200) return false;
  EntryBus b={0}; b.bus=g_cpu; b.cpu=interp816_init(&b,read_entry,write_entry);
  if (!b.cpu) return false;
  interp816_set_brk_hook_enabled(b.cpu,false);
  uint8_t scratch[64]; memcpy(scratch,g_ram,sizeof(scratch));
  const unsigned work[]={0xdc4,0xdde,0x1c27,0x1c29,0x1c2b};
  uint16_t saved[sizeof(work)/sizeof(*work)];
  for (unsigned i=0; i<sizeof(work)/sizeof(*work); ++i) saved[i]=(uint16_t)word(g_ram+work[i]);
  unsigned block=y*w+x;
  for (unsigned i=0; i<40; ++i)
    if (word(g_ram+0x1c87+2*i)==2*block && SmDoorHeader(word(g_ram+0x1c37+2*i))) put(0x1c37+2*i,0);
  bool restored=false,ok=true;
  if (closing) {
    unsigned cursor=word(RomPtr(0x8f0014u+word(g_ram+0x7bb)));
    for (unsigned i=0; (cursor&0x8000) && i<256; ++i,cursor+=6) {
      const uint8_t *entry=RomPtr(0x8f0000u+cursor); unsigned header=word(entry);
      if (!header) break;
      if (!SmDoorHeader(header) || entry[2]!=x || entry[3]!=y) continue;
      b.cpu->x=(uint16_t)cursor; ok=entry_call(&b,0x84846a,3); restored=true;
      if (ok) put(0x1d27+word(g_ram+0x1c27),(int)word(RomPtr(0x840004u+header)));
      break;
    }
  }
  if (!restored) {
    unsigned index=word(g_ram+0xdde); put(0xdc4,(int)block); put(0xdde,65535);
    b.cpu->a=(uint16_t)(closing ? 0xc8ba+4*orientation : word(RomPtr(0x949ea6u+2*(0x40+orientation))));
    ok=entry_call(&b,0x8484e7,3); put(0xdde,(int)index);
  }
  /* A native message/PLM can be suspended across this frame boundary. Keep
   * its direct-page temporaries and current work indices as well as its CPU
   * registers/return stack; only the actual door state belongs to this event. */
  memcpy(g_ram,scratch,sizeof(scratch));
  for (unsigned i=0; i<sizeof(work)/sizeof(*work); ++i) put(work[i],saved[i]);
  interp816_free(b.cpu); return ok;
}
static bool open_entrance(EntryBus *b,const uint8_t *definition) {
  if (!SmDoorHasArrivalCap(definition[3])) return true;
  unsigned block=definition[5]*word(g_ram+0x7a5)+definition[4];
  if (block>=word(g_ram+0x7a5)*word(g_ram+0x7a7)) return false;
  /* A valid door-block crossing already passed the outgoing cap. Play the
   * cartridge's blue-cap opening on its paired entrance rather than closing
   * it around the player's body. No synthetic shot/ammo is granted. */
  unsigned orientation=(definition[3]&3)^1;
  unsigned header=read_entry(b,0x949ea6+2*(0x40+orientation)) |
      read_entry(b,0x949ea7+2*(0x40+orientation))<<8;
  for (unsigned i=0; i<40; ++i) {
    unsigned h=word(g_ram+0x1c37+2*i);
    if (word(g_ram+0x1c87+2*i)==2*block && SmDoorHeader(h)) put(0x1c37+2*i,0);
  }
  unsigned index=word(g_ram+0xdde);
  put(0xdc4,(int)block); put(0xdde,65535); b->cpu->a=(uint16_t)header;
  bool ok=entry_call(b,0x8484e7,3);
  put(0xdde,(int)index);
  return ok && entry_call(b,0x8485b4,3);
}
bool SmSeamlessDoor(void) {
  SmRoomPlacement p;
  uint16_t crossed_door=0;
  if (SmRoomsUpwardDoorCarry(g_ram) && word(g_ram+0xb2e)<4) {
    put(0xb2c,0); put(0xb2e,4);
  }
  bool crossing=word(g_ram+0x998)==8 && SmRoomsCrossing(g_ram,&crossed_door,&p);
  if (crossing) { put(0x78d,crossed_door); put(0x998,9); }
  else if (word(g_ram+0x998)!=9 || !SmRoomsDoor((uint16_t)word(g_ram+0x78d),&p)) return true;
  unsigned door=word(g_ram+0x78d);
  const uint8_t *definition=RomPtr(0x830000u+door);
  if ((definition[2]&0x80) || word(definition)!=p.room) return true;
  unsigned arrival_phase=SmRoomsDoorPhase((uint16_t)door);
  EntryBus b={0}; b.bus=g_cpu;
  b.cpu=interp816_init(&b,read_entry,write_entry); if (!b.cpu) return false;
  uint8_t *previous=malloc(0x20000); uint16_t *previous_vram=malloc(0x10000);
  if (!previous || !previous_vram) { free(previous); free(previous_vram); interp816_free(b.cpu); return false; }
  memcpy(previous,g_ram,0x20000); memcpy(previous_vram,g_ppu->vram,0x10000);
  size_t backup_bytes=RtlSaveSnapshotToMemory(NULL,0);
  void *backup=backup_bytes && backup_bytes<=16*1024*1024 ? malloc(backup_bytes) : NULL;
  if (!backup || RtlSaveSnapshotToMemory(backup,backup_bytes)!=backup_bytes) {
    free(backup); free(previous); free(previous_vram); interp816_free(b.cpu); return false;
  }
  interp816_set_brk_hook_enabled(b.cpu,false);
  int sx=(int16_t)word(g_ram+0xaf6)-p.x, sy=(int16_t)word(g_ram+0xafa)-p.y;
  /* Native scrolling tests the authored screen under the camera's center.
   * Carrying the old world camera into its registers can put that center in
   * padding (9FE5 -> A011), where native autoscroll drives it off the room.
   * Start at the cartridge's destination screen; SmRoomsHandoff carries the
   * departing world view separately, preserving continuous presentation. */
  int cx=definition[6]*256,cy=definition[7]*256+((definition[3]&3)==3 ? 32 : 0);
  bool ok=true;
  const struct { uint32_t pc; unsigned frame; } setup[]={
    {0x82ddf1,2},{0x82de12,2},{0x82de6f,2},{0x82def2,2},
    /* The native transition clears old HDMA objects before initializing the
     * next room. Keep that bounded part, without its vblank wait. */
    {0x8882ac,3},{0x8882c1,3},{0x878016,3},{0x868016,3},{0x8dc4d8,3},{0x8483c3,3},
    {0x82e783,3},{0x82e7d3,3},{0x89ab82,3},
    {0x90ac8d,3},{0x82e139,3},{0xa08a1e,3},{0xa08a9e,3},{0x91dee6,3},
  };
  /* Remove stale transfer requests before the destination fills its queues. */
  put(0x330,0);
  for (unsigned i=0; ok && i<sizeof(setup)/sizeof(*setup); ++i)
    ok=entry_call(&b,setup[i].pc,setup[i].frame);
  if (ok) {
    /* Door entry scripts and boss setup have now run once, on actual entry.
     * Native coordinates remain bounded; the presentation camera is rebased
     * separately from the active room's safe tilemap/collision coordinates. */
    unsigned rx=word(g_ram+0xafe),ry=word(g_ram+0xb00);
    if (rx>32 || ry>48) ok=false;
    if (crossing) {
      /* Ownership changes at the body center's actual door-plane crossing.
       * The surrounding native collision samples can still reach either
       * room; never move the body forward to clear an entrance trigger. */
      if (sx<0 || sy<0 || sx>=p.width || sy>=p.height) ok=false;
      put(0xaf6,sx); put(0xafa,sy);
    } else {
      /* Explicit native-transition/debug entries retain their bounded setup. */
      int min_x=16+(int)rx,min_y=16+(int)ry;
      put(0xaf6,sx<min_x ? min_x : clamp(sx,p.width-min_x));
      put(0xafa,sy<min_y ? min_y : clamp(sy,p.height-min_y));
    }
    put(0xb10,word(g_ram+0xaf6)); put(0xb14,word(g_ram+0xafa));
    put(0x911,clamp(cx,p.width-256)); put(0x915,clamp(cy,p.height-256));
    put(0x91d,0); put(0x91f,0); put(0x921,0); put(0x923,0);
    bool boss=false;
    for (unsigned i=0; i<32; ++i) {
      unsigned id=word(g_ram+0xf78+64*i);
      if (id && word(RomPtr(0xa00010u+id))) boss=true;
    }
    ok=ok && enemy_tiles(&b) && ((arrival_phase==SM_DOOR_OPEN || arrival_phase==SM_DOOR_OPENING) && !boss ? true :
       entry_call(&b,0x82e8eb,2) && open_entrance(&b,definition)) && entry_call(&b,0x82e97c,3) &&
       entry_call(&b,0x80a2f9,2) && entry_call(&b,0x80a33a,2) &&
       entry_call(&b,0x80a37b,2) && entry_call(&b,0x80a176,3) &&
       entry_call(&b,0x88d865,3) && entry_call(&b,0x82e071,3) && entry_call(&b,0x82e0d5,3);
  }
  if (ok) {
    bool redraw=false;
    ok=SmRoomsHandoff(previous,previous_vram,g_ram,g_ppu->vram,&p,definition[3]&3,&redraw);
    /* Completed hatch openings and permanent grapple breaks may have no
     * PLM left to repaint them. Refresh the loader's native column upload. */
    if (ok && redraw) ok=entry_call(&b,0x80a176,3);
  }
  if (ok) {
    /* This loader deliberately skips ResetProjectileData. Keep the single
     * native player pool (including super-missile links) and rebase its live
     * positions instead of leaving shots in the departing room's coordinates. */
    SmActivityRebaseProjectiles(g_ram,p.x,p.y);
    memcpy(g_ram+0xc000,g_ram+0xc200,512);
    put(0x795,0); put(0x797,0); put(0x799,0); put(0xe16,0); put(0xe18,0);
    put(0x998,8); put(0x99c,0xe737); put(0x931,0);
    g_ram[0x51]=15;
    /* NMI must receive a destination-owned OAM publication immediately.
     * Reusing the departing room's screen coordinates shifts Samus by an
     * entire screen when the continuous camera is rebased. */
    put(0x590,0); memset(g_ram+0x570,0,32);
    for (unsigned i=0; i<128; ++i) put(0x370+4*i,0xf000);
    ok=entry_call(&b,0xa0884d,3) && entry_call(&b,0x82dfc7,2);
    if (ok && word(g_ram+0x79b)==0xdf45 && word(g_ram+0x7df)==0xe51f &&
        (g_ram[0x56]&7)==7 && word(g_ram+0x783)) {
      /* A native transition processes this scenic HDMA setup before its
       * first gameplay raster. Its initial D949 instruction selects Mode1;
       * prime that one list now, then publish its native table/mode mirror.
       * The music queue, gameplay handlers and timers do not run here. */
      unsigned saved_index=word(g_ram+0x18b2);bool initialized=false;
      for (unsigned i=0; ok && i<12; i+=2) if (word(g_ram+0x18cc+i)==0xd939) {
        put(0x18b2,(int)i);b.cpu->x=(uint16_t)i;ok=entry_call(&b,0x88851c,2);
        if (ok) {
          g_ram[0x85]|=(uint8_t)word(g_ram+0x18b4+i);
          cpu_write16(&b.bus,0,(uint16_t)(0x4302+(word(g_ram+0x18c0+i)&255)),(uint16_t)word(g_ram+0x18d8+i));
          initialized=true;
        }
        break;
      }
      put(0x18b2,(int)saved_index);
      ok=ok && initialized && entry_call(&b,0x8091ee,2);
      if (ok) cpu_write8(&b.bus,0,0x420c,g_ram[0x85]);
    }
  }
  if (ok) {
    /* The usual NMI uploads destination palette, OAM and pending requests. */
    fprintf(stderr,"[sm-seamless] entered %04X via %04X without transition ticks\n",p.room,door);
  }
  if (!ok) {
    /* A room-specific script may require a real wait/cutscene. Restore the
     * complete machine, including devices and native saves, before allowing
     * the normal door scheduler to execute that connection. Never retry a
     * partly installed room at the following frame boundary. */
    fprintf(stderr,"[sm-seamless] restoring native transition for %04X via %04X\n",p.room,door);
    ok=RtlLoadSnapshotFromMemory(backup,backup_bytes)!=0;
  }
  free(backup); free(previous); free(previous_vram); interp816_free(b.cpu); return ok;
}
