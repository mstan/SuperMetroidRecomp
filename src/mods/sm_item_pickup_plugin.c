/* super-metroid.pacing / item-pickup: three independent options.
 *
 * Stock pickup: the item PLM runs PlmInstr_ClearMusicQueueAndQueueTrack
 * ($84:8BDD, track 2 = item fanfare), then its collect instruction, which
 * schedules the room music after 360 frames (PlayRoomMusicTrackAfterAFrames,
 * JSL $82:E118) and opens the message box (DisplayMessageBox, JSL $85:8080).
 * HandleMessageBoxInteraction ($85:846D) then waits 360 frames (10 for the
 * map/recharge/"Save completed" boxes, $14/$15/$16/$18) before a held button
 * on the pad registers closes it; the game is frozen throughout.
 *
 * skip-message (Super Metroid Redux, "message boxes can be skipped"): a
 * mod_hook on the DEX of that minimum-wait loop ($85:84A0) ends the wait when
 * a button is pressed that was not already held when the box opened. The
 * loop does not run ReadJoypadInputs, so the hook reads the same auto-joypad
 * registers ($4218/$4219) the stock code reads right after the loop and
 * compares them with joypad1_lastkeys ($8B), frozen at the box opening. Every
 * box that uses the loop is covered: items, map station, energy and missile
 * recharge, "Save completed". The save yes/no prompt keeps its own input.
 *
 * no-freeze (Ancient Chozo): a mod_hook on each collect instruction's
 * JSL DisplayMessageBox skips the box (and its freeze); the fanfare plays as
 * music while the game runs and the room music returns after 360 frames as
 * scheduled.
 *
 * short-jingle (Redux item sounds, with existing sounds only): for an
 * E-tank, a reserve tank, or a missile/super/power-bomb expansion that is
 * not the first of its kind, the fanfare is not started (hook on $84:8BDD,
 * continuing at its INY/RTS $84:8C05), the room music is not rescheduled
 * (hook on the collect's LDA #$0168, continuing at the box's LDA #id) and
 * the pickup sound of the matching enemy drop plays instead (sound library
 * 2: big energy $02, missile $03, super $04, power bomb $05; Eproj_Pickup_*).
 * With the message box shown, DisplayMessageBox's CancelSoundEffects would
 * cut a sound queued before it, so the sound is queued on the box's first
 * wait frame, when no music is pending (stock always has the fanfare's
 * scheduled room music pending there). With no-freeze it is queued at the
 * skipped box call instead. "First of its kind" is decided from guest state
 * alone: max ammo 0 before the collect, max == the PLM argument after it.
 */

#include <stdio.h>

#include "common_cpu_infra.h"
#include "mod_hook.h"
#include "mod_runtime.h"
#include "sm_mod_support.h"
#include "snes/snes.h"

#define PACKAGE "super-metroid.pacing"
#define FEATURE "item-pickup"

#define RAM16(cpu, a) ((uint16_t)((cpu)->ram[(a)] | (cpu)->ram[(a) + 1] << 8))

enum {
  kJoypad1Held = 0x8B,
  kSfxReadPos2 = 0x644, kSfxWritePos2 = 0x647, kSfxMaxQueued2 = 0x654,
  kSfx2Queue = 0x666, kDebugDisableSounds = 0x5F5, kPowerBombStatus = 0x592,
  kGameState = 0x998,
  kMusicWritePos = 0x639, kMusicReadPos = 0x63B,
  kMaxMissiles = 0x9C8, kMaxSupers = 0x9CC, kMaxPowerBombs = 0x9D0,
  kMessageBoxIndex = 0x1C1F,
};

static bool g_skip_message, g_no_freeze, g_short_jingle;

