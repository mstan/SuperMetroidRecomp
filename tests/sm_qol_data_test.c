/* Regression boundaries for the production data-consumer callbacks. The
 * stock-routine/gameplay checks live in tools/test_qol_rom_removal.py. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "mod_hook.h"
#include "mod_runtime.h"
#include "sm_mod_support.h"

uint32 SmShakeRoomOffsets(CpuState *, uint32);
uint32 SmShakeProjectileOffsets(CpuState *, uint32);
uint32 SmModAmmoStationMessage(CpuState *, uint32);

static SNESModActivationCallback shake_activate;
static const char *level = "half";
static unsigned threshold_sets;
int snes_mod_register_activation_plugin(const char *id, SNESModActivationCallback fn) {
  if (!strcmp(id, "super-metroid.pacing.screen-shake")) shake_activate = fn;
  return 1;
}
int snes_mod_register_reset_callback(SNESModActivationCallback fn) { (void)fn; return 1; }
const char *SmModOption(SmModValue *out, const char *pkg, const char *feature,
                        const char *option, const char *fallback) {
  (void)pkg; (void)feature; (void)option; (void)fallback;
  snprintf(out->text, sizeof(out->text), "%s", level);
  return out->text;
}
size_t SmModArmHooks(const char *name, const uint32_t *pcs, size_t count) {
  (void)name; (void)pcs; return count;
}
bool snes_mod_hook_arm(uint32 pc, bool arm) { (void)pc; (void)arm; return true; }
bool snes_mod_operand_set(uint32 pc, uint16 value) {
  assert(pc == 0xA08705 && value == 0x7FFF); threshold_sets++; return true;
}

static uint8_t ram[0x20000];
static void Put(unsigned addr, uint16_t value) {
  ram[addr] = (uint8_t)value; ram[addr + 1] = (uint8_t)(value >> 8);
}
static uint16_t Get(unsigned addr) { return (uint16_t)(ram[addr] | ram[addr + 1] << 8); }

int main(void) {
  assert(shake_activate);
  shake_activate();
  CpuState cpu = {0}; cpu.ram = ram; cpu.S = 0x1FF; cpu.X = 17 * 8;
  /* A positive overflow on the last BG scroll ADC must survive the stock
   * continuation's DEC/LDA/CMP, which do not change V. */
  Put(0xB7, 0x7FFF); Put(0x1840, 4);
  assert(SmShakeRoomOffsets(&cpu, 0xA086A5) == 0xA086FF);
  assert(Get(0xB7) == 0x8001 && cpu._flag_V);
  Put(0xB7, 0x0400); cpu._flag_V = 1;
  SmShakeRoomOffsets(&cpu, 0x2086A5);
  assert(Get(0xB7) == 0x0402 && !cpu._flag_V);
  Put(0xB7, 0x8000); Put(0x1840, 2);
  SmShakeRoomOffsets(&cpu, 0xA086A5);
  assert(Get(0xB7) == 0x7FFE && cpu._flag_V);
  assert(cpu.S == 0x1FF && cpu.X == 17 * 8 && !cpu.m_flag && !cpu.x_flag);
  /* Off reproduces an ADC of zero (V cleared). Projectile LDA/EOR/INC
   * preserves the incoming V, while returning zero offsets and N/Z. */
  level = "off"; shake_activate(); assert(threshold_sets == 1);
  Put(0xB7, 0x8000); cpu._flag_V = 1;
  SmShakeRoomOffsets(&cpu, 0xA086A5);
  assert(Get(0xB7) == 0x8000 && !cpu._flag_V);
  cpu.X = 35 * 4; cpu._flag_V = 1;
  assert(SmShakeProjectileOffsets(&cpu, 0x86843C) == 0x868465);
  assert(!Get(0x22) && !Get(0x24) && !cpu.A && cpu._flag_Z && !cpu._flag_N && cpu._flag_V);
  /* The last old letter is at $85:932B. Omitting it leaves a trailing D.
   * The adjacent tile and every other message must remain untouched. */
  Put(0x1C1F, 0x16); Put(0, 0x92FF); cpu.Y = 0x2C;
  cpu.A = 0x3CE3;
  assert(!SmModAmmoStationMessage(&cpu, 0x858311));
  assert(cpu.A == 0x3C4E && !cpu._flag_N && !cpu._flag_Z);
  cpu.Y = 0x2E; cpu.A = 0x1234;
  SmModAmmoStationMessage(&cpu, 0x858311); assert(cpu.A == 0x1234);
  Put(0x1C1F, 0x15); cpu.Y = 0x10; cpu.A = 0x3CE3;
  SmModAmmoStationMessage(&cpu, 0x858311); assert(cpu.A == 0x3CE3);
  puts("QoL data-consumer boundaries passed");
  return 0;
}
