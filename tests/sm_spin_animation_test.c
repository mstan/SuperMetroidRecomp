#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "mod_runtime.h"
#include "sm_mod_support.h"
int snes_mod_register_activation_plugin(const char *id,SNESModActivationCallback callback) { (void)id;(void)callback;return 1; }
size_t SmModArmHooks(const char *name,const uint32_t *pcs,size_t count) { (void)name;assert(count==2 && pcs[0]==0x91f6bf && pcs[1]==0x91f720);return count; }
#include "../src/mods/sm_spin_animation_plugin.c"
int main(void) {
  static uint8_t ram[0x20000],before[0x20000];CpuState cpu={0};cpu.ram=ram;
  ram[0xa1c]=0x19;cpu._flag_Z=1;cpu.P=2;memcpy(before,ram,sizeof(ram));
  activate();assert(SmSpinAnimationHook(&cpu,0x91f6bf)==0x91f6d4);
  assert(cpu._flag_Z && (cpu.P&2) && !memcmp(before,ram,sizeof(ram)));
  ram[0xa1c]=0x1a;assert(SmSpinAnimationHook(&cpu,0x11f720)==0x11f735);
  ram[0xa1c]=0x81;assert(!SmSpinAnimationHook(&cpu,0x91f6bf));
  ram[0xa1c]=0x19;cpu.m_flag=1;assert(!SmSpinAnimationHook(&cpu,0x91f6bf));
  puts("Spin animation: native pose branch, mirror, screw isolation and untouched equipment/physics RAM pass.");
}