typedef enum { kEtank, kReserve, kMissile, kSuper, kPowerBomb, kKinds } Kind;
typedef struct {
  uint16_t instr;      /* PLM instruction entry in bank $84 */
  uint16_t music_lda;  /* LDA #$0168 before JSL PlayRoomMusicTrackAfterAFrames */
  uint16_t box_lda;    /* LDA #index before JSL DisplayMessageBox */
  uint16_t box_jsl;    /* JSL DisplayMessageBox */
  uint16_t after;      /* INY after the box (documentation; see kBoxCalls) */
  uint16_t box_index;
  uint16_t max_addr;   /* 0: always minor */
  uint8_t sfx;
} Minor;
static const Minor kMinor[kKinds] = {
  {0x8968, 0x8975, 0x897C, 0x897F, 0x8983, 0x01, 0, 0x02},             /* E-tank */
  {0x8986, 0x8998, 0x899F, 0x89A2, 0x89A6, 0x19, 0, 0x02},             /* reserve */
  {0x89A9, 0x89C1, 0x89C8, 0x89CB, 0x89CF, 0x02, kMaxMissiles, 0x03},  /* missile */
  {0x89D2, 0x89EA, 0x89F1, 0x89F4, 0x89F8, 0x03, kMaxSupers, 0x04},    /* super */
  {0x89FB, 0x8A13, 0x8A1A, 0x8A1D, 0x8A21, 0x04, kMaxPowerBombs, 0x05},/* PB */
};
/* Every collect instruction's JSL DisplayMessageBox and the INY after it. */
static const struct { uint16_t jsl, after; } kBoxCalls[] = {
  {0x88EB, 0x88EF}, /* PlmInstr_PickupBeamAndShowMessage */
  {0x8912, 0x8916}, /* PlmInstr_PickupEquipmentAndShowMessage */
  {0x893A, 0x893E}, /* PlmInstr_PickupEquipmentAddGrappleShowMessage */
  {0x8961, 0x8965}, /* PlmInstr_PickupEquipmentAddXrayShowMessage */
  {0x897F, 0x8983}, /* PlmInstr_CollectHealthEnergyTank */
  {0x89A2, 0x89A6}, /* PlmInstr_CollectHealthReserveTank */
  {0x89CB, 0x89CF}, /* PlmInstr_CollectAmmoMissileTank */
  {0x89F4, 0x89F8}, /* PlmInstr_CollectAmmoSuperMissileTank */
  {0x8A1D, 0x8A21}, /* PlmInstr_CollectAmmoPowerBombTank */
};

/* QueueSfx2_Max6 ($80:90CB): the stock queue rules, written directly. */
static void QueueSfx2(CpuState *cpu, uint8_t id) {
  uint8_t *r = cpu->ram;
  const uint8_t max = 6;
  r[kSfxMaxQueued2] = max;
  if (((r[kSfxWritePos2] - r[kSfxReadPos2]) & 0xF) >= max) return;
  if (RAM16(cpu, kDebugDisableSounds) || RAM16(cpu, kGameState) >= 0x28 ||
      (RAM16(cpu, kPowerBombStatus) & 0x8000))
    return;
  uint8_t w = r[kSfxWritePos2], next = (uint8_t)((w + 1) & 0xF);
  if (next == r[kSfxReadPos2]) return; /* full: stock keeps the lower id */
  r[kSfx2Queue + w] = id;
  r[kSfxWritePos2] = next;
  r[kSfx2Queue + next] = 0;
}

/* Before the collect instruction runs (max still stock). */
static bool MinorBefore(const CpuState *cpu, const Minor *m) {
  return !m->max_addr || RAM16(cpu, m->max_addr) != 0;
}
/* Inside the collect instruction: max already raised by the PLM argument
 * at bank $84:Y; the first expansion of a kind leaves max == argument. */
static bool MinorAfter(const CpuState *cpu, const Minor *m) {
  return !m->max_addr || RAM16(cpu, m->max_addr) != SmModRomWord(0x840000u | cpu->Y);
}

/* $84:8BDD PlmInstr_ClearMusicQueueAndQueueTrack. Y -> track byte; the next
 * PLM instruction follows it. */
