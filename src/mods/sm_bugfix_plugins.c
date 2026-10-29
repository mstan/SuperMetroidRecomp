/* super-metroid.balance-fixes: vanilla bug fixes, one switch (option) each.
 * Every fix is a mod_hook on the stock code; the ROM is never written.
 *
 * bug-fixes (Super Metroid Redux vanillabugfixes.asm, compiled by Nodever2
 * from PJBoy, Benox50, amoeba and others, plus HAM's suit pickup fix):
 *   screw-attack-menu   EquipmentScreenMoveLowerOnSuitsMisc stops before
 *                       the screw attack entry when Samus has neither spring
 *                       ball nor boots (CPX #$000A, should be #$000C). The
 *                       hook on the CPX ($82:B4C4) continues the loop for
 *                       index $0A.
 *   xray-respawning-plm LoadXrayBlocks branches out with X still pushed on
 *                       a respawning PLM (BMI at $84:8331) and crashes; it
 *                       now continues at $84:8342 with the stack as the
 *                       normal path has it, showing the item (amoeba).
 *   bomb-block-screw    PlmSetup_RespawningBombBlock ($84:CE83) breaks the
 *                       block for any speed/shinespark/screw contact damage
 *                       ($0A6E = 1-3) instead of a list of poses, so a
 *                       screw attack wall jump breaks bomb blocks (PJBoy).
 *   eye-door-projectile EprojPreInstr_EyeDoorProjectile's shared exit
 *                       assumes X = the projectile; from the door-bit test
 *                       ($86:B704) X is the bit index. X = Y there (PJBoy).
 *   speed-booster-sound The speed booster animation queues a sound and then
 *                       uses A as the boost counter (JSL clobbers it); A is
 *                       the counter again at $90:85AA (Nodever2).
 *   speed-booster-jump  Samus_InitJump adds half the extra run speed to the
 *                       jump speed without carrying between the halves; the
 *                       hook ($90:9927) adds it as one 32-bit value
 *                       (Nodever2, Speed Booster Vertical Jump Speed Fix).
 *   wall-jump-screw     Samus_Movement_14_WallJumping gives screw attack
 *                       contact damage only from animation frame $17: with
 *                       screw attack it is set for the whole wall jump, else
 *                       pseudo screw from a full charge (PJBoy, $90:A737).
 *   blue-suit-palette   The blue suit palette index is used before being
 *                       range-checked, so coming from the screw attack
 *                       palette flashes garbage; clamped first ($91:DA89,
 *                       Nodever2).
 *   screw-iframes       Hitting an enemy with screw attack resets Samus'
 *                       invincibility timer; the STZ $18A8 at $A0:A096 and
 *                       $A0:9A90 is skipped (PJBoy).
 *   ws-ghost-position   Wrecked Ship ghost (Atomic) routines read Samus'
 *                       position indexed by the enemy (LDA $0AFA,X etc. at
 *                       $A8:9C13/9D45/9DC3/9DCD); they read it unindexed.
 *   sprite-objects      ClearSpriteObjects loops with BNE and never clears
 *                       entry 0 ($7E:EF78); cleared at $B4:BDA3 (PJBoy).
 *   suit-pickup-palette The Varia/Gravity item PLMs grant the suit at
 *                       pickup, so the suit palette changes before the
 *                       pickup sequence's stage 3 grants it again. For the
 *                       six suit PLMs the grant's ORAs ($84:88F6, $84:88FF)
 *                       are skipped (HAM, VariaGravityPickupFix).
 *
 * reserve-tanks (Nodever2's ReserveTank_Bugfixes with Benox50's crash fix):
 *   pause-crash         HandleSamusOutOfHealthAndGameTile returns unless in
 *                       main gameplay, so pausing on the frame auto reserves
 *                       trigger no longer crashes ($82:DB73).
 *   come-full           A new reserve tank is filled ($84:8990).
 *   keep-leftover       Auto and manual refills stop when Samus is full and
 *                       keep what is left in the tanks ($82:DC45, $82:AF81).
 *   no-heat-damage      No heat damage while auto reserves refill
 *                       (periodic subdamage cleared at $82:DC31).
 *   keep-iframes        Auto reserves do not run the invincibility timer
 *                       down ($82:DC31).
 *   manual-control      A stops a manual refill, and moving off the reserve
 *                       button cancels it instead of resuming later
 *                       ($82:AF52, $82:AC52).
 *
 * ship-save: the native ship dialog shows Save Completed before returning
 * to GunshipTop_13, which performs the save. Return Yes before that message,
 * let the gunship call native SaveToSram, then reuse its DisplayMessageBox
 * call for Save Completed. Native message index/choice encode both phases;
 * no extra state or guest-call bridge is needed (Redux ShipBugFix behavior).
 *
 * The hooks are declared in recomp/bank02/04/05/06/10/11/20/22/28/34.cfg.
 */

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "mod_hook.h"
#include "mod_runtime.h"
#include "sm_mod_support.h"

