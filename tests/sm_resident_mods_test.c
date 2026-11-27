#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "mod_runtime.h"
#include "sm_mod_support.h"
#include "sm_room_runtime.h"
#include "snes/interp816.h"
static struct { const char *id;SNESModActivationCallback fn; } plugins[8];
static SNESModActivationCallback resets[8];
static unsigned plugin_count,reset_count;
static uint8_t *rom,active[0x20000];
static bool protect_fire;
int snes_mod_register_activation_plugin(const char *id,SNESModActivationCallback fn) {
  assert(plugin_count<8);plugins[plugin_count].id=id;plugins[plugin_count++].fn=fn;return 1;
}
int snes_mod_register_reset_callback(SNESModActivationCallback fn) {
  assert(reset_count<8);resets[reset_count++]=fn;return 1;
}
size_t SmModArmHooks(const char *name,const uint32_t *pcs,size_t n) { (void)name;assert(pcs && n);return n; }
static uint8_t Read(void *unused,uint32_t a) {
  (void)unused;unsigned offset=((a>>16)&127)*32768u+(a&32767);
  return (a&0x8000) && offset<0x300000 ? rom[offset] : 255;
}
uint16_t SmModReadRom16(uint32_t a) { return (uint16_t)(Read(NULL,a)|Read(NULL,a+1)<<8); }
const char *SmModOption(SmModValue *v,const char *pkg,const char *feature,const char *option,const char *fallback) {
  (void)pkg;(void)option;
  snprintf(v->text,sizeof(v->text),"%s",!strcmp(feature,"health-multipliers") ? "2" : fallback);return v->text;
}
bool SmCombatSpinHazardsActive(void) { return protect_fire; }
#include "../src/mods/sm_item_layout_plugin.c"
#include "../src/mods/sm_resident_mods.c"
static void put(unsigned a,unsigned v) { active[a]=v;active[a+1]=v>>8; }
static unsigned word(const uint8_t *r,unsigned a) { return r[a]|r[a+1]<<8; }
static void feature(const char *id) {
  for(unsigned i=0;i<plugin_count;++i)if(!strcmp(plugins[i].id,id)){plugins[i].fn();return;}
  assert(!"missing activation");
}
static SmRoomRuntime *create(SmWorld *world,unsigned id,bool actors) {
  const SmWorldRoom *room=SmWorldFindRoom(world,(uint16_t)id);assert(room);
  SmRoomRuntime *r=SmRoomRuntimeCreateConfigured(Read,NULL,room,room->default_state,0,active,actors);
  assert(r && !*SmRoomRuntimeError(r));return r;
}
static void item(SmWorld *world,unsigned room,unsigned expected) {
  SmRoomRuntime *r=create(world,room,false);const uint8_t *ram=SmRoomRuntimeRam(r);
  bool found=false;
  for(unsigned i=0;i<40;++i)if(word(ram,0x1c37+2*i)==expected)found=true;
  assert(found);SmRoomRuntimeDestroy(r);
}
int main(int argc,char **argv) {
  if(argc!=2)return 2;
  rom=malloc(0x300000);assert(rom);FILE *f=fopen(argv[1],"rb");assert(f);
  assert(fread(rom,1,0x300000,f)==0x300000);fclose(f);
  SmWorld *world=malloc(sizeof(*world));assert(world && SmWorldLoad(world,Read,NULL));
  for(unsigned i=0;i<reset_count;++i)resets[i]();
  SmItemLayoutReset();
  item(world,0xa2ce,0xef63); /* disabled default remains native */
  feature("super-metroid.balance-fixes.swap-spring-xray");
  item(world,0xa2ce,0xef57);item(world,0xd6d0,0xef63);
  feature("super-metroid.combat.health-multipliers");
  item(world,0xa2ce,0xef57); /* challenge activation cannot replace layout */
  SmRoomRuntime *r=create(world,0xb5d5,true);const uint8_t *ram=SmRoomRuntimeRam(r);
  assert(word(ram,0xf78)==0xe07f && word(ram,0xf8c)==40);SmRoomRuntimeDestroy(r);
  for(unsigned i=0;i<reset_count;++i)resets[i]();
  SmItemLayoutReset();
  active[0xd874]=0x40;put(0x9a4,2);item(world,0xd6d0,0xef63); /* swapped-save continuity while off */
  put(0xe54,0);put(0xf78,0xe07f);put(0xa6e,4);
  Interp816 c={0};c.k=0xa0;c.pc=0xa07a;ResidentMods(&c,active);assert(c.pc==0xa07a);
  protect_fire=true;ResidentMods(&c,active);assert(c.pc==0xa0b7);
  c.pc=0xa07a;put(0xa6e,0);ResidentMods(&c,active);assert(c.pc==0xa07a);
  free(world);free(rom);
  puts("PASS: actual resident item PLMs, challenge coexistence, off-save continuity and active-only plume policy");
}