uint32 SmItemFanfareHook(CpuState *cpu, uint32 pc24) {
  uint16_t next = SmModRomWord(0x840000u | (uint16_t)(cpu->Y + 1));
  for (int k = 0; k < kKinds; ++k)
    if (kMinor[k].instr == next && MinorBefore(cpu, &kMinor[k]))
      return (pc24 & 0xFF0000u) | 0x8C05u;
  return 0;
}

/* LDA #$0168 in a minor collect instruction. */
uint32 SmItemRoomMusicHook(CpuState *cpu, uint32 pc24) {
  for (int k = 0; k < kKinds; ++k) {
    const Minor *m = &kMinor[k];
    if (m->music_lda != (uint16_t)pc24) continue;
    return MinorAfter(cpu, m) ? (pc24 & 0xFF0000u) | m->box_lda : 0;
  }
  return 0;
}

/* JSL DisplayMessageBox in a collect instruction (no-freeze). */
uint32 SmItemMessageHook(CpuState *cpu, uint32 pc24) {
  if (g_short_jingle)
    for (int k = 0; k < kKinds; ++k)
      if (kMinor[k].box_jsl == (uint16_t)pc24 && MinorAfter(cpu, &kMinor[k]))
        QueueSfx2(cpu, kMinor[k].sfx);
  for (size_t i = 0; i < sizeof(kBoxCalls) / sizeof(kBoxCalls[0]); ++i)
    if (kBoxCalls[i].jsl == (uint16_t)pc24)
      return (pc24 & 0xFF0000u) | kBoxCalls[i].after;
  return 0;
}

/* $85:84A0 DEX of HandleMessageBoxInteraction's minimum wait (X = frames
 * left, 16-bit). */
uint32 SmMessageBoxWaitHook(CpuState *cpu, uint32 pc24) {
  (void)pc24;
  uint16_t box = RAM16(cpu, kMessageBoxIndex) & 0xFF;
  if (g_short_jingle && cpu->X == 0x168 &&
      RAM16(cpu, kMusicReadPos) == RAM16(cpu, kMusicWritePos)) {
    for (int k = 0; k < kKinds; ++k)
      if (kMinor[k].box_index == box) QueueSfx2(cpu, kMinor[k].sfx);
  }
  if (g_skip_message && g_snes) {
    uint16_t pad = (uint16_t)(snes_readReg(g_snes, 0x4218) |
                              snes_readReg(g_snes, 0x4219) << 8);
    if (pad & ~RAM16(cpu, kJoypad1Held)) cpu->X = 1;
  }
  return 0;
}

static void SmItemPickupActivate(void) {
  g_skip_message = SmModOptionBool(PACKAGE, FEATURE, "skip-message", true);
  g_no_freeze = SmModOptionBool(PACKAGE, FEATURE, "no-freeze", false);
  g_short_jingle = SmModOptionBool(PACKAGE, FEATURE, "short-jingle", true);
  uint32_t hooks[32];
  size_t n = 0;
  if (g_skip_message || g_short_jingle) hooks[n++] = 0x8584A0;
  if (g_short_jingle) {
    hooks[n++] = 0x848BDD;
    for (int k = 0; k < kKinds; ++k) hooks[n++] = 0x840000u | kMinor[k].music_lda;
  }
  if (g_no_freeze)
    for (size_t i = 0; i < sizeof(kBoxCalls) / sizeof(kBoxCalls[0]); ++i)
      hooks[n++] = 0x840000u | kBoxCalls[i].jsl;
  size_t armed = SmModArmHooks("item-pickup", hooks, n);
  fprintf(stderr, "[mods] item pickup skip-message=%d no-freeze=%d short-jingle=%d: %zu/%zu hooks\n",
          g_skip_message, g_no_freeze, g_short_jingle, armed, n);
}

SNES_MOD_CONSTRUCTOR(sm_register_item_pickup_plugin) {
  snes_mod_register_activation_plugin("super-metroid.pacing.item-pickup",
                                      SmItemPickupActivate);
}