#define PACKAGE "super-metroid.balance-fixes"

enum {
  kNew = 0x8F,
  kMessageChoice = 0x05F9, kMessageIndex = 0x1C1F,
  kPauseItem = 0x0755, kManualCounter = 0x0757,
  kGameState = 0x0998, kEquipped = 0x09A2,
  kHealth = 0x09C2, kMaxHealth = 0x09C4, kMaxReserve = 0x09D4, kReserve = 0x09D6,
  kPeriodicSubDamage = 0x0A4E, kContactDamage = 0x0A6E, kSuitPalIndex = 0x0ACE,
  kSamusX = 0x0AF6, kSamusY = 0x0AFA, kSpeedBoost = 0x0B3E,
  kYSubSpeed = 0x0B2C, kYSpeed = 0x0B2E, kExtraRun = 0x0B42, kExtraRunSub = 0x0B44,
  kFlare = 0x0CD0, kInvincible = 0x18A8, kSpriteObjects = 0xEF78,
};

static uint16_t W(const CpuState *cpu, uint32_t a) {
  return (uint16_t)(cpu->ram[a] | cpu->ram[a + 1] << 8);
}
static void SetW(CpuState *cpu, uint32_t a, uint16_t v) {
  cpu->ram[a] = (uint8_t)v;
  cpu->ram[a + 1] = (uint8_t)(v >> 8);
}
static uint32 At(uint32 pc24, uint16_t pc) { return (pc24 & 0xFF0000u) | pc; }
static uint32 Next(uint32 pc24, uint16_t len) { return At(pc24, (uint16_t)((pc24 & 0xFFFF) + len)); }

/* Enabled fixes; all false when the features are off. */
static struct {
  bool reserve_heat, reserve_iframes, reserve_keep, reserve_manual;
} g;

/* ── bug-fixes ────────────────────────────────────────────────────────── */

uint32 SmFixScrewMenuHook(CpuState *cpu, uint32 pc24) {
  return cpu->X == 0x000A ? At(pc24, 0xB4BA) : 0;
}

uint32 SmFixXrayRespawningPlmHook(CpuState *cpu, uint32 pc24) {
  return (cpu->A & 0x8000) ? At(pc24, 0x8342) : 0;   /* BMI taken */
}

uint32 SmFixBombBlockScrewHook(CpuState *cpu, uint32 pc24) {
  uint16_t index = W(cpu, kContactDamage);
  return At(pc24, (uint16_t)(index - 1) < 3 ? 0xCEC1 : 0xCEB9);
}

uint32 SmFixEyeDoorProjectileHook(CpuState *cpu, uint32 pc24) {
  (void)pc24;
  if (cpu->A) cpu->X = cpu->Y;                       /* BNE to the exit */
  return 0;
}

uint32 SmFixSpeedBoosterSfxHook(CpuState *cpu, uint32 pc24) {
  (void)pc24;
  cpu->A = W(cpu, kSpeedBoost);
  return 0;
}

uint32 SmFixSpeedBoosterJumpHook(CpuState *cpu, uint32 pc24) {
  uint32_t half = ((uint32_t)W(cpu, kExtraRun) << 16 | W(cpu, kExtraRunSub)) >> 1;
  uint32_t y = ((uint32_t)W(cpu, kYSpeed) << 16 | W(cpu, kYSubSpeed)) + half;
  SetW(cpu, kYSubSpeed, (uint16_t)y);
  SetW(cpu, kYSpeed, (uint16_t)(y >> 16));
  return At(pc24, 0x993A);
}

