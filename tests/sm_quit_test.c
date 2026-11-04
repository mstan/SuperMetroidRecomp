#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "mod_hook.h"
#include "mod_runtime.h"
#include "sm_mod_support.h"
#include "sm_return_to_title_plugin.h"
#include "snes_overlay_draw.h"

uint32 SmQuitFileSelectReadyHook(CpuState *,uint32);
uint32 SmReturnToTitleHook(CpuState *,uint32);
static SNESModActivationCallback activate_quit,activate_ending,reset_quit;
static SNESModInputTransform input;
static bool options_open;
static unsigned armed;
int snes_mod_register_activation_plugin(const char *id,SNESModActivationCallback fn) {
  if (strstr(id,".quit-to-file-select")) activate_quit=fn;
  if (strstr(id,".return-to-title")) activate_ending=fn;
  return 1;
}
int snes_mod_register_reset_callback(SNESModActivationCallback fn) { reset_quit=fn; return 1; }
int snes_mod_register_input_transform(SNESModInputTransform fn) { input=fn; return 1; }
size_t SmModArmHooks(const char *name,const uint32_t *pcs,size_t count) {
  (void)name; assert(pcs && count); armed+=(unsigned)count; return count;
}
void SmPauseOptions_SetActive(bool active) { options_open=active; }
static uint8_t ram[0x20000],copy[0x20000];
static void Put(unsigned a,uint16_t v) { ram[a]=(uint8_t)v; ram[a+1]=(uint8_t)(v>>8); }
static uint16_t Get(unsigned a) { return (uint16_t)(ram[a]|ram[a+1]<<8); }
int main(void) {
  assert(activate_quit && activate_ending && reset_quit);
  assert(!SmQuitFileSelectEnabled() && !SmQuitFileSelectRequest());
  activate_quit(); assert(SmQuitFileSelectEnabled() && armed==1 && input);
  assert(SmQuitFileSelectRequest() && SmQuitFileSelectState()==1);
  CpuState cpu={0}; cpu.ram=ram; cpu.S=0x1ff9; cpu.DB=0x82;
  Put(0x998,8); assert(!SmQuitPauseStep(&cpu)); /* only from pause */
  Put(0x998,15); Put(0x51,15); options_open=true;
  assert(SmQuitPauseStep(&cpu) && !options_open);
  assert(SmQuitFileSelectState()==2 && Get(0x998)==15); /* fade before Game Over */
  uint32_t saved=SmQuitFileSelectState();
  reset_quit(); activate_quit(); SmQuitFileSelectRestore(saved);
  assert(SmQuitFileSelectStateValid(saved) && !SmQuitFileSelectStateValid(4));
  SmQuitFileSelectRestore(4); assert(SmQuitFileSelectState()==saved);
  unsigned frames=0;
  while (SmQuitFileSelectState()==2 && frames++<40) SmQuitPauseStep(&cpu);
  assert(frames<=31 && SmQuitFileSelectState()==3 && Get(0x998)==0x1a && Get(0x727)==7 && ram[0x51]==0x80);
  SnesModInputContext context={.player=0,.wram=ram};
  Put(0x998,1); Put(0x5b6,16); memcpy(copy,ram,sizeof(ram));
  assert(input(SNES_PAD_LEFT,&context)==(SNES_PAD_LEFT|SNES_PAD_START));
  Put(0x5b6,17); assert(input(SNES_PAD_LEFT,&context)==SNES_PAD_LEFT);
  Put(0x5b6,16); context.player=1; assert(input(SNES_PAD_LEFT,&context)==SNES_PAD_LEFT); context.player=0;
  assert(!memcmp(copy,ram,sizeof(ram)));
  Put(0x998,4); assert(!input(0,&context)); SmQuitFileSelectReadyHook(&cpu,0x8289e5);
  assert(!SmQuitFileSelectState());
  Put(0x998,1); assert(!input(0,&context));
  /* Ending return is still independent and waits for the complete final pose. */
  activate_ending(); Put(0x998,0x27); Put(0x1f51,0xe7ba); Put(0x1997,0xffb1); Put(0x8f,0x1000);
  SmReturnToTitleHook(&cpu,0x8bd44f); assert(Get(0x1f51)==0xe7ba);
  Put(0x1997,0xffb0); SmReturnToTitleHook(&cpu,0x8bd44f); assert(Get(0x1f51)==0x9a47);
  assert(!SmQuitFileSelectState());
  assert(cpu.S==0x1ff9 && cpu.DB==0x82 && !cpu.m_flag && !cpu.x_flag);
  reset_quit(); assert(!SmQuitFileSelectEnabled() && !SmQuitFileSelectRequest());
  puts("quit flow/fade/navigation/state boundaries passed"); return 0;
}
