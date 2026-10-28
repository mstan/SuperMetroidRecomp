/* super-metroid.combat: weapon features. Each is its own switch,
 * reimplemented on the stock routines from the behaviour of the ROM hacks
 * credited in the package manifest. Every hook reads only guest state and
 * the feature options, so a run is reproducible. Hooks are declared in
 * recomp/bank06.cfg ($86), bank10.cfg ($90), bank14.cfg ($94) and
 * bank20.cfg ($A0).
 *
 * screw-attack: screw attack breaks shot blocks and frozen enemies (option:
 *   also while speed boosting or shinesparking).
 *   - Shot blocks: Samus' block collision dispatches on the block type
 *     (JSR ($94D5,X) at $94:9527, JSR ($94F5,X) at $94:953E). For a shot
 *     block (type $C, BTS 0-7) the hook sends it to the bomb block handler
 *     ($94:932D / $94:934C, X = $1E), whose JSL SpawnPLM ($94:933F /
 *     $94:935E) then spawns the shot block's own breaking PLM
 *     (kBlockShotBombedReactionShootablePlm, $94:9EA6) instead of the bomb
 *     block's. Those setups always break (respawning or not), so Samus goes
 *     through as through a bomb block. Power bomb, super missile and door
 *     blocks (BTS 8 and up) stay solid.
 *   - Frozen enemies: Samus' solid-enemy collision ($A0:AA24) handles
 *     frozen and platform enemies alike. While screw attacking against a
 *     frozen non-platform enemy, not from above, the hook clears its freeze
 *     timer and skips the solid response, so the normal contact damage hits
 *     it (Adam's ScrewAttackFrozenEnemies, as in Super Metroid Redux).
 *
 * item-magnet: while the beam charges, item drops move toward Samus (Ancient
 *   Chozo's tractor beam). The pickup pre-instruction ($86:EFE0) runs once
 *   per drop per frame; the hook moves the drop straight at Samus at the
 *   chosen speed before the stock touch check. Integer maths only.
 *
 * bomb-limit: more than five bombs at once. Stock bombs live in projectile
 *   slots 5-9 and the morph ball handler refuses a sixth
 *   (HudSelectionHandler_MorphBall_Helper2's CMP #$0005, $90:C0F3). With the
 *   feature, a bomb or power bomb takes a free slot 5-9, else a free beam
 *   slot 0-4 (the slot searches at $90:BFCF and $90:C039). A bomb in a beam
 *   slot also counts in the beam counter ($0CCE), so a beam can never take
 *   its slot; ClearProjectile ($90:ADB7) gives its bomb count back. The
 *   enemy bomb collision loops ($A0:A26A, $A0:9D6B) and
 *   Eproj_CheckForBombCollisionWithRect ($86:C1BF) scan from slot 0, and
 *   EnemyBombCollHandler's "type bit 15" test ($A0:A290) is limited to slots
 *   5-9, as uncharged beams carry that bit. A bomb lasts about 69 frames
 *   and stock waits 16 frames between bombs (kNonBeamProjectileCooldowns
 *   [5]), which alone caps the count at 4-5, so the wait after a bomb
 *   (STA $0CCC at $90:C017) is an option too.
 *
 * beam-combo-select: Select on a full charge fires the beam's combo (special
 *   beam attack), the power bombs it costs taken as usual, without selecting
 *   power bombs first (Super Metroid Redux). HandleSwitchingHudSelection
 *   ($90:C4E0) does not cycle the HUD item, the beam handler
 *   ($90:B843) calls FireSba, and FireSba ($90:CCC0) skips its "power bombs
 *   selected" check. Only beams that have a combo, with enough power bombs.
 */

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "mod_hook.h"
#include "mod_runtime.h"
#include "sm_mod_support.h"

#define PACKAGE "super-metroid.combat"

