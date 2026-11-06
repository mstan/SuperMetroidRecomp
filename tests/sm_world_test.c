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
static void CeresLevel(uint32_t source,unsigned width,unsigned height,bool missing,bool ambiguous) {
  uint8_t data[2+3*768]={0};unsigned blocks=width*height*256,bytes=blocks*2,size=2+blocks*3;
  data[0]=(uint8_t)bytes;data[1]=(uint8_t)(bytes>>8);
  if (!missing) for (unsigned n=0; n<(ambiguous ? 2u : 1u); ++n) for (unsigned row=0; row<4; ++row) {
    unsigned block=height==3 ? (38+row)*16+15 : (6+4*n+row)*32;
    data[2+2*block]=0x40;data[3+2*block]=0x90;data[2+bytes+block]=(uint8_t)n;
  }
  for (unsigned p=0; p<size;) {
    unsigned count=size-p;if (count>1024) count=1024;
    Byte(source++,(uint8_t)(0xe0 | ((count-1)>>8)));Byte(source++,(uint8_t)(count-1));
    for (unsigned n=0; n<count; ++n) Byte(source++,data[p++]);
  }
  Byte(source,255);
}
static void UncappedCeres(bool missing,bool ambiguous) {
  Fixture();Header(0x8fdf45,6,12,13,0xdf80);Header(0x8fe021,6,13,13,0xe060);
  Byte(0x8fdf4a,3);Byte(0x8fe025,2);
  Word(0x8fdf50,0xe5e6);State(0xdf52,0);Word(0x8fe02c,0xe5e6);State(0xe02e,0);
  Long(0x8fdf52,0xc68000);Long(0x8fe02e,0xc78000);
  CeresLevel(0xc68000,1,3,false,false);CeresLevel(0xc78000,2,1,missing,ambiguous);
  Word(0x8fdf80,0xab4c);Word(0x8fe060,0xab58);
  Word(0x83ab4c,0xe021);Byte(0x83ab4f,0);
  Word(0x83ab58,0xdf45);Byte(0x83ab5b,1);Byte(0x83ab5f,2);
  if (ambiguous) { Word(0x8fe062,0xab64);Word(0x83ab64,0xdf45);Byte(0x83ab67,1);Byte(0x83ab6b,2); }
}
static void CappedLevel(uint32_t source,bool left) {
  uint8_t data[770]={0};data[1]=2;
  /* A buried duplicate precedes the four real hatch triggers in scan order.
   * Only the latter agree with the reciprocal native arrival cap/span. */
  unsigned buried=4*16+4;data[3+2*buried]=0x90;
  for (unsigned row=6; row<10; ++row) data[3+2*(row*16+(left ? 0 : 15))]=0x90;
  Byte(source++,0xe3);Byte(source++,1);
  for (unsigned i=0; i<sizeof(data); ++i) Byte(source++,data[i]);
  Byte(source,255);
}
static void MorphLevel(uint32_t source,bool wide) {
  uint8_t data[1538]={0};unsigned blocks=wide ? 512 : 256,size=2+3*blocks;
  data[1]=wide ? 4 : 2;
  if (wide) { data[3+2*(6*32+15)]=0x90;data[3+2*(6*32+16)]=0x90; }
  else data[3+2*(6*16+15)]=0x90;
  for (unsigned i=0; i<size;) {
    unsigned n=size-i;if (n>1024) n=1024;
    Byte(source++,(uint8_t)(0xe0|((n-1)>>8)));Byte(source++,(uint8_t)(n-1));
    while (n--) Byte(source++,data[i++]);
  }
  Byte(source,255);
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
  /* An unvisited preview knows its incoming connection. Select its actual
   * door-specific state rather than showing the default room incorrectly. */
  SmWorldRoom door_room={.header=0x9700,.default_state=0x9713};
  Word(0x8f970b,0xe5eb); Word(0x8f970d,0x9000); Word(0x8f970f,0x9350);
  Word(0x8f9711,0xe5e6);
  assert(SmWorldRoomState(&door_room,Read,NULL,ram)==0x9713);
  assert(SmWorldRoomStateForDoor(&door_room,Read,NULL,ram,0x9000)==0x9350);
  assert(SmWorldRoomStateForDoor(&door_room,Read,NULL,ram,0x900c)==0x9713);
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
  /* Native eye/guard doors need their own marker, rather than a blue cap. */
  Word(0x8f928c,0xdb48); Byte(0x8f928e,6); Byte(0x8f928f,6);
  Word(0x8f9292,0xdb56); Byte(0x8f9294,8); Byte(0x8f9295,6);
  assert(SmWorldLoad(&world,Read,NULL));
  unsigned eyes=0;
  for (unsigned i=0; i<world.marker_count; ++i) if (world.markers[i].kind==SM_MAP_CAP) {
    assert(world.markers[i].detail==SM_CAP_EYE);
    assert(world.markers[i].direction==(world.markers[i].plm==0xdb48)); ++eyes;
  }
  assert(eyes==4);
  Fixture();
  Word(0x84d000,0xc7b1); /* another special-block setup is not a down-only floor */
  assert(SmWorldLoad(&world,Read,NULL) && world.marker_count==5);
  /* An arrival/reverse header without a source block cannot make a passage
   * two-way, or impose a fabricated atlas constraint. */
  const uint8_t empty_level[]={1,0,2,0xe6,0xff,0,0xff};
  for (unsigned i=0; i<sizeof(empty_level); ++i) Byte(0xc28000+i,empty_level[i]);
  Long(0x8f940d,0xc28000);
  assert(SmWorldLoad(&world,Read,NULL));
  assert(world.doors[0].block_x==0 && world.doors[1].block_x==-1);
  assert(!world.doors[0].reciprocal && !world.doors[1].reciprocal);
  assert(!SmWorldLayout(&world,layout) && layout[1][0]==24);
  /* A scripted escape door reads its native spawn coordinates and setup
   * index, including bounds/signature checks, without executing the script. */
  Fixture(); Header(0x8fdd58,5,1,1,0xddf0);
  Word(0x8fdd63,0xe5e6); State(0xdd65,0x9280);
  Word(0x83900c,0xdd58); Word(0x8fddf0,0x9018); Word(0x8fddf2,0x9024);
  Word(0x839018,0x91f8); Word(0x839024,0x91f8);
  Byte(0xa9b333,0x22); Long(0xa9b334,0x8483d7);
  Byte(0xa9b337,0); Byte(0xa9b338,6); Word(0xa9b339,0xb677);
  Word(0x84b677,0xb5f8); Byte(0x84b5fb,0xa9); Word(0x84b5fc,0x9001);
  assert(SmWorldLoad(&world,Read,NULL));
  const SmWorldRoom *escape=SmWorldFindRoom(&world,0xdd58);
  assert(escape && world.doors[escape->first_door+1].block_x==0 && world.doors[escape->first_door+1].block_y==6);
  Byte(0xa9b338,16); assert(!SmWorldLoad(&world,Read,NULL)); assert(strstr(world.error,"escape door block"));
  Byte(0xa9b338,6); Byte(0xa9b333,0); assert(!SmWorldLoad(&world,Read,NULL)); assert(strstr(world.error,"escape door spawn"));
  /* The disconnected Ceres component extends beyond $E000 and has no map.
   * Its geometry/doors must still be available without indexing map area 6. */
  Fixture(); Header(0x8fdf45,6,12,13,0xdf80); Header(0x8fe021,6,13,13,0xe060);
  Word(0x8fdf50,0xe5e6); State(0xdf52,0); Word(0x8fe02c,0xe5e6); State(0xe02e,0);
  Word(0x8fdf80,0xab4c); Word(0x8fe060,0xab58);
  Word(0x83ab4c,0xe021); Byte(0x83ab4f,4);
  Word(0x83ab58,0xdf45); Byte(0x83ab5b,5);
  assert(SmWorldLoad(&world,Read,NULL) && world.room_count==4 && world.door_count==4);
  const SmWorldRoom *ceres=SmWorldFindRoom(&world,0xe021);
  assert(ceres && ceres->area==6 && world.doors[ceres->first_door].reciprocal);
  assert(!SmWorldKnown(&world,ram,6,13,14,true,true) && !SmWorldCellRoom(&world,6,13,14));
  /* Uncapped Ceres headers keep zero cap fields. Native arrival screens and
   * real opposed triggers must produce a symmetric multi-screen embedding. */
  UncappedCeres(false,false);assert(SmWorldLoad(&world,Read,NULL));
  const SmWorldRoom *elevator=SmWorldFindRoom(&world,0xdf45),*hall=SmWorldFindRoom(&world,0xe021);
  const SmWorldDoor *out=&world.doors[elevator->first_door],*back=&world.doors[hall->first_door];
  assert(out->reciprocal && back->reciprocal && out->arrival_x==0 && out->arrival_y==6);
  assert(back->arrival_x==15 && back->arrival_y==38 && back->arrival_screen_y==2);
  assert(!Read(NULL,0x83ab50) && !Read(NULL,0x83ab51) && !Read(NULL,0x83ab5c) && !Read(NULL,0x83ab5d));
  /* The automap layout traverses Zebes only. Check Ceres' actual forward
   * and reverse constraints here; the preview probe checks their placement. */
  assert(out->block_x/16+1-out->arrival_x/16==1 && out->block_y/16-out->arrival_y/16==2);
  assert(back->block_x/16-1-back->arrival_x/16==-1 && back->block_y/16-back->arrival_y/16==-2);
  for (unsigned ambiguous=0; ambiguous<2; ++ambiguous) {
    UncappedCeres(!ambiguous,ambiguous!=0);assert(SmWorldLoad(&world,Read,NULL));
    elevator=SmWorldFindRoom(&world,0xdf45);hall=SmWorldFindRoom(&world,0xe021);
    out=&world.doors[elevator->first_door];back=&world.doors[hall->first_door];
    assert(!out->reciprocal && !back->reciprocal && out->arrival_y==0 && back->arrival_y==0);
    assert(out->block_x==15 && out->block_y==38); /* retain real source metadata */
  }
  Fixture();Long(0x8f920a,0xc68000);Long(0x8f9350,0xc68000);Long(0x8f940d,0xc78000);
  CappedLevel(0xc68000,false);CappedLevel(0xc78000,true);
  Byte(0x839004,1);Byte(0x839005,6);Byte(0x839010,13);Byte(0x839011,6);
  assert(SmWorldLoad(&world,Read,NULL));
  assert(world.doors[0].block_x==15 && world.doors[0].block_y==6);
  assert(world.doors[1].block_x==0 && world.doors[1].block_y==6);
  assert(world.doors[0].reciprocal && world.doors[1].reciprocal);
  assert(Read(NULL,0x839004)==1 && Read(NULL,0x839005)==6 && Read(NULL,0x839010)==13 && Read(NULL,0x839011)==6);
  /* A one-cell-high capless Morph trigger straddles a screen boundary.
   * Native opposed arrival screens choose the real endpoint; zero cap
   * placeholders remain zero and the physical reciprocal gate still applies. */
  Fixture();Byte(0x8f91fc,2);Long(0x8f920a,0xc68000);Long(0x8f9350,0xc68000);Long(0x8f940d,0xc78000);
  MorphLevel(0xc68000,true);MorphLevel(0xc78000,false);
  Byte(0x839003,1);Byte(0x83900f,0);Byte(0x839012,1);
  assert(SmWorldLoad(&world,Read,NULL));
  assert(world.doors[0].block_x==16 && world.doors[0].block_y==6 && world.doors[0].arrival_x==15);
  assert(world.doors[1].block_x==15 && world.doors[1].block_y==6 && world.doors[1].arrival_x==16);
  assert(world.doors[0].reciprocal && world.doors[1].reciprocal);
  assert(!Read(NULL,0x839004) && !Read(NULL,0x839005) && !Read(NULL,0x839010) && !Read(NULL,0x839011));
  Fixture();
  Word(0x8f9203,0xffff); assert(!SmWorldLoad(&world,Read,NULL)); assert(strstr(world.error,"selector"));
  Fixture(); Byte(0xc0fffe,0x80); /* invalid absolute copy before any output */
  assert(!SmWorldLoad(&world,Read,NULL)); assert(strstr(world.error,"compressed level"));
  puts("read-only world model boundaries passed"); return 0;
}
