/* super-metroid.balance-fixes: station refills.
 *
 * missile-station: a missile station reloads missiles, super missiles and
 * power bombs, and works while any of them is short (Missile Station
 * Enhancer by MoogleEmperor, as shipped in Super Metroid Redux). Its message
 * reads "AMMO RELOAD".
 *   - PlmInstr_ActivateMissileStation ($84:8CD0) compares missiles with max
 *     at $84:8CD2 and skips the message and refill when equal. The hook
 *     refills supers and power bombs and, when anything was short, continues
 *     at the message call ($84:8CDA), after which stock refills missiles.
 *   - The access checks PlmSetup_B6EF/B6F3 (BEQ at $84:B2EE, $84:B321) and
 *     PlmInstr_GotoIfMissilesFull (BEQ at $84:AEC5) treat "missiles full" as
 *     "nothing to do"; while any ammo is short the hooks continue past the
 *     branch.
 *
 * save-station: saving also refills energy, reserve tanks and all ammo, like
 * the gunship (Save Refills Everything by Adam, as shipped in Redux). Redux
 * fills over several frames before the prompt; here the hook on
 * PlmInstr_ActivateSaveStationAndGotoIfNo ($84:8CF1), which runs while the
 * save prompt is up, fills at once.
 *
 * The hooks are declared in recomp/bank04.cfg.
 */

#include <stdbool.h>
#include <stdio.h>

#include "mod_hook.h"
#include "mod_runtime.h"
#include "sm_mod_support.h"

enum {
  kEnergy = 0x09C2, kMaxEnergy = 0x09C4,
  kMissiles = 0x09C6, kMaxMissiles = 0x09C8,
  kSupers = 0x09CA, kMaxSupers = 0x09CC,
  kPowerBombs = 0x09CE, kMaxPowerBombs = 0x09D0,
  kMaxReserve = 0x09D4, kReserve = 0x09D6,
};

static uint16_t Word(const CpuState *cpu, uint16_t addr) {
  return (uint16_t)(cpu->ram[addr] | cpu->ram[addr + 1] << 8);
}
static void SetWord(CpuState *cpu, uint16_t addr, uint16_t value) {
  cpu->ram[addr] = (uint8_t)value;
  cpu->ram[addr + 1] = (uint8_t)(value >> 8);
}
/* Refill one supply; returns whether it was short. */
static bool Fill(CpuState *cpu, uint16_t current, uint16_t max) {
  uint16_t want = Word(cpu, max);
  if (Word(cpu, current) == want) return false;
  SetWord(cpu, current, want);
  return true;
}
static bool AmmoShort(const CpuState *cpu) {
  return Word(cpu, kMissiles) != Word(cpu, kMaxMissiles) ||
         Word(cpu, kSupers) != Word(cpu, kMaxSupers) ||
         Word(cpu, kPowerBombs) != Word(cpu, kMaxPowerBombs);
}

/* ── missile stations ────────────────────────────────────────────────── */

uint32 SmModMissileStationRefill(CpuState *cpu, uint32 pc24) {
  bool short_ammo = Word(cpu, kMissiles) != Word(cpu, kMaxMissiles);
  short_ammo |= Fill(cpu, kSupers, kMaxSupers);
  short_ammo |= Fill(cpu, kPowerBombs, kMaxPowerBombs);
  /* Stock refills missiles right after the message call. */
  return short_ammo ? (pc24 & 0xFF0000u) | 0x8CDAu : 0;
}

static uint32 ContinuePast(CpuState *cpu, uint32 pc24, uint16_t next) {
  return AmmoShort(cpu) ? (pc24 & 0xFF0000u) | next : 0;
}
uint32 SmModMissileStationUsableB2EE(CpuState *cpu, uint32 pc24) {
  return ContinuePast(cpu, pc24, 0xB2F0);
}
uint32 SmModMissileStationUsableB321(CpuState *cpu, uint32 pc24) {
  return ContinuePast(cpu, pc24, 0xB323);
}
uint32 SmModMissileStationUsableAEC5(CpuState *cpu, uint32 pc24) {
  return ContinuePast(cpu, pc24, 0xAEC7);
}

static const uint32_t kMissileStationHooks[] = {
  0x848CD2, 0x84B2EE, 0x84B321, 0x84AEC5, 0x858311
};

/* Message $16's tilemap row, including the surrounding spaces. */
uint32 SmModAmmoStationMessage(CpuState *cpu, uint32 pc24) {
  (void)pc24;
  /* Replace letters as WriteMessageTilemap copies them into WRAM, before
   * the normal DMA. The source row starts on an odd cartridge address. */
  static const char text[] = "   AMMO RELOAD   ";
  uint16_t source = (uint16_t)(Word(cpu, cpu->D) + cpu->Y);
  if (Word(cpu, 0x1C1F) == 0x16 && source >= 0x930B && source < 0x932D &&
      !((source - 0x930B) & 1)) {
    unsigned char c = (unsigned char)text[(source - 0x930B) / 2];
    cpu->A = (uint16_t)(0x3C00 | (c == ' ' ? 0x4E : 0xE0 + c - 'A'));
    cpu->_flag_N = 0;
    cpu->_flag_Z = 0;
  }
  return 0;
}

static void SmMissileStationActivate(void) {
  size_t armed = 0, count = sizeof(kMissileStationHooks) / sizeof(kMissileStationHooks[0]);
  for (size_t i = 0; i < count; ++i)
    armed += snes_mod_hook_arm(kMissileStationHooks[i], true);
  fprintf(stderr, "[mods] missile stations reload all ammo: %zu/%zu hooks\n", armed, count);
}

/* ── save stations ───────────────────────────────────────────────────── */

uint32 SmModSaveStationRefill(CpuState *cpu, uint32 pc24) {
  (void)pc24;
  Fill(cpu, kEnergy, kMaxEnergy);
  Fill(cpu, kReserve, kMaxReserve);
  Fill(cpu, kMissiles, kMaxMissiles);
  Fill(cpu, kSupers, kMaxSupers);
  Fill(cpu, kPowerBombs, kMaxPowerBombs);
  return 0;
}

static void SmSaveStationActivate(void) {
  bool armed = snes_mod_hook_arm(0x848CF1, true);
  fprintf(stderr, "[mods] save stations refill everything: %s\n", armed ? "armed" : "FAILED");
}

static void SmStationReset(void) {
  for (size_t i = 0; i < sizeof(kMissileStationHooks) / sizeof(kMissileStationHooks[0]); ++i)
    snes_mod_hook_arm(kMissileStationHooks[i], false);
  snes_mod_hook_arm(0x848CF1, false);
}

SNES_MOD_CONSTRUCTOR(sm_register_station_plugins) {
  snes_mod_register_reset_callback(SmStationReset);
  snes_mod_register_activation_plugin("super-metroid.balance-fixes.missile-station",
                                      SmMissileStationActivate);
  snes_mod_register_activation_plugin("super-metroid.balance-fixes.save-station",
                                      SmSaveStationActivate);
}