enum {
  kHeld = 0x8B, kNew = 0x8F,
  kGameState = 0x0998,
  kEquipped = 0x09A2, kBeams = 0x09A6,
  kCfgShoot = 0x09B2, kCfgItemSwitch = 0x09BA,
  kPowerBombs = 0x09CE, kHudItem = 0x09D2,
  kMoveType = 0x0A1F, kContactDamage = 0x0A6E, kHyperBeam = 0x0A76,
  kTimeFrozen = 0x0A78, kSamusX = 0x0AF6, kSamusY = 0x0AFA,
  kCollisionDir = 0x0B02, kDirChangedPose = 0x0B5E,
  kProjType = 0x0C18,
  kProjCounter = 0x0CCE, kFlare = 0x0CD0, kBombCounter = 0x0CD2,
  kGrappleFunc = 0x0D32, kCurBlock = 0x0DC4,
  kEnemyProps = 0x0F86, kEnemyFrozen = 0x0F9E,
  kCollEnemy = 0x18A6,
  kEprojXSub = 0x1A27, kEprojX = 0x1A4B, kEprojYSub = 0x1A6F, kEprojY = 0x1A93,
  kLevelData = 0x10002, kBts = 0x16402,
};

static uint16_t W(const CpuState *cpu, uint32_t a) {
  return (uint16_t)(cpu->ram[a] | cpu->ram[a + 1] << 8);
}
static void SetW(CpuState *cpu, uint32_t a, uint16_t v) {
  cpu->ram[a] = (uint8_t)v;
  cpu->ram[a + 1] = (uint8_t)(v >> 8);
}
static uint32 At(uint32 pc24, uint16_t pc) { return (pc24 & 0xFF0000u) | pc; }

/* Options of the enabled features; all false/zero when disabled. */
static struct {
  bool shot_blocks, frozen, with_speed;
  bool magnet; uint16_t magnet_charge; uint32_t magnet_step;   /* 16.16 px */
  bool bomb_limit; uint16_t bombs, bomb_interval;
  bool combo_select;
} g;

/* ── screw-attack ─────────────────────────────────────────────────────── */

/* Contact damage index: 1 speed boosting, 2 shinespark, 3 screw attack. */
static bool Breaking(const CpuState *cpu) {
  uint16_t index = W(cpu, kContactDamage);
  return index == 3 || (g.with_speed && (index == 1 || index == 2));
}

static bool BreakableShotBlock(const CpuState *cpu) {
  uint16_t block = W(cpu, kCurBlock);
  return (W(cpu, kLevelData + 2u * block) & 0xF000) == 0xC000 &&
         cpu->ram[kBts + block] <= 7;
}

/* BlockColl_{Horiz,Vert}_CheckColl: JSR (table,X), X = block type * 2. */
uint32 SmCombatShotBlockDispatchHook(CpuState *cpu, uint32 pc24) {
  (void)pc24;
  if (g.shot_blocks && cpu->X == 0x18 && Breaking(cpu) && BreakableShotBlock(cpu))
    cpu->X = 0x1E;                                   /* bomb block handler */
  return 0;
}

/* BlockColl_{Horiz,Vert}_BombBlock: JSL SpawnPLM with A = the PLM. */
uint32 SmCombatShotBlockPlmHook(CpuState *cpu, uint32 pc24) {
  (void)pc24;
  if (g.shot_blocks && BreakableShotBlock(cpu))
    cpu->A = SmModReadRom16(0x949EA6 + 2u * cpu->ram[kBts + W(cpu, kCurBlock)]);
  return 0;
}

/* Samus' solid-enemy collision, LDA $0B02 (collision direction). */
uint32 SmCombatFrozenEnemyHook(CpuState *cpu, uint32 pc24) {
  if (!g.frozen || !Breaking(cpu) || W(cpu, kCollisionDir) == 3) return 0;
  uint16_t enemy = W(cpu, kCollEnemy);
  if (W(cpu, kEnemyProps + enemy) & 0x8000) return 0;     /* a platform */
  SetW(cpu, kEnemyFrozen + enemy, 0);
  return At(pc24, 0xAABF);                                 /* no solid hit */
}

