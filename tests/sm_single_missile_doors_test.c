#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "mod_hook.h"
#include "mod_runtime.h"
#include "sm_mod_support.h"

int snes_mod_register_activation_plugin(const char *id, SNESModActivationCallback cb) {
  (void)id; (void)cb; return 1;
}
size_t SmModArmHooks(const char *name, const uint32_t *pcs, size_t count) {
  (void)name; assert(count == 1 && pcs[0] == 0x84bd50); return count;
}
#include "../src/mods/sm_single_missile_doors_plugin.c"

static uint8_t ram[0x20000], before[0x20000];
static void Word(unsigned at, unsigned value) { ram[at] = value; ram[at+1] = value >> 8; }

int main(void) {
  CpuState cpu = {0}; cpu.ram = ram;
  for (unsigned slot = 0; slot < 0x50; slot += 2) {
    cpu.X = slot;
    for (unsigned header = 0xc88a; header <= 0xc89c; header += 6) {
      memset(ram, 0, sizeof(ram)); Word(0x1c37 + slot, header);
      Word(0x1d77 + slot, 0x0100); /* a real standard missile hit */
      memcpy(before, ram, sizeof(ram)); before[0xdf0c + slot] = 4;
      assert(SmSingleMissileDoorHook(&cpu, 0x84bd50) == 0);
      assert(memcmp(before, ram, sizeof(ram)) == 0); /* no door/event flags forced */
      ram[0xdf0c + slot] = 119; /* keep native super-missile shortcut */
      SmSingleMissileDoorHook(&cpu, 0x84bd50); assert(ram[0xdf0c + slot] == 119);
    }
    const unsigned other_doors[] = {0xc842, 0xc85a, 0xc872, 0xc8a2, 0xc80e, 0xc812};
    for (unsigned i = 0; i < sizeof(other_doors) / sizeof(other_doors[0]); ++i) {
      Word(0x1c37 + slot, other_doors[i]); memcpy(before, ram, sizeof(ram));
      SmSingleMissileDoorHook(&cpu, 0x84bd50); assert(memcmp(before, ram, sizeof(ram)) == 0);
    }
    Word(0x1c37 + slot, 0xc88a); ram[0xdf0c + slot] = 0;
    const unsigned other_hits[] = {0, 0x000f, 0x0200, 0x0300, 0x0500};
    for (unsigned i = 0; i < sizeof(other_hits) / sizeof(other_hits[0]); ++i) {
      Word(0x1d77 + slot, other_hits[i]); memcpy(before, ram, sizeof(ram));
      SmSingleMissileDoorHook(&cpu, 0x84bd50); assert(memcmp(before, ram, sizeof(ram)) == 0);
    }
  }
  puts("sm_single_missile_doors_test: PASS");
  return 0;
}