uint32 SmFixWallJumpScrewHook(CpuState *cpu, uint32 pc24) {
  if (W(cpu, kEquipped) & 0x0008)
    SetW(cpu, kContactDamage, 3);                    /* screw attack */
  else if ((int16_t)W(cpu, kFlare) >= 0x3C)
    SetW(cpu, kContactDamage, 4);                    /* pseudo screw */
  return At(pc24, 0xA75A);                           /* JSR jumping movement */
}

uint32 SmFixBlueSuitPaletteHook(CpuState *cpu, uint32 pc24) {
  (void)pc24;
  if ((int16_t)W(cpu, kSuitPalIndex) > 6) SetW(cpu, kSuitPalIndex, 6);
  return 0;
}

uint32 SmFixScrewIframesHook(CpuState *cpu, uint32 pc24) {
  (void)cpu;
  return Next(pc24, 3);                              /* past STZ $18A8 */
}

/* LDA $0AFA,X / LDA $0AF6,X -> the unindexed load. */
uint32 SmFixWsGhostHook(CpuState *cpu, uint32 pc24) {
  uint16_t v = W(cpu, (pc24 & 0xFFFF) == 0x9DC3 ? kSamusX : kSamusY);
  cpu->A = v;
  cpu->_flag_N = (uint8_t)(v >> 15);
  cpu->_flag_Z = v == 0;
  return Next(pc24, 3);
}

uint32 SmFixSpriteObjectClearHook(CpuState *cpu, uint32 pc24) {
  (void)pc24;
  if (cpu->X == 0) SetW(cpu, kSpriteObjects, 0);    /* the BNE falls out */
  return 0;
}

/* PlmInstr_PickupEquipmentAndShowMessage, Y = its argument: the ORA of the
 * granted items into $09A2 ($84:88F6) / $09A4 ($84:88FF). For the six suit
 * PLMs the ORA is skipped (continue at the STA). */
static bool SuitPlmArgument(uint16_t y) {
  static const uint16_t kSuitArgs[] = {
    0xE2CA, 0xE2FF,          /* Varia, Gravity */
    0xE727, 0xE769,          /* Chozo orb */
    0xEC31, 0xEC79,          /* shot block */
  };
  for (size_t i = 0; i < sizeof(kSuitArgs) / sizeof(kSuitArgs[0]); ++i)
    if (y == kSuitArgs[i]) return true;
  return false;
}

uint32 SmFixSuitPickupHook(CpuState *cpu, uint32 pc24) {
  return SuitPlmArgument(cpu->Y) ? At(pc24, (uint16_t)((pc24 & 0xFFFF) + 3)) : 0;
}

/* Ship Yes is choice0; No is2. Tear down the prompt on Yes while retaining
 * index1C so native MessageBoxPostProcess returns that actual choice. */
uint32 SmFixShipPromptHook(CpuState *cpu, uint32 pc24) {
  return W(cpu, kMessageIndex) == 0x1C && W(cpu, kMessageChoice) == 0
      ? At(pc24, 0x80E8) : 0;
}

/* The second dialog (18) has already followed the native save; do not repeat
 * the save when its call returns to the shared gunship instruction. */
uint32 SmFixShipMessageReturnHook(CpuState *cpu, uint32 pc24) {
  if (W(cpu, kMessageIndex) != 0x18) return 0;
  /* MessageBoxPostProcess returns18; retain the skipped CMP #2 flags. */
  cpu->_flag_C = 1;
  cpu->_flag_N = cpu->_flag_Z = 0;
  return At(pc24, 0xAB40);
}

/* A fresh Saved dialog normally uses the generic path. For the gunship's
 * second call, retain its original refill sound and 160-frame lead-in before
 * opening the confirmation. The actual native actor/room identify the call;
 * station confirmations and other message18 uses keep their normal timing. */
uint32 SmFixShipPresentationHook(CpuState *cpu, uint32 pc24) {
  uint16_t enemy = W(cpu, 0x0E54);
  if (W(cpu, kMessageIndex) != 0x18 || W(cpu, 0x079B) != 0x91F8 ||
      (enemy & 0x3F) || enemy >= 0x800 || W(cpu, 0x0F78 + enemy) != 0xD07F ||
      W(cpu, 0x0FB2 + enemy) != 0xAB1F)
    return 0;
  return At(pc24, 0x80D6);
}