/* ── item-magnet: EprojPreInstr_Pickup, X = the drop ──────────────────── */

static uint32_t ISqrt(uint32_t v) {
  uint32_t r = 0, bit = 1u << 30;
  while (bit > v) bit >>= 2;
  while (bit) {
    if (v >= r + bit) { v -= r + bit; r = (r >> 1) + bit; }
    else r >>= 1;
    bit >>= 2;
  }
  return r;
}

uint32 SmCombatMagnetHook(CpuState *cpu, uint32 pc24) {
  (void)pc24;
  if (!g.magnet || (int16_t)W(cpu, kFlare) < (int16_t)g.magnet_charge ||
      W(cpu, kGameState) != 8)
    return 0;
  uint16_t k = cpu->X;
  int32_t dx = (int16_t)(W(cpu, kSamusX) - W(cpu, kEprojX + k));
  int32_t dy = (int16_t)(W(cpu, kSamusY) - W(cpu, kEprojY + k));
  uint32_t dist = ISqrt((uint32_t)(dx * dx) + (uint32_t)(dy * dy));
  if (!dist) return 0;
  int32_t x = (int32_t)((uint32_t)W(cpu, kEprojX + k) << 16 | W(cpu, kEprojXSub + k));
  int32_t y = (int32_t)((uint32_t)W(cpu, kEprojY + k) << 16 | W(cpu, kEprojYSub + k));
  if ((uint64_t)dist << 16 <= g.magnet_step) {
    x = (int32_t)((uint32_t)W(cpu, kSamusX) << 16);
    y = (int32_t)((uint32_t)W(cpu, kSamusY) << 16);
  } else {
    x += (int32_t)((int64_t)dx * g.magnet_step / dist);
    y += (int32_t)((int64_t)dy * g.magnet_step / dist);
  }
  SetW(cpu, kEprojX + k, (uint16_t)((uint32_t)x >> 16));
  SetW(cpu, kEprojXSub + k, (uint16_t)x);
  SetW(cpu, kEprojY + k, (uint16_t)((uint32_t)y >> 16));
  SetW(cpu, kEprojYSub + k, (uint16_t)y);
  return 0;
}

/* ── bomb-limit ───────────────────────────────────────────────────────── */

/* A free slot for a bomb: 5-9 first, then a beam slot 0-4; -1 if none. */
static int FreeBombSlot(const CpuState *cpu) {
  for (int x = 10; x < 20; x += 2)
    if (!W(cpu, kProjType + (uint32_t)x)) return x;
  for (int x = 0; x < 10; x += 2)
    if (!W(cpu, kProjType + (uint32_t)x)) return x;
  return -1;
}

/* Helper2's CMP #$0005 on A = bomb count; BPL refuses the bomb. A is
 * reloaded right after, so it only steers the branch. */
uint32 SmBombLimitGateHook(CpuState *cpu, uint32 pc24) {
  (void)pc24;
  if (!g.bomb_limit) return 0;
  bool room = (int16_t)cpu->A < (int16_t)g.bombs && FreeBombSlot(cpu) >= 0;
  cpu->A = room ? 0 : 0x7FFF;
  return 0;
}

/* LDX #$000A of the bomb ($90:BFCF) and power bomb ($90:C039) slot search;
 * continues at the search's STX $14. */
uint32 SmBombLimitSlotHook(CpuState *cpu, uint32 pc24) {
  if (!g.bomb_limit) return 0;
  int slot = FreeBombSlot(cpu);
  if (slot < 0) slot = 18;                       /* stock: the last slot */
  if (slot < 10) SetW(cpu, kProjCounter, (uint16_t)(W(cpu, kProjCounter) + 1));
  cpu->X = (uint16_t)slot;
  return At(pc24, (pc24 & 0xFFFF) == 0xBFCF ? 0xBFE0 : 0xC04A);
}

