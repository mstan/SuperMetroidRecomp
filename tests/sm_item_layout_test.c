#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "mod_runtime.h"
#include "sm_mod_support.h"
int snes_mod_register_activation_plugin(const char *id,SNESModActivationCallback callback) {
  (void)id;(void)callback;return 1;
}
size_t SmModArmHooks(const char *name,const uint32_t *pcs,size_t count) {
  (void)name;assert(count==1 && pcs[0]==0x8484ad);return count;
}
#include "sm_item_layout_plugin.c"
static uint8_t ram[0x20000], before[0x20000];
static CpuState cpu;
static void put(unsigned at,unsigned value) { ram[at]=value;ram[at+1]=value>>8; }
static unsigned header(unsigned original,unsigned argument) {
  cpu.A=original;put(0x1dc7+cpu.Y,argument);memcpy(before,ram,sizeof(ram));
  assert(!SmItemLayoutHeaderHook(&cpu,0x8484ad));
  assert(!memcmp(before,ram,sizeof(ram)));return cpu.A;
}
int main(void) {
  cpu.ram=ram;cpu.Y=4;SmItemLayoutReset();
  assert(header(0xef57,0x96)==0xef57 && header(0xef63,0x26)==0xef63);
  activate();
  assert(header(0xef57,0x96)==0xef63 && header(0xef63,0x26)==0xef57);
  assert(header(0xef03,0x96)==0xef0f && header(0xef0f,0x26)==0xef03);
  assert(header(0xefab,0x96)==0xefb7 && header(0xefb7,0x26)==0xefab);
  assert(header(0xef57,0x11)==0xef57 && header(0xeeee,0x96)==0xeeee);
  /* An existing stock partial file retains the still-missing ability. */
  ram[0xd882]=0x40;put(0x9a4,2);
  assert(header(0xef63,0x26)==0xef63);
  ram[0xd882]=0;ram[0xd874]=0x40;put(0x9a4,0x8000);
  assert(header(0xef57,0x96)==0xef57);
  /* A legitimately swapped partial save remains completable with the mod
   * disabled, with no private host marker, SRAM rewrite or new save format. */
  SmItemLayoutReset();ram[0xd882]=0x40;ram[0xd874]=0;put(0x9a4,0x8000);
  assert(header(0xef63,0x26)==0xef57);
  ram[0xd882]=0;ram[0xd874]=0x40;put(0x9a4,2);
  assert(header(0xef57,0x96)==0xef63);
  ram[0xd882]=0x40;put(0x9a4,0x8002);
  assert(header(0xef57,0x96)==0xef57);
  cpu.m_flag=1;assert(header(0xef57,0x96)==0xef57);
  puts("native item headers, location guards, stock partial saves and disabled swap continuity pass");
}
