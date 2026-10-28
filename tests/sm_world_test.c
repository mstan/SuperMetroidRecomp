/* Synthetic cartridge fixtures exercise the public read-only world model. */
#include "sm_world.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static uint8_t rom[0x300000], ram[0x20000], saved[0x20000];
static SmWorld world;
static unsigned Offset(uint32_t a) { return ((a>>16)&127)*32768u+(a&32767); }
static uint8_t Read(void *p, uint32_t a) { (void)p; return (a&0x8000) && Offset(a)<sizeof(rom) ? rom[Offset(a)] : 0; }
static void Byte(uint32_t a, uint8_t b) { rom[Offset(a)]=b; }
static void Word(uint32_t a, uint16_t w) { Byte(a,(uint8_t)w); Byte(a+1,(uint8_t)(w>>8)); }
static void Long(uint32_t a, uint32_t v) { Word(a,(uint16_t)v); Byte(a+2,(uint8_t)(v>>16)); }
static void Ram(unsigned a, uint16_t v) { ram[a]=(uint8_t)v; ram[a+1]=(uint8_t)(v>>8); }
static void Header(uint32_t a, uint8_t area, uint8_t x, uint8_t y, uint16_t doors) {
  Byte(a+1,area); Byte(a+2,x); Byte(a+3,y); Byte(a+4,1); Byte(a+5,1); Word(a+9,doors);
}
static void State(uint16_t state, uint16_t plms) {
  Long(0x8f0000u+state,0xc0fffe); Word(0x8f0000u+state+20,plms);
}
static void Fixture(void) {
  memset(rom,0,sizeof(rom)); memset(ram,0,sizeof(ram));
  Header(0x8f91f8,0,23,0,0x927b); Header(0x8f9400,1,3,4,0x9450);
  Word(0x8f9203,0xe612); Byte(0x8f9205,0x14); Word(0x8f9206,0x9350);
  Word(0x8f9208,0xe5e6); State(0x920a,0x9280); State(0x9350,0x9280);
  Word(0x8f940b,0xe5e6); State(0x940d,0x9460);
  Word(0x8f927b,0x9000); Word(0x8f9450,0x900c);
  Word(0x839000,0x9400); Byte(0x839003,4);
  Word(0x83900c,0x91f8); Byte(0x83900f,5);
  Word(0x8f9280,0xeed7); Byte(0x8f9282,4); Byte(0x8f9283,6); Word(0x8f9284,7);
  Word(0x8f9286,0xc836); Byte(0x8f9288,2); Byte(0x8f9289,3); Word(0x8f928a,10);
  Word(0x8f9460,0xb76f); Byte(0x8f9462,8); Byte(0x8f9463,10);
  /* 512-byte level, a door and two crumble blocks in one map cell; remaining
   * tile/BTS bytes zero-filled. Stream crosses a LoROM bank boundary. */
  const uint8_t stream[]={1,0,2, 5,0,0x90,0,0xb0,0,0xb0, 0xe5,0xf9,0, 0xe4,0xff,0, 0xff};
  Word(0x949139,0xd000); Word(0x84d000,0xce37);
  uint32_t p=0xc0fffe;
  for (unsigned i=0; i<sizeof(stream); ++i) { Byte(p,stream[i]); ++p; if (!(p&0xffff)) p+=0x8000; }
  for (unsigned a=0; a<6; ++a) { Long(0x82964a+3*a,0xb58000); Word(0x829717+2*a,0x9800); }
  for (unsigned i=0; i<2048; ++i) Word(0xb58000+2*i,31);
  Word(0xb58000+2*(23+32),0x24); Byte(0x829800+(23+32)/8,1);
  Word(0xb58000+2*(1024+32),0x24); Byte(0x829800+(1024+32)/8,0x80);
}
int main(void) {
  Fixture(); assert(SmWorldLoad(&world,Read,NULL));
  assert(world.room_count==2 && world.door_count==2 && world.state_count==3 && world.marker_count==8);
  unsigned gates=0;
  for (unsigned i=0; i<world.marker_count; ++i) if (world.markers[i].kind==SM_MAP_GATE) {
    assert(world.markers[i].detail==SM_CAP_BLUE && world.markers[i].direction==1); ++gates;
  }
  assert(gates==2);
  unsigned drops=0;
  for (unsigned i=0; i<world.marker_count; ++i) if (world.markers[i].kind==SM_MAP_DROP) ++drops;
  assert(drops==3);
  const SmWorldRoom *room=SmWorldFindRoom(&world,0x91f8);
  assert(room && room->x==23 && room->y==1 && room->default_state==0x920a);
  assert(!SmWorldFindRoom(&world,0xffff));
  assert(world.doors[0].source==0x91f8 && world.doors[0].destination==0x9400 && world.doors[0].reciprocal);
  assert(world.doors[0].block_x==0 && world.doors[0].block_y==0);
  assert(SmWorldCellRoom(&world,0,23,1)==room && !SmWorldCellRoom(&world,0,0,0));
  int16_t layout[SM_WORLD_ROOMS][2]; assert(!SmWorldLayout(&world,layout));
  assert(layout[0][0]==23 && layout[0][1]==1 && layout[1][0]==24 && layout[1][1]==1);
  assert(SmWorldRoomState(room,Read,NULL,ram)==0x920a);
  ram[0xd822]=0x10; assert(SmWorldRoomState(room,Read,NULL,ram)==0x9350);
  Ram(0x79b,0x91f8); Ram(0x7bb,0x9999);
  assert(SmWorldRoomState(room,Read,NULL,ram)==0x9999); /* current actual state wins */
  assert(SmWorldTile(&world,0,23,1)==0x24 && SmWorldTile(&world,0,32,1)==0x24);
  assert(SmWorldTile(&world,6,23,1)==31 && SmWorldTile(&world,0,-1,1)==31);
  assert(!SmWorldExplored(ram,0,23,1) && !SmWorldKnown(&world,ram,0,23,1,false,false));
  ram[0xcd52+(23+32)/8]=1; /* stale saved copy for current area is ignored */
  assert(!SmWorldExplored(ram,0,23,1));
  ram[0x7f7+(23+32)/8]=1;
  assert(SmWorldExplored(ram,0,23,1));
  Ram(0x79f,1); assert(SmWorldExplored(ram,0,23,1)); /* another area's saved copy */
  assert(!SmWorldExplored(ram,0,32,1));
  assert(SmWorldKnown(&world,ram,0,32,1,true,false));
  assert(!SmWorldKnown(&world,ram,0,33,1,true,true)); /* blank stays blank */
  ram[0xd908]=1; assert(SmWorldKnown(&world,ram,0,32,1,false,false));
  const SmWorldMarker *item=NULL;
  for (unsigned i=0; i<world.marker_count; ++i) if (world.markers[i].kind==SM_MAP_ITEM) { item=&world.markers[i]; break; }
  assert(item && item->detail==0 && !SmWorldItemCollected(ram,item));
  ram[0xd870]=0x80; assert(SmWorldItemCollected(ram,item));
  memcpy(saved,ram,sizeof(ram));
  SmWorldRoomState(room,Read,NULL,ram); SmWorldKnown(&world,ram,0,23,1,true,true); SmWorldItemCollected(ram,item);
  assert(!memcmp(saved,ram,sizeof(ram)));
  Word(0x84d000,0xc7b1); /* another special-block setup is not a down-only floor */
  assert(SmWorldLoad(&world,Read,NULL) && world.marker_count==5);
  Word(0x8f9203,0xffff); assert(!SmWorldLoad(&world,Read,NULL)); assert(strstr(world.error,"selector"));
  Fixture(); Byte(0xc0fffe,0x80); /* invalid absolute copy before any output */
  assert(!SmWorldLoad(&world,Read,NULL)); assert(strstr(world.error,"compressed level"));
  puts("read-only world model boundaries passed"); return 0;
}