/* ClearProjectile, X = the slot: a bomb leaving a beam slot gives its bomb
 * count back (stock then gives the beam count back, taken at placement). */
uint32 SmBombLimitClearHook(CpuState *cpu, uint32 pc24) {
  (void)pc24;
  if (!g.bomb_limit || cpu->X >= 10) return 0;
  uint16_t type = W(cpu, kProjType + cpu->X) & 0x0F00;
  if (type == 0x0500 || type == 0x0300) {
    uint16_t bombs = W(cpu, kBombCounter);
    SetW(cpu, kBombCounter, bombs ? (uint16_t)(bombs - 1) : 0);
  }
  return 0;
}

/* The bomb branch's STA $0CCC, A = the wait before the next bomb. */
uint32 SmBombLimitIntervalHook(CpuState *cpu, uint32 pc24) {
  (void)pc24;
  if (g.bomb_limit) cpu->A = g.bomb_interval;
  return 0;
}

/* LDA #$0005 (enemy loops, $A0:A26A / $A0:9D6B) -> start at slot 0. */
uint32 SmBombLimitScanEnemyHook(CpuState *cpu, uint32 pc24) {
  if (!g.bomb_limit) return 0;
  cpu->A = 0;
  return At(pc24, (uint16_t)((pc24 & 0xFFFF) + 3));
}

/* LDY #$000A (Eproj_CheckForBombCollisionWithRect) -> start at slot 0. */
uint32 SmBombLimitScanEprojHook(CpuState *cpu, uint32 pc24) {
  if (!g.bomb_limit) return 0;
  cpu->Y = 0;
  return At(pc24, 0xC1C2);
}

/* EnemyBombCollHandler, not a bomb type: bit 15 counts only in 5-9. */
uint32 SmBombLimitEnemyTypeHook(CpuState *cpu, uint32 pc24) {
  if (!g.bomb_limit || W(cpu, kCollEnemy) >= 5) return 0;
  return At(pc24, 0xA298);                       /* JMP next slot */
}

/* ── beam-combo-select ────────────────────────────────────────────────── */

static bool ComboSelectNow(const CpuState *cpu) {
  if (!g.combo_select) return false;
  if (!(W(cpu, kNew) & W(cpu, kCfgItemSwitch))) return false;
  if (!(W(cpu, kHeld) & W(cpu, kCfgShoot))) return false;
  uint16_t beams = W(cpu, kBeams);
  if (!(beams & 0x1000) || W(cpu, kHyperBeam)) return false;
  if ((int16_t)W(cpu, kFlare) < 60) return false;          /* full charge */
  uint16_t hud = W(cpu, kHudItem);
  if (hud != 0 && hud != 3) return false;
  if (W(cpu, kGrappleFunc) != 0xC4F0 || W(cpu, kTimeFrozen) ||
      W(cpu, kDirChangedPose))
    return false;
  uint8_t type = cpu->ram[kMoveType];
  if (type >= 28 || SmModReadRom16(0x90DD05 + 2u * type) != 0xDD3D)  /* _Normal */
    return false;
  uint16_t cost = SmModReadRom16(0x90CC21 + 2u * (beams & 0xF));      /* PBs */
  return cost && W(cpu, kPowerBombs) >= cost;
}

uint32 SmSbaSelectSwitchHook(CpuState *cpu, uint32 pc24) {
  return ComboSelectNow(cpu) ? At(pc24, 0xC519) : 0;       /* no HUD cycle */
}
uint32 SmSbaSelectFireHook(CpuState *cpu, uint32 pc24) {
  return ComboSelectNow(cpu) ? At(pc24, 0xB876) : 0;       /* JSR FireSba */
}
uint32 SmSbaSelectHudHook(CpuState *cpu, uint32 pc24) {
  return ComboSelectNow(cpu) ? At(pc24, 0xCCCA) : 0;       /* past the check */
}

/* ── activation ───────────────────────────────────────────────────────── */