/* Both native Yes/save and No branches reach AB40. Only the first Yes
 * return (index1C, choice0) may show Saved. Reuse JSL DisplayMessageBox in
 * the same gunship function, with the native LDA #18 register/flag effects.
 * That dialog changes index to18, preventing re-entry here afterward. */
uint32 SmFixShipSavedHook(CpuState *cpu, uint32 pc24) {
  if (W(cpu, kMessageIndex) != 0x1C || W(cpu, kMessageChoice) != 0) return 0;
  cpu->A = 0x18;
  cpu->_flag_N = cpu->_flag_Z = 0;
  return At(pc24, 0xAB22);
}

/* ── reserve-tanks ────────────────────────────────────────────────────── */

uint32 SmFixReservePauseHook(CpuState *cpu, uint32 pc24) {
  return W(cpu, kGameState) != 8 ? At(pc24, 0xDB9D) : 0;   /* PLP ; RTS */
}

uint32 SmFixReserveFullHook(CpuState *cpu, uint32 pc24) {
  (void)pc24;
  SetW(cpu, kReserve, W(cpu, kMaxReserve));
  return 0;
}

/* RefillHealthFromReserveTanks entry. */
uint32 SmFixReserveAutoEntryHook(CpuState *cpu, uint32 pc24) {
  (void)pc24;
  if (g.reserve_heat) SetW(cpu, kPeriodicSubDamage, 0);
  if (g.reserve_iframes) SetW(cpu, kInvincible, (uint16_t)(W(cpu, kInvincible) + 1));
  return 0;
}

/* One unit from the tanks while Samus is not full; reports whether the
 * refill is over. */
static bool RefillStep(CpuState *cpu, uint16_t amount) {
  uint16_t reserve = W(cpu, kReserve);
  if (!reserve) return true;
  uint16_t health = (uint16_t)(W(cpu, kHealth) + amount);
  if ((int16_t)(health - W(cpu, kMaxHealth)) > 0) return true;
  SetW(cpu, kHealth, health);
  reserve = reserve > amount ? (uint16_t)(reserve - amount) : 0;
  SetW(cpu, kReserve, reserve);
  return reserve == 0;
}

/* Auto refill, after the sound: health += 1 ... */
uint32 SmFixReserveAutoKeepHook(CpuState *cpu, uint32 pc24) {
  if (!g.reserve_keep) return 0;
  uint16_t reserve = W(cpu, kReserve);
  uint16_t health = (uint16_t)(W(cpu, kHealth) + 1);
  if ((int16_t)(health - W(cpu, kMaxHealth)) > 0)
    return At(pc24, 0xDC7C);                         /* SEC ; RTS: done */
  SetW(cpu, kHealth, health);
  SetW(cpu, kReserve, (uint16_t)(reserve - 1));
  return At(pc24, 0xDC77);                           /* done when empty */
}

/* Manual refill: A pressed while refilling stops it. */
uint32 SmFixReserveManualCancelHook(CpuState *cpu, uint32 pc24) {
  if (!g.reserve_manual || !W(cpu, kManualCounter) || !(W(cpu, kNew) & 0x0080))
    return 0;
  SetW(cpu, kPauseItem, 0);
  SetW(cpu, kManualCounter, 0);
  return At(pc24, 0xAFBC);                           /* PLP ; RTS */
}

/* Manual refill, after the sound: health += [$82:BF04] ... */
uint32 SmFixReserveManualKeepHook(CpuState *cpu, uint32 pc24) {
  if (!g.reserve_keep) return 0;
  bool done = RefillStep(cpu, SmModReadRom16(0x82BF04));
  return At(pc24, done ? 0xAFB3 : 0xAFBC);           /* cleanup, tanks kept */
}

/* Equipment screen handler: off the reserve button, no refill runs. */
uint32 SmFixReserveManualDeselectHook(CpuState *cpu, uint32 pc24) {
  (void)pc24;
  if (g.reserve_manual && W(cpu, kPauseItem) != 0x0100) SetW(cpu, kManualCounter, 0);
  return 0;
}

