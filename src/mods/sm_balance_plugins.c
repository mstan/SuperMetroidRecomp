/* super-metroid.balance-fixes: Tourian exit and power bomb reveal.
 * Every behaviour is a mod_hook on the stock code; the ROM is never written.
 *
 * tourian-exit: Samus can walk back out of Tourian once the baby metroid has
 *   let go of her in the Big Boy room (Super Metroid Redux, "Fixed Tourian
 *   Unlocked Doors"). Stock locks the right-hand doors of the Dust Torizo
 *   room ($DC65) and the Big Boy room ($DCB1) for good: their grey door PLMs
 *   (room arguments $90A4, $90A6) carry lock type $90, whose pre-instruction
 *   only plays the dud sound. The feature:
 *   - sets Tourian boss bit 4 when the baby metroid releases Samus
 *     (Shitroid_Func_16, LDA #$0007 / QueueMusic at $A9:F29A, in the Big Boy
 *     room). Stock Tourian never uses that bit.
 *   - gives both doors lock type $08 ("boss bit 4 set", the Torizo door
 *     type) as PlmSetup_C794_GreyDoor reads it ($84:C794), so they open
 *     after the cutscene.
 *   - lets the same bit pick the three rooms' alternate states: their
 *     condition is "event $14", which stock never sets
 *     (RoomDefStateSelect_IsEventSet's CheckEventHappened call, $8F:E618).
 *     As LoadStateHeader stores those states ($82:DF56), the Blue Hopper and
 *     Big Boy rooms play the Tourian theme (music $1E/5, as the Dust Torizo
 *     room's alternate state already does), and the Big Boy room loads its
 *     enemy population from its third entry ($A1:E28E): no baby metroid and
 *     no live sidehopper, the corpses stay.
 *   Redux uses event 1 and deletes enemy slots 0-1 from new room setup code;
 *   the boss bit and the shorter population list do the same without an
 *   event that other rooms could read.
 *
 * power-bomb-reveal: power bombs reveal hidden super missile blocks the way
 *   bombs do (Super Metroid Redux PBRevealTiles, Ancient Chozo). The super
 *   missile block PLM setups ($84:CF67 respawning, $84:CFA0 not) compare the
 *   hitting projectile's type with $0500 (bomb) to start the reveal; the
 *   hooks on those CMPs ($84:CF70, $84:CFA9) present a power bomb ($0300)
 *   as a bomb. Every other block already reacts to power bombs at least as
 *   much as to bombs.
 *
 * The hooks are declared in recomp/bank02.cfg, bank04.cfg, bank0f.cfg and
 * bank29.cfg.
 */

#include <stdbool.h>
#include <stdio.h>

#include "mod_hook.h"
#include "mod_runtime.h"
#include "sm_mod_support.h"

enum {
  kRoom = 0x079B, kArea = 0x079F,
  kMusicTrack = 0x07C9, kMusicData = 0x07CB, kEnemyPopulation = 0x07CF,
  kPlmId = 0x1C37, kPlmArgument = 0x1DC7,
  kBossBits = 0xD828, kTourian = 5, kExitBit = 4,
};

static uint16_t W(const CpuState *cpu, uint32_t a) {
  return (uint16_t)(cpu->ram[a] | cpu->ram[a + 1] << 8);
}
static void SetW(CpuState *cpu, uint32_t a, uint16_t v) {
  cpu->ram[a] = (uint8_t)v;
  cpu->ram[a + 1] = (uint8_t)(v >> 8);
}
static uint32 At(uint32 pc24, uint16_t pc) { return (pc24 & 0xFF0000u) | pc; }

/* ── tourian-exit ─────────────────────────────────────────────────────── */

static bool g_tourian;

static bool InTourian(const CpuState *cpu) { return W(cpu, kArea) == kTourian; }
static bool ExitOpen(const CpuState *cpu) {
  return (cpu->ram[kBossBits + kTourian] & kExitBit) != 0;
}