#define ARM(name, ...) do {                                                  \
    static const uint32_t pcs_[] = {__VA_ARGS__};                            \
    size_t n_ = sizeof(pcs_) / sizeof(pcs_[0]);                              \
    size_t armed_ = SmModArmHooks(name, pcs_, n_);                           \
    fprintf(stderr, "[mods] %s: %zu/%zu hooks\n", name, armed_, n_);         \
  } while (0)

static void SmScrewAttackActivate(void) {
  g.shot_blocks = SmModOptionBool(PACKAGE, "screw-attack", "shot-blocks", true);
  g.frozen = SmModOptionBool(PACKAGE, "screw-attack", "frozen-enemies", true);
  g.with_speed = SmModOptionBool(PACKAGE, "screw-attack", "speed", true);
  fprintf(stderr, "[mods] screw attack: shot blocks %s, frozen enemies %s, speed %s\n",
          g.shot_blocks ? "on" : "off", g.frozen ? "on" : "off",
          g.with_speed ? "on" : "off");
  if (g.shot_blocks) ARM("screw-attack shot blocks", 0x949527, 0x94953E, 0x94933F, 0x94935E);
  if (g.frozen) ARM("screw-attack frozen enemies", 0xA0AA24);
}

static void SmItemMagnetActivate(void) {
  SmModValue charge, strength;
  SmModOption(&charge, PACKAGE, "item-magnet", "charge", "charging");
  SmModOption(&strength, PACKAGE, "item-magnet", "strength", "normal");
  g.magnet = true;
  g.magnet_charge = !strcmp(charge.text, "full") ? 60 : 1;
  g.magnet_step = !strcmp(strength.text, "weak") ? 0x10000u
                : !strcmp(strength.text, "strong") ? 0x40000u : 0x20000u;
  fprintf(stderr, "[mods] item magnet: %s, %s\n", charge.text, strength.text);
  ARM("item-magnet", 0x86EFE0);
}

static int IntOption(const char *feature, const char *option, int fallback, int lo, int hi) {
  SmModValue value;
  char text[16];
  snprintf(text, sizeof(text), "%d", fallback);
  int v;
  if (sscanf(SmModOption(&value, PACKAGE, feature, option, text), "%d", &v) != 1 ||
      v < lo || v > hi)
    v = fallback;
  return v;
}

static void SmBombLimitActivate(void) {
  g.bomb_limit = true;
  g.bombs = (uint16_t)IntOption("bomb-limit", "bombs", 8, 5, 10);
  g.bomb_interval = (uint16_t)IntOption("bomb-limit", "interval", 8, 2, 16);
  fprintf(stderr, "[mods] bomb limit: %u, %u frames apart\n", g.bombs, g.bomb_interval);
  ARM("bomb-limit", 0x90C0F3, 0x90BFCF, 0x90C039, 0x90ADB7, 0x90C017,
      0xA0A26A, 0xA0A290, 0xA09D6B, 0x86C1BF);
}

static void SmBeamComboSelectActivate(void) {
  g.combo_select = true;
  ARM("beam-combo-select", 0x90C4E0, 0x90B843, 0x90CCC0);
}

static void SmCombatReset(void) { memset(&g, 0, sizeof(g)); }

SNES_MOD_CONSTRUCTOR(sm_register_combat_plugins) {
  snes_mod_register_reset_callback(SmCombatReset);
  static const struct { const char *id; void (*activate)(void); } kPlugins[] = {
    {"super-metroid.combat.screw-attack", SmScrewAttackActivate},
    {"super-metroid.combat.item-magnet", SmItemMagnetActivate},
    {"super-metroid.combat.bomb-limit", SmBombLimitActivate},
    {"super-metroid.combat.beam-combo-select", SmBeamComboSelectActivate},
  };
  for (size_t i = 0; i < sizeof(kPlugins) / sizeof(kPlugins[0]); ++i)
    snes_mod_register_activation_plugin(kPlugins[i].id, kPlugins[i].activate);
}