/* ── activation ───────────────────────────────────────────────────────── */

typedef struct {
  const char *option;
  uint32_t pcs[4];
} Fix;

static size_t ArmFix(const char *feature, const Fix *fix) {
  size_t n = 0;
  while (n < 4 && fix->pcs[n]) ++n;
  char name[96];
  snprintf(name, sizeof(name), "%s/%s", feature, fix->option);
  return SmModArmHooks(name, fix->pcs, n);
}

static const Fix kBugFixes[] = {
  {"screw-attack-menu", {0x82B4C4}},
  {"xray-respawning-plm", {0x848331}},
  {"bomb-block-screw", {0x84CE83}},
  {"eye-door-projectile", {0x86B704}},
  {"speed-booster-sound", {0x9085AA}},
  {"speed-booster-jump", {0x909927}},
  {"wall-jump-screw", {0x90A737}},
  {"blue-suit-palette", {0x91DA89}},
  {"screw-iframes", {0xA0A096, 0xA09A90}},
  {"ws-ghost-position", {0xA89C13, 0xA89D45, 0xA89DC3, 0xA89DCD}},
  {"sprite-objects", {0xB4BDA3}},
  {"suit-pickup-palette", {0x8488F6, 0x8488FF}},
  {"ship-save", {0x8580CD, 0x858093, 0xA2AB26, 0xA2AB40}},
};

static void SmBugFixesActivate(void) {
  unsigned on = 0, total = 0;
  for (size_t i = 0; i < sizeof(kBugFixes) / sizeof(kBugFixes[0]); ++i, ++total) {
    if (!SmModOptionBool(PACKAGE, "bug-fixes", kBugFixes[i].option, true)) continue;
    size_t n = 0;
    while (n < 4 && kBugFixes[i].pcs[n]) ++n;
    on += ArmFix("bug-fixes", &kBugFixes[i]) == n;
  }
  fprintf(stderr, "[mods] vanilla bug fixes: %u/%u on\n", on, total);
}

static void SmReserveTanksActivate(void) {
  static const Fix kPause = {"pause-crash", {0x82DB73}};
  static const Fix kFull = {"come-full", {0x848990}};
  static const Fix kKeep = {"keep-leftover", {0x82DC45, 0x82AF81}};
  static const Fix kAutoEntry = {"auto-refill", {0x82DC31}};
  static const Fix kManual = {"manual-control", {0x82AF52, 0x82AC52}};
  if (SmModOptionBool(PACKAGE, "reserve-tanks", "pause-crash", true))
    ArmFix("reserve-tanks", &kPause);
  if (SmModOptionBool(PACKAGE, "reserve-tanks", "come-full", false))
    ArmFix("reserve-tanks", &kFull);
  g.reserve_keep = SmModOptionBool(PACKAGE, "reserve-tanks", "keep-leftover", true);
  if (g.reserve_keep) ArmFix("reserve-tanks", &kKeep);
  g.reserve_heat = SmModOptionBool(PACKAGE, "reserve-tanks", "no-heat-damage", true);
  g.reserve_iframes = SmModOptionBool(PACKAGE, "reserve-tanks", "keep-iframes", true);
  if (g.reserve_heat || g.reserve_iframes) ArmFix("reserve-tanks", &kAutoEntry);
  g.reserve_manual = SmModOptionBool(PACKAGE, "reserve-tanks", "manual-control", false);
  if (g.reserve_manual) ArmFix("reserve-tanks", &kManual);
  fprintf(stderr, "[mods] reserve tank fixes: keep %d heat %d iframes %d manual %d\n",
          g.reserve_keep, g.reserve_heat, g.reserve_iframes, g.reserve_manual);
}

static void SmBugFixReset(void) { memset(&g, 0, sizeof(g)); }

SNES_MOD_CONSTRUCTOR(sm_register_bugfix_plugins) {
  snes_mod_register_reset_callback(SmBugFixReset);
  snes_mod_register_activation_plugin("super-metroid.balance-fixes.bug-fixes",
                                      SmBugFixesActivate);
  snes_mod_register_activation_plugin("super-metroid.balance-fixes.reserve-tanks",
                                      SmReserveTanksActivate);
}
