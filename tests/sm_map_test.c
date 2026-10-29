/* Map controls, read-only rendering, freeze/close and netplay behavior. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "mod_runtime.h"
#include "sm_mod_overlay.h"
#include "sm_mod_support.h"

static SNESModActivationCallback activate_area,activate_world,activate_mini,reset_map;
static SNESModInputTransform transform;
static SmModOverlayDraw draw_map;
static SmModOverlayDraw draw_ammo;
static SNESModActivationCallback activate_ammo;
static bool online,pause_option=true,reveal_option;
static unsigned transform_registrations,draw_registrations,hook_arms;
static uint64_t test_timeline;
static bool adapter_probe;
static uint32_t last_read;
uint64_t RtlStateGeneration(void) { return test_timeline; }
int snes_mod_register_activation_plugin(const char *id,SNESModActivationCallback fn) {
  if (strstr(id,".pause-map")) activate_area=fn;
  if (strstr(id,".world-map")) activate_world=fn;
  if (strstr(id,".minimap")) activate_mini=fn;
  return 1;
}
int snes_mod_register_reset_callback(SNESModActivationCallback fn) { reset_map=fn; return 1; }
int snes_mod_register_input_transform(SNESModInputTransform fn) { transform=fn; ++transform_registrations; return 1; }
bool SmModOverlayRegister(SmModOverlayDraw fn) { draw_map=fn; ++draw_registrations; return true; }
bool SmModOverlayRegisterPriority(SmModOverlayDraw fn,int priority) {
  if (priority==20) { draw_ammo=fn; return true; }
  assert(priority==10); return SmModOverlayRegister(fn);
}
int snes_mod_register_presentation_plugin(const char *id,SNESModActivationCallback fn) {
  if (strstr(id,".ammo-totals")) activate_ammo=fn;
  return 1;
}
int snes_netplay_active(void) { return online; }
bool SmPauseOptions_Visible(void) { return false; }
int snes_mod_runtime_feature_enabled_c(const char *pkg,const char *feature) {
  assert(!strcmp(pkg,"super-metroid.map-hud") && !strcmp(feature,"reveal-map")); return reveal_option;
}
const char *SmModOption(SmModValue *out,const char *pkg,const char *feature,const char *option,const char *fallback) {
  (void)pkg; (void)feature;
  snprintf(out->text,sizeof(out->text),"%s",!strcmp(option,"background") ? (pause_option?"paused":"live") : fallback);
  return out->text;
}
size_t SmModArmHooks(const char *name,const uint32_t *pcs,size_t count) {
  assert(!strcmp(name,"minimap") && count==1 && pcs[0]==0x828b47); ++hook_arms; return count;
}
uint16_t SmModReadRom16(uint32_t at) { last_read=at; return adapter_probe ? 0x1234 : 0; }

#include "../src/mods/sm_map_plugin.c"
#include "../src/mods/sm_hud_plugins.c"

enum { TEST_WIDTH=256, TEST_STRIDE=272, TEST_HEIGHT=224, GUARD=64 };
static uint8_t test_ram[0x20000],ram_copy[0x20000];
static uint32_t frame[GUARD+TEST_STRIDE*TEST_HEIGHT+GUARD];
static void Put(unsigned a,uint16_t v) { test_ram[a]=(uint8_t)v; test_ram[a+1]=(uint8_t)(v>>8); }
static void Fixture(void) {
  memset(&world,0,sizeof(world)); memset(test_ram,0,sizeof(test_ram)); ready=true;
  world.room_count=1; world.rooms[0]=(SmWorldRoom){0x91f8,0x920a,0,0,0,23,1,2,1};
  layout[0][0]=23; layout[0][1]=1;
  for (unsigned a=0; a<6; ++a) for (unsigned i=0; i<2048; ++i) world.tiles[a][i]=31;
  world.tiles[0][55]=world.tiles[0][56]=0x24;
  world.room_at[0][55]=world.room_at[0][56]=1;
  world.station_map[0][6]=1; world.station_map[0][7]=0x80;
  Put(0x79b,0x91f8); Put(0x7bb,0x920a); Put(0x998,15); Put(0x51,15);
  Put(0xaf6,16); Put(0xafa,16); test_ram[0x7f7+6]=1;
  previous=0;
}
static void Fill(uint32_t color) { for (unsigned i=0; i<sizeof(frame)/sizeof(*frame); ++i) frame[i]=color; }
static void Guards(uint32_t color) {
  for (unsigned i=0; i<GUARD; ++i) assert(frame[i]==color && frame[GUARD+TEST_STRIDE*TEST_HEIGHT+i]==color);
  for (unsigned y=0; y<TEST_HEIGHT; ++y) for (unsigned x=TEST_WIDTH; x<TEST_STRIDE; ++x)
    assert(frame[GUARD+y*TEST_STRIDE+x]==color);
}
int main(void) {
  adapter_probe=true;
  assert(Read(NULL,0xc0ffff)==0x12 && last_read==0xc0fffe);
  assert(Read(NULL,0xc18000)==0x34 && last_read==0xc18000);
  adapter_probe=false;
  assert(activate_area && activate_world && activate_mini && reset_map);
  activate_area(); activate_world(); activate_mini();
  assert(transform_registrations==1 && draw_registrations==1 && hook_arms==1);
  Fixture(); SnesModInputContext input={.player=0,.wram=test_ram};
  memcpy(ram_copy,test_ram,sizeof(test_ram));
  assert(!(transform(SNES_PAD_X,&input)&SNES_PAD_X) && SmMapFullScreen(test_ram));
  transform(SNES_PAD_X,&input); assert(SmMapFullScreen(test_ram)); /* held X is one edge */
  transform(0,&input); transform(SNES_PAD_R,&input); assert(scales[zoom]==4);
  transform(0,&input); int before=center_x16; transform(SNES_PAD_RIGHT,&input); assert(center_x16>before);
  transform(0,&input); transform(SNES_PAD_Y,&input); assert(center_x16==23*16+1 && center_y16==17);
  Fill(0xff102030); draw_map(frame+GUARD,TEST_STRIDE,TEST_WIDTH,TEST_HEIGHT,0,test_ram); Guards(0xff102030);
  assert(!memcmp(ram_copy,test_ram,sizeof(test_ram)));
  transform(0,&input); assert(!(transform(SNES_PAD_B,&input)&SNES_PAD_B) && !SmMapFullScreen(test_ram));
  assert(transform(SNES_PAD_R,&input)==SNES_PAD_R); /* native tabs still receive shoulders */
  transform(0,&input); transform(SNES_PAD_X,&input); transform(0,&input);
  assert(transform(SNES_PAD_START,&input)&SNES_PAD_START); assert(!SmMapFullScreen(test_ram));
  assert(world_closing);
  Put(0x998,16); Put(0x51,7);
  Fill(0xff102030); draw_map(frame+GUARD,TEST_STRIDE,TEST_WIDTH,TEST_HEIGHT,0,test_ram);
  assert(frame[GUARD]==0xff03070e); Guards(0xff102030); /* same world page fades out */
  Put(0x998,8); transform(0,&input); assert(!world_closing);
  Put(0x998,15); Put(0x51,15);
  input.player=1; assert(transform(SNES_PAD_X,&input)==SNES_PAD_X); input.player=0;
  /* The replacement covers every native map fade, with native brightness.
   * Equipment, gameplay and native borders must never be covered/dimmed. */
  world_open=false;
  Fill(0xff102030); draw_map(frame+GUARD,TEST_STRIDE,TEST_WIDTH,TEST_HEIGHT,0,test_ram);
  static uint32_t full[TEST_STRIDE*TEST_HEIGHT];
  memcpy(full,frame+GUARD,sizeof(full));
  const unsigned phases[]={14,15,16},levels[]={0,1,7,14,15,0x80,0x8f};
  for (unsigned s=0; s<3; ++s) for (unsigned l=0; l<7; ++l) {
    Put(0x998,(uint16_t)phases[s]); Put(0x51,(uint16_t)levels[l]);
    Fill(0xff102030); draw_map(frame+GUARD,TEST_STRIDE,TEST_WIDTH,TEST_HEIGHT,0,test_ram);
    Guards(0xff102030);
    unsigned level=(levels[l]&0x80) ? 0 : levels[l]&15;
    for (unsigned y=0; y<TEST_HEIGHT; ++y) for (unsigned x=0; x<TEST_WIDTH; ++x) {
      bool owned=y<36 || (y>=38 && y<194 && x>=8 && x<248);
      uint32_t c=full[y*TEST_STRIDE+x],want=c;
      if (owned) want=(c&0xff000000u)|((((c>>16)&255)*level/15)<<16)|
                         ((((c>>8)&255)*level/15)<<8)|((c&255)*level/15);
      assert(frame[GUARD+y*TEST_STRIDE+x]==want);
    }
  }
  const unsigned other_states[]={8,12,13,17,18};
  for (unsigned i=0; i<5; ++i) {
    Put(0x998,(uint16_t)other_states[i]); Put(0x51,15);
    Fill(0xff102030); draw_map(frame+GUARD,TEST_STRIDE,TEST_WIDTH,TEST_HEIGHT,0,test_ram);
    for (unsigned n=0; n<sizeof(frame)/sizeof(*frame); ++n) assert(frame[n]==0xff102030);
  }
  Put(0x998,15); Put(0x763,1);
  Fill(0xff102030); draw_map(frame+GUARD,TEST_STRIDE,TEST_WIDTH,TEST_HEIGHT,0,test_ram);
  for (unsigned n=0; n<sizeof(frame)/sizeof(*frame); ++n) assert(frame[n]==0xff102030);
  Put(0x763,0); Put(0x51,15);
  /* A view cannot reveal an undiscovered corridor through its markers. */
  world.marker_count=1; world.markers[0]=(SmWorldMarker){0x91f8,0x920a,0xeed7,7,17,1,SM_MAP_ITEM,0,0};
  SmMapView v={0,0,128,80,8,24*16,16,0,false,false,false};
  uint32_t hidden[128*80],clean[128*80],known[128*80];
  SmMapViewDraw(hidden,128,80,&world,layout,Read,NULL,test_ram,v);
  world.marker_count=0;
  SmMapViewDraw(clean,128,80,&world,layout,Read,NULL,test_ram,v);
  assert(!memcmp(hidden,clean,sizeof(hidden)));
  world.marker_count=1;
  test_ram[0x7f7+7]=0x80;
  SmMapViewDraw(known,128,80,&world,layout,Read,NULL,test_ram,v);
  assert(memcmp(hidden,known,sizeof(hidden)));
  world.door_count=world.rooms[0].door_count=1;
  world.doors[0]=(SmWorldDoor){.source=0x91f8,.destination=0x91f8,.block_x=0,.block_y=0,.direction=4};
  world.markers[0]=(SmWorldMarker){0x91f8,0x920a,0xdb56,0,1,0,SM_MAP_CAP,SM_CAP_EYE,0};
  SmMapViewDraw(known,128,80,&world,layout,Read,NULL,test_ram,v);
  bool guard_color=false;
  for (unsigned i=0; i<sizeof(known)/sizeof(*known); ++i) if (known[i]==0xffffa14f) guard_color=true;
  assert(guard_color);
  world.door_count=world.rooms[0].door_count=0;
  /* Open the optional pause over a cached gameplay picture, close with the
   * same chord, and ensure a netplay session can never freeze gameplay. */
  Put(0x998,8); previous=0; mini_open=false;
  Fill(0xff102030); draw_map(frame+GUARD,TEST_STRIDE,TEST_WIDTH,TEST_HEIGHT,0,test_ram);
  uint16_t chord=SNES_PAD_SELECT|SNES_PAD_START;
  assert(!transform(chord,&input) && mini_open);
  CpuState cpu={0}; cpu.ram=test_ram; cpu.S=0x1ff9;
  assert(SmMapPauseHook(&cpu,0x828b47)==0x828bb7 && cpu.S==0x1ff9);
  Fill(0xff708090); draw_map(frame+GUARD,TEST_STRIDE,TEST_WIDTH,TEST_HEIGHT,0,test_ram);
  assert(frame[GUARD+150*TEST_STRIDE]==0xff102030); Guards(0xff708090);
  online=true;
  assert(!SmMapPauseHook(&cpu,0x828b47)); assert(transform(SNES_PAD_RIGHT,&input)&SNES_PAD_RIGHT);
  online=false; transform(0,&input); transform(chord,&input);
  assert(!mini_open && !SmMapPauseHook(&cpu,0x028b47));
  transform(0,&input); transform(chord,&input); assert(mini_open);
  ++test_timeline; /* save/load or rewind must close transient UI/cache */
  assert(!SmMapPauseHook(&cpu,0x828b47) && !mini_open && !backdrop_width);
  reset_map();
  Fill(0xff102030); draw_map(frame+GUARD,TEST_STRIDE,TEST_WIDTH,TEST_HEIGHT,0,test_ram);
  for (unsigned i=0; i<sizeof(frame)/sizeof(*frame); ++i) assert(frame[i]==0xff102030);
  assert(transform(chord,&input)==chord);
  /* Ammo capacity belongs on pause screens. Enabling it must leave every
   * gameplay pixel (including the native ammo counters) untouched. */
  assert(activate_ammo); activate_ammo(); assert(draw_ammo);
  memset(test_ram,0,sizeof(test_ram)); Put(0x51,15); Put(0x998,8);
  Put(0x9c6,5); Put(0x9c8,99); Put(0x9ca,2); Put(0x9cc,30); Put(0x9ce,1); Put(0x9d0,20);
  memcpy(ram_copy,test_ram,sizeof(test_ram));
  Fill(0xff102030); draw_ammo(frame+GUARD,TEST_STRIDE,TEST_WIDTH,TEST_HEIGHT,0,test_ram);
  for (unsigned i=0; i<sizeof(frame)/sizeof(*frame); ++i) assert(frame[i]==0xff102030);
  assert(!memcmp(ram_copy,test_ram,sizeof(test_ram)));
  Put(0x998,15); memcpy(ram_copy,test_ram,sizeof(test_ram));
  Fill(0xff102030); draw_ammo(frame+GUARD,TEST_STRIDE,TEST_WIDTH,TEST_HEIGHT,0,test_ram);
  Guards(0xff102030);
  assert(frame[GUARD+(TEST_HEIGHT-42)*TEST_STRIDE+8]!=0xff102030);
  memcpy(full,frame+GUARD,sizeof(full));
  assert(!memcmp(ram_copy,test_ram,sizeof(test_ram)));
  Put(0x9c8,100); /* Only maximum capacity changes; the pause readout must react. */
  Fill(0xff102030); draw_ammo(frame+GUARD,TEST_STRIDE,TEST_WIDTH,TEST_HEIGHT,0,test_ram);
  assert(memcmp(full,frame+GUARD,sizeof(full)));
  Put(0x51,7);
  Fill(0xff102030); draw_ammo(frame+GUARD,TEST_STRIDE,TEST_WIDTH,TEST_HEIGHT,0,test_ram);
  for (unsigned i=0; i<sizeof(frame)/sizeof(*frame); ++i) assert(frame[i]==0xff102030);
  puts("map input/render/freeze boundaries passed"); return 0;
}
