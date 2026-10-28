/* Boundary regressions for the original-game defects. Game scenarios verify
 * the stock call sites and both execution paths separately. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "mod_hook.h"
#include "mod_runtime.h"
#include "sm_mod_support.h"

#define HOOK(name) uint32 name(CpuState *, uint32)
HOOK(SmFixScrewMenuHook); HOOK(SmFixXrayRespawningPlmHook);
HOOK(SmFixBombBlockScrewHook); HOOK(SmFixEyeDoorProjectileHook);
HOOK(SmFixSpeedBoosterSfxHook); HOOK(SmFixSpeedBoosterJumpHook);
HOOK(SmFixWallJumpScrewHook); HOOK(SmFixBlueSuitPaletteHook);
HOOK(SmFixScrewIframesHook); HOOK(SmFixWsGhostHook);
HOOK(SmFixSpriteObjectClearHook); HOOK(SmFixSuitPickupHook);
HOOK(SmFixReservePauseHook); HOOK(SmFixReserveFullHook);
HOOK(SmFixReserveAutoEntryHook); HOOK(SmFixReserveAutoKeepHook);
HOOK(SmFixReserveManualCancelHook); HOOK(SmFixReserveManualKeepHook);
HOOK(SmFixReserveManualDeselectHook);

static SNESModActivationCallback bugs, reserves, reset;
static bool options = true;
static unsigned arms;
int snes_mod_register_activation_plugin(const char *id, SNESModActivationCallback fn) {
  if (strstr(id, ".bug-fixes")) bugs = fn;
  if (strstr(id, ".reserve-tanks")) reserves = fn;
  return 1;
}
int snes_mod_register_reset_callback(SNESModActivationCallback fn) { reset = fn; return 1; }
bool SmModOptionBool(const char *pkg, const char *feature, const char *option, bool fallback) {
  (void)pkg; (void)feature; (void)option; (void)fallback; return options;
}
size_t SmModArmHooks(const char *name, const uint32_t *pcs, size_t n) {
  (void)name; assert(n && pcs); arms += (unsigned)n; return n;
}
uint16_t SmModReadRom16(uint32_t pc) { assert(pc == 0x82BF04); return 1; }
static uint8_t ram[0x20000];
static void Put(unsigned a, uint16_t v) { ram[a] = (uint8_t)v; ram[a+1] = (uint8_t)(v >> 8); }
static uint16_t Get(unsigned a) { return (uint16_t)(ram[a] | ram[a+1] << 8); }

int main(void) {
  assert(bugs && reserves && reset);
  options = false; bugs(); reserves(); assert(!arms);
  options = true; bugs(); reserves(); assert(arms == 24);
  CpuState c = {0}; c.ram = ram; c.S = 0x1ff9; c.DB = 0x7e;
  c.X = 10; assert(SmFixScrewMenuHook(&c, 0x82B4C4) == 0x82B4BA);
  c.X = 12; assert(!SmFixScrewMenuHook(&c, 0x02B4C4));
  c.A = 0x8000; assert(SmFixXrayRespawningPlmHook(&c, 0x848331) == 0x848342);
  c.A = 1; assert(!SmFixXrayRespawningPlmHook(&c, 0x848331));
  for (unsigned i = 0; i < 5; ++i) {
    Put(0xA6E, (uint16_t)i);
    assert(SmFixBombBlockScrewHook(&c, 0x84CE83) == (i >= 1 && i <= 3 ? 0x84CEC1 : 0x84CEB9));
  }
  c.A = 1; c.X = 3; c.Y = 0x20; SmFixEyeDoorProjectileHook(&c, 0x86B704); assert(c.X == 0x20);
  c.A = 0; c.X = 3; SmFixEyeDoorProjectileHook(&c, 0x86B704); assert(c.X == 3);
  Put(0xB3E, 0x401); c.A = 0xffff; SmFixSpeedBoosterSfxHook(&c, 0x9085AA); assert(c.A == 0x401);
  /* Odd whole run speed contributes a half pixel; fraction carry survives. */
  Put(0xB42, 3); Put(0xB44, 0x8000); Put(0xB2E, 4); Put(0xB2C, 0x8000);
  assert(SmFixSpeedBoosterJumpHook(&c, 0x909927) == 0x90993A);
  assert(Get(0xB2E) == 6 && Get(0xB2C) == 0x4000);
  Put(0xB42, 0); Put(0xB44, 0xffff); Put(0xB2E, 4); Put(0xB2C, 0xffff);
  SmFixSpeedBoosterJumpHook(&c, 0x109927); assert(Get(0xB2E) == 5 && Get(0xB2C) == 0x7ffe);
  Put(0x9A2, 8); Put(0xA6E, 0); SmFixWallJumpScrewHook(&c, 0x90A737); assert(Get(0xA6E) == 3);
  Put(0x9A2, 0); Put(0xCD0, 60); SmFixWallJumpScrewHook(&c, 0x90A737); assert(Get(0xA6E) == 4);
  Put(0xCD0, 59); Put(0xA6E, 0); SmFixWallJumpScrewHook(&c, 0x90A737); assert(!Get(0xA6E));
  Put(0xACE, 10); SmFixBlueSuitPaletteHook(&c, 0x91DA89); assert(Get(0xACE) == 6);
  Put(0xACE, 4); SmFixBlueSuitPaletteHook(&c, 0x91DA89); assert(Get(0xACE) == 4);
  Put(0x18A8, 99); assert(SmFixScrewIframesHook(&c, 0xA0A096) == 0xA0A099); assert(Get(0x18A8) == 99);
  const uint32 ghosts[] = {0xA89C13, 0xA89D45, 0xA89DC3, 0xA89DCD};
  Put(0xAF6, 0x1234); Put(0xAFA, 0x8001); c.X = 0x40;
  Put(0xB36, 0xaaaa); Put(0xB3A, 0xbbbb);
  for (unsigned i = 0; i < 4; ++i) {
    c._flag_V = 1; SmFixWsGhostHook(&c, ghosts[i]);
    assert(c.A == (i == 2 ? 0x1234 : 0x8001));
    assert(c._flag_N == (i != 2) && !c._flag_Z && c._flag_V);
  }
  Put(0xEF78, 0x1234); c.X = 2; SmFixSpriteObjectClearHook(&c, 0xB4BDA3); assert(Get(0xEF78) == 0x1234);
  c.X = 0; SmFixSpriteObjectClearHook(&c, 0xB4BDA3); assert(!Get(0xEF78));
  const uint16_t suits[] = {0xE2CA, 0xE2FF, 0xE727, 0xE769, 0xEC31, 0xEC79};
  for (unsigned i = 0; i < 6; ++i) {
    c.Y = suits[i]; c.A = 0x2008;
    assert(SmFixSuitPickupHook(&c, 0x8488F6) == 0x8488F9 && c.A == 0x2008);
    assert(SmFixSuitPickupHook(&c, 0x8488FF) == 0x848902);
  }
  c.Y = 0xE000; assert(!SmFixSuitPickupHook(&c, 0x8488F6));
  Put(0x998, 15); assert(SmFixReservePauseHook(&c, 0x82DB73) == 0x82DB9D);
  Put(0x998, 8); assert(!SmFixReservePauseHook(&c, 0x82DB73));
  Put(0x9D4, 300); SmFixReserveFullHook(&c, 0x848990); assert(Get(0x9D6) == 300);
  Put(0xA4E, 0xffff); Put(0x18A8, 60); SmFixReserveAutoEntryHook(&c, 0x82DC31);
  assert(!Get(0xA4E) && Get(0x18A8) == 61);
  Put(0x9C4, 99); Put(0x9C2, 98); Put(0x9D6, 202);
  assert(SmFixReserveAutoKeepHook(&c, 0x82DC45) == 0x82DC77);
  assert(Get(0x9C2) == 99 && Get(0x9D6) == 201);
  assert(SmFixReserveAutoKeepHook(&c, 0x82DC45) == 0x82DC7C);
  assert(Get(0x9C2) == 99 && Get(0x9D6) == 201);
  Put(0x9C2, 50); Put(0x9D6, 1); SmFixReserveAutoKeepHook(&c, 0x82DC45);
  assert(Get(0x9C2) == 51 && !Get(0x9D6));
  Put(0x9C2, 98); Put(0x9D6, 202);
  assert(SmFixReserveManualKeepHook(&c, 0x82AF81) == 0x82AFBC);
  assert(SmFixReserveManualKeepHook(&c, 0x82AF81) == 0x82AFB3);
  assert(Get(0x9C2) == 99 && Get(0x9D6) == 201);
  Put(0x755, 0x100); Put(0x757, 30); Put(0x8F, 0x80);
  assert(SmFixReserveManualCancelHook(&c, 0x82AF52) == 0x82AFBC);
  assert(!Get(0x755) && !Get(0x757));
  Put(0x755, 0x100); Put(0x757, 30); SmFixReserveManualDeselectHook(&c, 0x82AC52); assert(Get(0x757) == 30);
  Put(0x755, 0x200); SmFixReserveManualDeselectHook(&c, 0x82AC52); assert(!Get(0x757));
  reset(); Put(0xA4E, 0x1234); Put(0x18A8, 60); SmFixReserveAutoEntryHook(&c, 0x82DC31);
  assert(Get(0xA4E) == 0x1234 && Get(0x18A8) == 60);
  assert(!SmFixReserveAutoKeepHook(&c, 0x82DC45));
  assert(!SmFixReserveManualKeepHook(&c, 0x82AF81));
  assert(c.S == 0x1ff9 && c.DB == 0x7e && !c.m_flag && !c.x_flag);
  puts("vanilla/reserve bug boundaries passed");
  return 0;
}