/* Shitroid_Func_16: the baby metroid lets go of Samus. */
uint32 SmTourianDrainDoneHook(CpuState *cpu, uint32 pc24) {
  (void)pc24;
  if (g_tourian && InTourian(cpu) && W(cpu, kRoom) == 0xDCB1)
    cpu->ram[kBossBits + kTourian] |= kExitBit;
  return 0;
}

/* PlmSetup_C794_GreyDoor, Y = the PLM: LDA $1DC8,Y reads the lock type. */
uint32 SmTourianGreyDoorHook(CpuState *cpu, uint32 pc24) {
  (void)pc24;
  if (!g_tourian || !InTourian(cpu) || W(cpu, kPlmId + cpu->Y) != 0xC842) return 0;
  uint16_t room = W(cpu, kRoom), argument = W(cpu, kPlmArgument + cpu->Y);
  if ((room == 0xDC65 && argument == 0x90A4) || (room == 0xDCB1 && argument == 0x90A6))
    SetW(cpu, kPlmArgument + cpu->Y, (uint16_t)(0x0800 | (argument & 0xFF)));
  return 0;
}

/* RoomDefStateSelect_IsEventSet, X = the condition's argument: JSL
 * CheckEventHappened. Continue at "state found" ($E61E) or "next" ($E625). */
uint32 SmTourianStateSelectHook(CpuState *cpu, uint32 pc24) {
  if (!g_tourian) return 0;
  uint16_t at = cpu->X;
  if (at != 0xDC26 && at != 0xDC72 && at != 0xDCBE) return 0;
  return At(pc24, ExitOpen(cpu) ? 0xE61E : 0xE625);
}

/* LoadStateHeader, X = the state, after its music and population are in. */
uint32 SmTourianStateHeaderHook(CpuState *cpu, uint32 pc24) {
  (void)pc24;
  if (!g_tourian) return 0;
  if (cpu->X == 0xDC45 || cpu->X == 0xDCDD) {       /* Blue Hopper, Big Boy */
    SetW(cpu, kMusicData, 0x1E);
    SetW(cpu, kMusicTrack, 0x05);
  }
  if (cpu->X == 0xDCDD) SetW(cpu, kEnemyPopulation, 0xE28E);
  return 0;
}

static void SmTourianExitActivate(void) {
  static const uint32_t kHooks[] = {0xA9F29A, 0x84C794, 0x8FE618, 0x82DF56};
  size_t armed = SmModArmHooks("tourian-exit", kHooks, 4);
  g_tourian = true;
  fprintf(stderr, "[mods] tourian exit: %zu/4 hooks\n", armed);
}

/* ── power-bomb-reveal ────────────────────────────────────────────────── */

static bool g_pb_reveal;

/* CMP #$0500 with A = the hitting projectile's type & $0F00. */
uint32 SmPbRevealHook(CpuState *cpu, uint32 pc24) {
  (void)pc24;
  if (g_pb_reveal && cpu->A == 0x0300) cpu->A = 0x0500;
  return 0;
}

static void SmPowerBombRevealActivate(void) {
  static const uint32_t kHooks[] = {0x84CF70, 0x84CFA9};
  size_t armed = SmModArmHooks("power-bomb-reveal", kHooks, 2);
  g_pb_reveal = true;
  fprintf(stderr, "[mods] power bomb reveal: %zu/2 hooks\n", armed);
}

static void SmBalanceReset(void) {
  g_tourian = false;
  g_pb_reveal = false;
}

SNES_MOD_CONSTRUCTOR(sm_register_balance_plugins) {
  snes_mod_register_reset_callback(SmBalanceReset);
  snes_mod_register_activation_plugin("super-metroid.balance-fixes.tourian-exit",
                                      SmTourianExitActivate);
  snes_mod_register_activation_plugin("super-metroid.balance-fixes.power-bomb-reveal",
                                      SmPowerBombRevealActivate);
}
