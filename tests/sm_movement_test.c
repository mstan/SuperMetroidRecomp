#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "mod_hook.h"
#include "sm_movement_plugins.h"
#include "sm_mod_support.h"
#include "sm_mod_overlay.h"
#include "snes_overlay_draw.h"

void SmDreadAimConfigure(bool active,unsigned shoulder) { (void)active;(void)shoulder; }
uint32 SmDreadAimTransition(CpuState *cpu,uint32 pc) { (void)cpu;(void)pc;return 0; }
uint32 SmMoveTransitionHook(CpuState *,uint32);
uint32 SmMoveSparkCancelHook(CpuState *,uint32);
uint32 SmMoveBallAnimationHook(CpuState *,uint32);
static struct { const char *id; SNESModActivationCallback fn; } plugins[16];
static size_t count;
static SNESModActivationCallback reset_move;
static SNESModInputTransform transforms[4];
static unsigned transform_count;
static bool zm,air,morph,cancel;
int snes_mod_register_activation_plugin(const char *id,SNESModActivationCallback fn) {
  assert(count<16); plugins[count].id=id; plugins[count++].fn=fn; return 1;
}
int snes_mod_register_reset_callback(SNESModActivationCallback fn) { reset_move=fn; return 1; }
int snes_mod_register_input_transform(SNESModInputTransform fn) { assert(transform_count<4); transforms[transform_count++]=fn; return 1; }
int snes_mod_runtime_feature_enabled_c(const char *pkg,const char *feature) {
  return zm && !strcmp(pkg,"super-metroid.controls") && !strcmp(feature,"zero-mission");
}
bool SmModOptionBool(const char *pkg,const char *feature,const char *option,bool fallback) {
  (void)pkg; (void)feature;
  if (!strcmp(option,"air-morph")) return air;
  if (!strcmp(option,"down-to-morph")) return morph;
  if (!strcmp(option,"jump-to-cancel")) return cancel;
  return fallback;
}
const char *SmModOption(SmModValue *out,const char *pkg,const char *feature,const char *option,const char *fallback) {
  (void)pkg;(void)feature;(void)option; snprintf(out->text,sizeof(out->text),"%s",fallback); return out->text;
}
size_t SmModArmHooks(const char *name,const uint32_t *pcs,size_t n) { (void)name; assert(pcs && n); return n; }
bool SmModOverlayRegister(SmModOverlayDraw draw) { (void)draw; return true; }
static void Activate(const char *id) {
  for (size_t i=0;i<count;i++) if (!strcmp(plugins[i].id,id)) { plugins[i].fn(); return; }
  assert(!"missing production activation callback");
}
static uint8_t ram[0x20000],copy[0x20000];
static void Put(unsigned a,uint16_t v) { ram[a]=(uint8_t)v; ram[a+1]=(uint8_t)(v>>8); }
static uint16_t Get(unsigned a) { return (uint16_t)(ram[a]|ram[a+1]<<8); }
int main(void) {
  CpuState cpu={0}; cpu.ram=ram; cpu.S=0x1ff9; cpu.DB=0x91;
  Put(0x998,8); Put(0x9a2,4); Put(0x9ac,0x400); Put(0x9b4,0x80);
  Put(0x9b8,0x10); /* item cancel rebound to R: shoulder remap must not eat it */
  ram[0xa1f]=2; ram[0xa1e]=8;
  SnesModInputContext ctx={.player=0,.wram=ram}; uint16_t raw=SNES_PAD_L|SNES_PAD_DOWN;
  memcpy(copy,ram,sizeof(ram)); uint16_t pad=raw;
  assert(!SmQuickMorphAirInput(&pad,&ctx) && pad==raw);
  air=true; zm=true;
  Activate("super-metroid.controls.zero-mission");
  Activate("super-metroid.movement.quick-morph");
  assert(transform_count==1); /* no second interpretation of the canonical pad */
  pad=transforms[0](raw,&ctx); assert(pad==(SNES_PAD_R|SNES_PAD_DOWN));
  assert(!memcmp(copy,ram,sizeof(ram))); /* input stage cannot write the guest */
  Put(0x8b,0x410); Put(0x8f,0x400);
  assert(SmMoveTransitionHook(&cpu,0x9181a9)==0x9181e2 && Get(0xa28)==0x37);
  ctx.player=1; pad=raw; assert(!SmQuickMorphAirInput(&pad,&ctx)); ctx.player=0;
  Put(0x9a2,0); pad=raw; assert(!SmQuickMorphAirInput(&pad,&ctx)); Put(0x9a2,4);
  Put(0x998,15); pad=raw; assert(!SmQuickMorphAirInput(&pad,&ctx)); Put(0x998,8);
  ram[0xa1f]=5; pad=raw; assert(!SmQuickMorphAirInput(&pad,&ctx)); ram[0xa1f]=6;
  reset_move(); transform_count=0; zm=false; Activate("super-metroid.movement.quick-morph");
  assert(transform_count==1 && transforms[0](raw,&ctx)==(SNES_PAD_R|SNES_PAD_DOWN));
  reset_move(); air=false; morph=cancel=true; Activate("super-metroid.movement.shinespark-steering");
  const uint16_t entries[]={0xd0ab,0xd0d7,0xd106},ends[]={0xd0d6,0xd105,0xd131};
  for (unsigned i=0;i<3;i++) for (unsigned left=0;left<2;left++) {
    Put(0xa1c,(uint16_t)(0xc9+2*i+left)); ram[0xa1e]=left?4:8;
    Put(0x8f,0); memcpy(copy,ram,sizeof(ram));
    assert(!SmMoveSparkCancelHook(&cpu,0x900000|entries[i]) && !memcmp(copy,ram,sizeof(ram)));
    Put(0x8f,0x400); Put(0xb3e,0x400); Put(0xa6e,2); Put(0x648,15); Put(0x645,14);
    assert(SmMoveSparkCancelHook(&cpu,0x900000|entries[i])==(0x900000u|ends[i]));
    assert(Get(0xa2c)==0x31+left && Get(0xa32)==2 && Get(0xa5c)==0xeb52);
    assert(!Get(0xb3e) && !Get(0xa6e) && !ram[0x648] && ram[0x685]==0x10 && !ram[0x676]);
    Put(0x8f,0x80); assert(SmMoveSparkCancelHook(&cpu,0x900000|entries[i]));
    assert(Get(0xa2c)==0x29+left);
  }
  Put(0xa1c,0xc9); Put(0x9a2,0); Put(0x8f,0x400);
  memcpy(copy,ram,sizeof(ram)); assert(!SmMoveSparkCancelHook(&cpu,0x90d106) && !memcmp(copy,ram,sizeof(ram)));
  Put(0xa1c,0x19); Put(0x8f,0x80); assert(!SmMoveSparkCancelHook(&cpu,0x90d106));
  reset_move(); Put(0xa1c,0xc9); assert(!SmMoveSparkCancelHook(&cpu,0x90d106));
  assert(cpu.S==0x1ff9 && cpu.DB==0x91 && !cpu.m_flag && !cpu.x_flag);
  reset_move();Activate("super-metroid.movement.motion-ball-animation");
  memset(ram,0,sizeof(ram));ram[0xa1f]=4;
  Put(0xa94,1);Put(0xa96,7);memcpy(copy,ram,sizeof(ram));
  assert(SmMoveBallAnimationHook(&cpu,0x908011)==0x908065);
  assert(!memcmp(copy,ram,sizeof(ram)) && cpu.S==0x1ff9);
  Put(0xaf8,1);assert(!SmMoveBallAnimationHook(&cpu,0x908011)); /* subpixel motion */
  Put(0xaf8,0);Put(0xafa,1);assert(!SmMoveBallAnimationHook(&cpu,0x908011)); /* falling */
  Put(0xafa,0);ram[0xa1f]=15;assert(!SmMoveBallAnimationHook(&cpu,0x908011)); /* morph transition */
  reset_move();ram[0xa1f]=4;assert(!SmMoveBallAnimationHook(&cpu,0x908011));
  reset_move();Activate("super-metroid.controls.falling-aim");
  memset(ram,0,sizeof(ram));Put(0x9ac,0x400);Put(0x9ae,0x200);Put(0x9b0,0x100);Put(0x9b2,0x40);
  for(unsigned left=0;left<2;++left) {
    Put(0xa1c,left?0x2e:0x2d);ram[0xa1f]=6;ram[0xa1e]=left?4:8;
    Put(0x8b,(uint16_t)(0x440|(left?0x100:0x200)));
    Put(0xb2c,0x1234);Put(0xb2e,4);Put(0xb46,2);Put(0xb48,0x5678);
    assert(SmMoveTransitionHook(&cpu,0x9181a9)==0x9181e2 && Get(0xa28)==(left?0x2d:0x2e));
    assert(Get(0xb2c)==0x1234 && Get(0xb2e)==4 && Get(0xb46)==2 && Get(0xb48)==0x5678);
    Put(0x8b,0x740);assert(!SmMoveTransitionHook(&cpu,0x9181a9)); /* opposing inputs */
    Put(0x8b,0x640);ram[0xa1f]=0;assert(!SmMoveTransitionHook(&cpu,0x9181a9));
  }
  reset_move();ram[0xa1f]=6;Put(0xa1c,0x2d);ram[0xa1e]=8;Put(0x8b,0x640);
  assert(!SmMoveTransitionHook(&cpu,0x9181a9));
  puts("air gesture/remap, spark cancellation and down-aim turning boundaries passed"); return 0;
}
