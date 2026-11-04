/* Re-entering Big Boy without the Baby must still initialize the native
 * shared corpse palette. Room/feature boundaries must leave WRAM untouched. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "mod_hook.h"
#include "mod_runtime.h"
#include "sm_mod_support.h"

uint32 SmTourianCorpsePaletteHook(CpuState *, uint32);
static SNESModActivationCallback activate, reset;
static uint8_t ram[0x20000], before[0x20000];
static const uint16_t palette[16] = {
  0x3800, 0x57ff, 0x42f7, 0x0929, 0x00a5, 0x7fff, 0x4231, 0x0043,
  0x4b3f, 0x42db, 0x3678, 0x2e14, 0x2190, 0x192c, 0x0cc9, 0x0465
};
static void put(unsigned a, uint16_t v) { ram[a] = v; ram[a+1] = v >> 8; }
int snes_mod_register_activation_plugin(const char *id, SNESModActivationCallback fn) {
  if (strstr(id, ".tourian-exit")) activate = fn;
  return 1;
}
int snes_mod_register_reset_callback(SNESModActivationCallback fn) { reset = fn; return 1; }
size_t SmModArmHooks(const char *name, const uint32_t *pcs, size_t n) {
  (void)name; assert(n == 5 && pcs[4] == 0xA9DC5F); return n;
}
uint16_t SmModReadRom16(uint32_t pc) {
  assert(pc >= 0xA9F8A6 && pc < 0xA9F8C6 && !(pc & 1));
  return palette[(pc - 0xA9F8A6) / 2];
}
static void unchanged(CpuState *cpu) {
  memcpy(before, ram, sizeof(ram));
  assert(!SmTourianCorpsePaletteHook(cpu, 0xA9DC5F));
  assert(!memcmp(before, ram, sizeof(ram)));
}
int main(void) {
  CpuState cpu = {0}; cpu.ram = ram;
  assert(activate && reset);
  put(0x79f, 5); put(0x79b, 0xdcb1); put(0x7bb, 0xdcdd);
  put(0x7cf, 0xe28e); ram[0xd82d] = 4;
  unchanged(&cpu); /* feature off */
  activate();
  const unsigned fields[] = {0x79f, 0x79b, 0x7bb, 0x7cf, 0xd82d};
  const uint16_t values[] = {5, 0xdcb1, 0xdcdd, 0xe28e, 4};
  for (unsigned i = 0; i < 5; ++i) {
    put(fields[i], 0); unchanged(&cpu); put(fields[i], values[i]);
  }
  memcpy(before, ram, sizeof(ram));
  for (unsigned i = 0; i < 16; ++i) {
    before[0xc3e0 + 2*i] = palette[i]; before[0xc3e1 + 2*i] = palette[i] >> 8;
  }
  assert(!SmTourianCorpsePaletteHook(&cpu, 0x29DC5F));
  assert(!memcmp(before, ram, sizeof(ram))); /* only the native target row */
  reset(); memset(ram + 0xc3e0, 0, 32); unchanged(&cpu);
  puts("Tourian re-entry corpse palette boundaries passed");
  return 0;
}
