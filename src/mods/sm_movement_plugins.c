/* super-metroid.movement: Samus movement options. Each feature is its own
 * switch, reimplemented on the stock routines from the behaviour of the ROM
 * hacks credited in the package manifest. Every hook reads only guest state
 * (joypad $8B held / $8F new, the button configuration words $09AA-$09BE,
 * Samus' WRAM) and the feature options, so a run is reproducible. Hooks are
 * declared in recomp/bank10.cfg ($90), bank11.cfg ($91) and bank20.cfg ($A0).
 *
 * respin: jump pressed while falling (normal fall, or a normal jump past its
 *   apex) resumes the spin jump. Samus_LookupTransitionTable ($91:81A9)
 *   selects the spin pose the way a transition-table entry would (Project
 *   Base's respin entries), and HandleJumpTransition_SpinJump ($91:FC99)
 *   skips Samus_InitJump for that transition, so vertical speed is kept and
 *   no new impulse is granted (Project Base's "spinjump restart").
 *
 * shinespark-steering: the D-pad nudges a shinespark across its path (up/down
 *   for a horizontal spark, left/right for a vertical one, up/down for a
 *   diagonal one). Every spark runs Samus_MoveHandler_Shinespark_Diag
 *   ($90:D0D7; a hook selects it at $91:FADB), whose hooks
 *   run only the movers the spark's pose needs, so an unsteered spark moves
 *   exactly as the stock vertical/horizontal handler would. A nudge goes
 *   through the stock collision-checked movers.
 *
 * landing-momentum: landing while still holding the facing direction keeps
 *   Samus' run speed. The landing transition ($91:F030) no longer clears the
 *   base speed, and the landing pose's standing handler ($90:A3D0) no longer
 *   cancels the run. A blue speed boost is kept only after a spin jump with
 *   run held (option).
 *
 * speed-ball: with the Speed Booster, the morph ball on the ground gains run
 *   speed like running (Samus_HandleExtraRunspeedX, $90:9771), optionally
 *   only while run is held.
 *
 * quick-morph: holding Down while crouched morphs after N frames, and item
 *   cancel + Down morphs at once from standing, running or crouching
 *   (Samus_LookupTransitionTable, $91:81A9). The hold counter is WRAM $0B28
 *   (unused by the game).
 *
 * space-jump: the space jump re-triggers at any fall speed (or any time),
 *   not just inside the stock $0280-$04FF (air) / $0080-$04FF (water) window
 *   checked in Samus_Movement_03_SpinJumping ($90:A488).
 *
 * bomb-control: Down held while a bomb is laid on the ground makes it explode
 *   at once (HudSelectionHandler_MorphBall, $90:BFFE), and Down held when a
 *   bomb goes off cancels the bomb jump (SamusProjectileInteractionHandler,
 *   $A0:984B).
 *
 * store-shinespark: charging a second shinespark before the first runs out
 *   stores it until used (Store Shinespark v1.01, a bonus patch of ocesse's
 *   Super Metroid Redux widescreen release). Samus_CrouchTrans
 *   ($91:F7B0) sets WRAM $0B3A, Samus_SpeedBoosterShinePals ($91:DB08) then
 *   keeps the palette state when the timer runs out, and
 *   Projectile_Func7_Shinespark ($90:D036) clears it when a spark starts.
 *
 * heavy-physics: Project Base / GBA Style heavy physics as shipped in Super
 *   Metroid Redux (Heavy Physics v3): stronger gravity, higher terminal speed
 *   (`mod_operand` $90:910F), stronger jumps, faster air control, the bomb
 *   timer retimed for bomb jumps and the intro/title demos re-timed. All
 *   implemented by sm_heavy_physics_plugin.c through hooks and mod_operand.
 */

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mod_hook.h"
#include "mod_runtime.h"
#include "sm_mod_support.h"
#include "sm_movement_plugins.h"
#include "snes_overlay_draw.h"

#define PACKAGE "super-metroid.movement"

enum {
  kHeld = 0x8B, kNew = 0x8F,
  kEquipped = 0x09A2,
  kCfgDown = 0x09AC, kCfgLeft = 0x09AE, kCfgRight = 0x09B0,
  kCfgJump = 0x09B4, kCfgRun = 0x09B6, kCfgItemCancel = 0x09B8,
  kPose = 0x0A1C, kPoseXDir = 0x0A1E, kMoveType = 0x0A1F,
  kPrevMoveType = 0x0A23, kNewPose = 0x0A28, kBombJumpDir = 0x0A56,
  kYSubSpeed = 0x0B2C, kYSpeed = 0x0B2E, kYSubAccel = 0x0B32, kYAccel = 0x0B34,
  kYDir = 0x0B36, kSpeedBoost = 0x0B3E,
  kExtraRun = 0x0B42, kExtraRunSub = 0x0B44,
  kExtraYSub = 0x0B5A, kExtraY = 0x0B5C,
  kCollision = 0x0DD0, kYAccum = 0x0DEC, kYAccumSub = 0x0DEE,
  kTransHi = 0x0DC7,
  /* WRAM the game never touches (no reference in the ROM). */
  kMorphHold = 0x0B28, kStoredSpark = 0x0B3A,
  kSteerRun = 0x0B4E, kSteerRunSub = 0x0B50, kSteerFlag = 0x0B52,
  kItemMorph = 0x0004, kItemSpaceJump = 0x0200, kItemSpeedBooster = 0x2000,
};

static uint16_t W(const CpuState *cpu, uint16_t a) {
  return (uint16_t)(cpu->ram[a] | cpu->ram[a + 1] << 8);
}
static void SetW(CpuState *cpu, uint16_t a, uint16_t v) {
  cpu->ram[a] = (uint8_t)v;
  cpu->ram[a + 1] = (uint8_t)(v >> 8);
}
static uint32_t Get32(const CpuState *cpu, uint16_t hi, uint16_t lo) {
  return (uint32_t)W(cpu, hi) << 16 | W(cpu, lo);
}
static void Set32(CpuState *cpu, uint16_t hi, uint16_t lo, uint32_t v) {
  SetW(cpu, hi, (uint16_t)(v >> 16));
  SetW(cpu, lo, (uint16_t)v);
}
static bool Held(const CpuState *cpu, uint16_t cfg) { return (W(cpu, kHeld) & W(cpu, cfg)) != 0; }
static bool Pressed(const CpuState *cpu, uint16_t cfg) { return (W(cpu, kNew) & W(cpu, cfg)) != 0; }
static uint32 At(uint32 pc24, uint16_t pc) { return (pc24 & 0xFF0000u) | pc; }
static bool FacingLeft(const CpuState *cpu) { return cpu->ram[kPoseXDir] == 4; }
static bool FacingHeld(const CpuState *cpu) {
  return Held(cpu, FacingLeft(cpu) ? kCfgLeft : kCfgRight);
}

/* Options of the enabled features; all false/zero when disabled. */
static struct {
  bool respin;
  bool steering, spark_morph, spark_cancel; uint32_t steer_step;
  bool momentum, keep_boost;
  bool speed_ball, speed_ball_hold_run;
  bool quick_morph; unsigned hold_frames; bool cancel_morph, air_morph;
  bool space_jump, space_jump_anytime;
  bool bomb_control, bomb_instant;
  bool store_spark;
} g;

static bool AirMovement(uint8_t type) {
  return type==2 || type==3 || type==6 || type==0x14 || type==0x17 || type==0x18;
}
static uint16_t HostPad(uint16_t guest) {
  uint16_t host=0;
  for (unsigned i=0;i<12;i++) if (guest&(1u<<(15-i))) host|=(uint16_t)(1u<<i);
  return host;
}
static uint16_t InputWord(const uint8_t *ram,uint16_t a) {
  return (uint16_t)(ram[a]|ram[a+1]<<8);
}
bool SmQuickMorphAirInput(uint16_t *buttons,const SnesModInputContext *ctx) {
  if (!g.quick_morph || !g.air_morph || ctx->player || !ctx->wram ||
      (*buttons&(SNES_PAD_L|SNES_PAD_DOWN))!=(SNES_PAD_L|SNES_PAD_DOWN)) return false;
  const uint8_t *ram=ctx->wram;
  if (InputWord(ram,0x998)!=8 || !(InputWord(ram,kEquipped)&kItemMorph) || !AirMovement(ram[kMoveType])) return false;
  *buttons=(uint16_t)((*buttons&~(SNES_PAD_L|SNES_PAD_DOWN)) |
                     HostPad(InputWord(ram,kCfgItemCancel)|InputWord(ram,kCfgDown)));
  return true;
}
static uint16_t QuickMorphInput(uint16_t buttons,const SnesModInputContext *ctx) {
  SmQuickMorphAirInput(&buttons,ctx); return buttons;
}

/* ── respin + quick-morph: Samus_LookupTransitionTable ($91:81A9) ──────── */

static bool IsJumpTakeoff(uint16_t pose) {
  return pose == 0x4B || pose == 0x4C || (pose >= 0x55 && pose <= 0x5A);
}

static bool RespinNow(const CpuState *cpu) {
  uint8_t type = cpu->ram[kMoveType];
  return (type == 0x02 || type == 0x06) && W(cpu, kYDir) == 2 &&
         Pressed(cpu, kCfgJump);
}

uint32 SmMoveTransitionHook(CpuState *cpu, uint32 pc24) {
  uint16_t pose = W(cpu, kPose);
  uint8_t type = cpu->ram[kMoveType];
  uint16_t new_pose = 0;
  if (g.quick_morph) {
    bool crouched = type == 0x05;
    unsigned hold = crouched && Held(cpu, kCfgDown) ? cpu->ram[kMorphHold] + 1u : 0u;
    if (hold > 0xFF) hold = 0xFF;
    SetW(cpu, kMorphHold, (uint16_t)hold);
    bool has_morph = (W(cpu, kEquipped) & kItemMorph) != 0;
    if (has_morph && g.hold_frames && hold >= g.hold_frames)
      new_pose = FacingLeft(cpu) ? 0x38 : 0x37;           /* MorphTrans */
    bool grounded = (type == 0x00 && pose && pose != 0x9B) || type == 0x01 || crouched;
    if (has_morph && g.cancel_morph && grounded && Held(cpu, kCfgItemCancel) &&
        Pressed(cpu, kCfgDown))
      new_pose = FacingLeft(cpu) ? 0x38 : 0x37;
    if (has_morph && g.air_morph && AirMovement(type) && Held(cpu,kCfgItemCancel) && Pressed(cpu,kCfgDown))
      new_pose=FacingLeft(cpu)?0x38:0x37;
    if (new_pose) SetW(cpu, kMorphHold, 0);
  }
  if (!new_pose && g.respin && RespinNow(cpu) && !IsJumpTakeoff(pose))
    new_pose = FacingLeft(cpu) ? 0x1A : 0x19;             /* SpinJump */
  if (!new_pose) return 0;
  SetW(cpu, kNewPose, new_pose);
  SetW(cpu, kBombJumpDir, 0);
  return At(pc24, 0x81E2);                                 /* CLC ; RTS */
}

/* HandleJumpTransition_SpinJump: a respin (from a fall or a normal jump,
 * descending, jump pressed this frame) keeps the current vertical speed. */
uint32 SmMoveSpinJumpInitHook(CpuState *cpu, uint32 pc24) {
  uint8_t prev = cpu->ram[kPrevMoveType];
  if (!g.respin || (prev != 0x02 && prev != 0x06) || W(cpu, kYDir) != 2 ||
      !Pressed(cpu, kCfgJump))
    return 0;
  return At(pc24, 0xFCAD);                                 /* RTS */
}

/* ── space-jump: Samus_Movement_03_SpinJumping ($90:A488) ───────────────── */

uint32 SmMoveSpaceJumpHook(CpuState *cpu, uint32 pc24) {
  if (!g.space_jump) return 0;
  if (!g.space_jump_anytime && W(cpu, kYDir) != 2) return 0;
  return At(pc24, 0xA4B1);           /* the in-window path: jump press => jump */
}

/* ── landing-momentum ─────────────────────────────────────────────────── */

/* Samus_HandleTransitionsA_5_1, normal landing: keep the base speed. */
uint32 SmMoveLandingSpeedHook(CpuState *cpu, uint32 pc24) {
  if (!g.momentum || cpu->ram[kTransHi] != 0 || !FacingHeld(cpu)) return 0;
  return At(pc24, 0xF036);           /* past STZ $0B46 / STZ $0B48 */
}

static bool IsLandingPose(uint16_t pose) {
  return (pose >= 0xA4 && pose <= 0xA7) || (pose >= 0xE0 && pose <= 0xE7);
}

/* Samus_Movement_00_Standing on the landing frame: skip the speed reset. */
uint32 SmMoveLandingStandHook(CpuState *cpu, uint32 pc24) {
  uint16_t pose = W(cpu, kPose);
  if (!g.momentum || !IsLandingPose(pose) || !FacingHeld(cpu)) return 0;
  if (W(cpu, kSpeedBoost) & 0x0400) {               /* blue: spin jump + run */
    bool spin = pose == 0xA6 || pose == 0xA7;
    if (!g.keep_boost || !spin || !Held(cpu, kCfgRun)) return 0;
  }
  return At(pc24, 0xA3E3);                          /* PLP ; RTS */
}

/* ── speed-ball: Samus_HandleExtraRunspeedX ($90:9771) ─────────────────── */

uint32 SmMoveSpeedBallHook(CpuState *cpu, uint32 pc24) {
  uint8_t type = cpu->ram[kMoveType];
  if (!g.speed_ball || (type != 0x04 && type != 0x11)) return 0;
  if (!(W(cpu, kEquipped) & kItemSpeedBooster)) return 0;
  if (g.speed_ball_hold_run && !Held(cpu, kCfgRun)) return 0;
  if (!Held(cpu, kCfgLeft) && !Held(cpu, kCfgRight)) return 0;
  return At(pc24, 0x9789);           /* the Speed Booster run-speed path */
}

/* ── bomb-control ─────────────────────────────────────────────────────── */

/* HudSelectionHandler_MorphBall, STA $0C7C,X: the new bomb's timer. */
uint32 SmMoveBombPlaceHook(CpuState *cpu, uint32 pc24) {
  (void)pc24;
  uint8_t type = cpu->ram[kMoveType];
  if (g.bomb_control && g.bomb_instant && (type == 0x04 || type == 0x11) &&
      Held(cpu, kCfgDown))
    cpu->A = 1;                      /* goes off on its first tick */
  return 0;
}

/* SamusProjectileInteractionHandler, STA $0A56: the bomb jump. */
uint32 SmMoveBombJumpHook(CpuState *cpu, uint32 pc24) {
  if (!g.bomb_control || !Held(cpu, kCfgDown)) return 0;
  return At(pc24, 0x984E);
}

/* ── store-shinespark ─────────────────────────────────────────────────── */

uint32 SmMoveStoreSparkChargeHook(CpuState *cpu, uint32 pc24) {
  (void)pc24;
  if (g.store_spark && (W(cpu, kSpeedBoost) & 0xFF00) >= 0x0400 &&
      (int16_t)W(cpu, 0x0A68) > 0)  /* recharged while a charge is running */
    SetW(cpu, kStoredSpark, 1);
  return 0;
}

uint32 SmMoveStoreSparkKeepHook(CpuState *cpu, uint32 pc24) {
  if (!g.store_spark || !W(cpu, kStoredSpark)) return 0;
  return At(pc24, 0xDB0E);           /* timer stays non-zero: spark stored */
}

uint32 SmMoveStoreSparkUsedHook(CpuState *cpu, uint32 pc24) {
  (void)pc24;
  if (g.store_spark) SetW(cpu, kStoredSpark, 0);
  return 0;
}

/* ── shinespark-steering: Samus_MoveHandler_Shinespark_Diag ($90:D0D7) ──── */

enum { kSparkHoriz, kSparkVert, kSparkDiag, kSparkNone };
static int SparkKind(const CpuState *cpu) {
  switch (W(cpu, kPose)) {
    case 0xC9: case 0xCA: return kSparkHoriz;
    case 0xCB: case 0xCC: return kSparkVert;
    case 0xCD: case 0xCE: return kSparkDiag;
    default: return kSparkNone;
  }
}

/* The native crash finish queues SFX3 $10 to stop the looping spark sound.
 * Use the same Max6 queue rules without running its crash/echo animation. */
static void StopSparkSound(CpuState *cpu) {
  uint8_t *r=cpu->ram; r[0x655]=6;
  uint8_t read=r[0x645],write=r[0x648];
  if (((write-read)&15)>=6 || W(cpu,0x5f5) || W(cpu,0x998)>=0x28 || (W(cpu,0x592)&0x8000)) return;
  uint8_t next=(uint8_t)((write+1)&15);
  if (next==read) { if (0x10<r[0x676+write]) r[0x676+write]=0x10; }
  else { r[0x676+write]=0x10; r[0x648]=next; r[0x676+next]=0; }
}
uint32 SmMoveSparkCancelHook(CpuState *cpu,uint32 pc24) {
  if (!g.steering || SparkKind(cpu)==kSparkNone) return 0;
  bool morph=g.spark_morph && (W(cpu,kEquipped)&kItemMorph) && Pressed(cpu,kCfgDown);
  if (!morph && !(g.spark_cancel && Pressed(cpu,kCfgJump))) return 0;
  /* C2 commits the requested pose, aligns it and restores normal movement
   * and input. C1 deliberately skips committing the pose (knockback path). */
  uint16_t pose=morph ? ((W(cpu,kEquipped)&2)?0x7d:0x31) : 0x29;
  SetW(cpu,0xa2c,(uint16_t)(pose+(FacingLeft(cpu)?1:0)));
  SetW(cpu,0xa32,2); SetW(cpu,0xa5c,0xeb52);
  SetW(cpu,kYSpeed,0); SetW(cpu,kYSubSpeed,0); SetW(cpu,kYDir,2);
  SetW(cpu,kExtraRun,0); SetW(cpu,kExtraRunSub,0);
  SetW(cpu,0xb46,0); SetW(cpu,0xb48,0); SetW(cpu,kSpeedBoost,0);
  SetW(cpu,0xa68,1); SetW(cpu,0xa6e,0); SetW(cpu,0xa48,0);
  SetW(cpu,kSteerFlag,0); SetW(cpu,kStoredSpark,0);
  SetW(cpu,kExtraY,0); SetW(cpu,kExtraYSub,0); SetW(cpu,kYAccum,0); SetW(cpu,kYAccumSub,0);
  memset(cpu->ram+0xaae,0,0x1a); StopSparkSound(cpu);
  uint16_t entry=(uint16_t)pc24;
  return At(pc24,entry==0xd0ab?0xd0d6:entry==0xd0d7?0xd105:0xd131);
}
/* -1 up/left, +1 down/right, 0 none (both or neither held). */
static int SteerAxis(const CpuState *cpu, uint16_t neg, uint16_t pos) {
  return (Held(cpu, pos) ? 1 : 0) - (Held(cpu, neg) ? 1 : 0);
}
static int SteerY(const CpuState *cpu) { return SteerAxis(cpu, 0x09AA, kCfgDown); }
static int SteerX(const CpuState *cpu) { return SteerAxis(cpu, kCfgLeft, kCfgRight); }

/* Speed echoes: the horizontal handler passes X = 8, the others 4. */
uint32 SmMoveSparkEchoHook(CpuState *cpu, uint32 pc24) {
  (void)pc24;
  if (g.steering && SparkKind(cpu) == kSparkHoriz) cpu->X = 8;
  return 0;
}

/* JSR Samus_ShinesparkMove_X. Vertical sparks skip it unless steered; a
 * steer runs it at the steering speed, facing the steered way. */
uint32 SmMoveSparkXHook(CpuState *cpu, uint32 pc24) {
  if (!g.steering || SparkKind(cpu) != kSparkVert) return 0;
  int dir = SteerX(cpu);
  if (!dir) return At(pc24, 0xD0EF);
  SetW(cpu, kSteerRun, W(cpu, kExtraRun));
  SetW(cpu, kSteerRunSub, W(cpu, kExtraRunSub));
  cpu->ram[kPoseXDir] = dir < 0 ? 4 : 8;
  /* The mover adds the spark acceleration first. */
  Set32(cpu, kExtraRun, kExtraRunSub, g.steer_step - Get32(cpu, kYAccel, kYSubAccel));
  return 0;
}

/* JSR Samus_ShinesparkMove_Y. Horizontal sparks skip it unless steered; a
 * steer runs it with no spark speed and the nudge as extra displacement
 * (the mover moves up by speed - displacement). */
uint32 SmMoveSparkYHook(CpuState *cpu, uint32 pc24) {
  if (!g.steering) return 0;
  int kind = SparkKind(cpu);
  if (kind == kSparkVert && SteerX(cpu)) {           /* undo the X steer */
    cpu->ram[kPoseXDir] = W(cpu, kPose) == 0xCC ? 4 : 8;
    SetW(cpu, kExtraRun, W(cpu, kSteerRun));
    SetW(cpu, kExtraRunSub, W(cpu, kSteerRunSub));
    SetW(cpu, kSteerRun, 0);
    SetW(cpu, kSteerRunSub, 0);
    return 0;
  }
  int dir = SteerY(cpu);
  if (kind == kSparkHoriz) {
    SetW(cpu, kSteerFlag, W(cpu, kCollision));       /* Move_X's result */
    if (!dir) return At(pc24, 0xD0F2);
    Set32(cpu, kYSpeed, kYSubSpeed, 0);
    Set32(cpu, kYAccum, kYAccumSub, 0u - Get32(cpu, kYAccel, kYSubAccel));
  }
  if ((kind == kSparkHoriz || kind == kSparkDiag) && dir)
    Set32(cpu, kExtraY, kExtraYSub, dir < 0 ? 0u - g.steer_step : g.steer_step);
  return 0;
}

/* JSR Samus_EndSuperJump. A horizontal spark ends on Move_X's collision only:
 * a nudge into the floor or ceiling just stops there. */
uint32 SmMoveSparkEndHook(CpuState *cpu, uint32 pc24) {
  (void)pc24;
  if (!g.steering) return 0;
  int kind = SparkKind(cpu);
  bool steered = SteerY(cpu) != 0;
  if (kind == kSparkHoriz) {
    SetW(cpu, kCollision, W(cpu, kSteerFlag));
    SetW(cpu, kSteerFlag, 0);
    if (steered) {
      Set32(cpu, kYSpeed, kYSubSpeed, 0);
      Set32(cpu, kYAccum, kYAccumSub, 0);
    }
  }
  if ((kind == kSparkHoriz || kind == kSparkDiag) && steered)
    Set32(cpu, kExtraY, kExtraYSub, 0);
  return 0;
}

/* ── activation ───────────────────────────────────────────────────────── */

static long IntOption(const char *feature, const char *option, long fallback, long lo, long hi) {
  SmModValue value;
  char fb[16];
  snprintf(fb, sizeof(fb), "%ld", fallback);
  char *end;
  long v = strtol(SmModOption(&value, PACKAGE, feature, option, fb), &end, 10);
  if (*end || v < lo || v > hi) v = fallback;
  return v;
}

#define ARM(name, ...) do {                                                  \
    static const uint32_t pcs_[] = {__VA_ARGS__};                            \
    size_t n_ = sizeof(pcs_) / sizeof(pcs_[0]);                              \
    size_t armed_ = SmModArmHooks(name, pcs_, n_);                           \
    fprintf(stderr, "[mods] %s: %zu/%zu hooks\n", name, armed_, n_);         \
  } while (0)

static void SmRespinActivate(void) {
  g.respin = true;
  ARM("respin", 0x9181A9, 0x91FC99);
}

/* Select the diagonal dispatcher when the stock handler pointer is stored.
 * The existing movers below preserve straight sparks without a nudge. */
uint32 SmMoveSparkHandlerHook(CpuState *cpu, uint32 pc24) {
  (void)pc24;
  cpu->A = 0xD0D7;
  cpu->_flag_N = 1;
  cpu->_flag_Z = 0;
  return 0;
}

static void SmShinesparkSteeringActivate(void) {
  SmModValue strength_value;
  const char *strength = SmModOption(&strength_value, PACKAGE, "shinespark-steering",
                                     "strength", "normal");
  g.steering = true;
  g.spark_morph=SmModOptionBool(PACKAGE,"shinespark-steering","down-to-morph",false);
  g.spark_cancel=SmModOptionBool(PACKAGE,"shinespark-steering","jump-to-cancel",false);
  g.steer_step = !strcmp(strength, "weak") ? 0x8000u : !strcmp(strength, "strong") ? 0x20000u : 0x10000u;
  fprintf(stderr, "[mods] shinespark steering %s\n", strength);
  ARM("shinespark-steering", 0x91FADB, 0x90D0E6, 0x90D0EC, 0x90D0EF, 0x90D0F2);
  if (g.spark_morph || g.spark_cancel) ARM("shinespark-extras",0x90D0AB,0x90D0D7,0x90D106);
}

static void SmLandingMomentumActivate(void) {
  g.momentum = true;
  g.keep_boost = SmModOptionBool(PACKAGE, "landing-momentum", "speed-booster", true);
  ARM("landing-momentum", 0x91F030, 0x90A3D0);
}

static void SmSpeedBallActivate(void) {
  g.speed_ball = true;
  g.speed_ball_hold_run = SmModOptionBool(PACKAGE, "speed-ball", "hold-run", true);
  ARM("speed-ball", 0x909771);
}

static void SmQuickMorphActivate(void) {
  g.quick_morph = true;
  g.hold_frames = (unsigned)IntOption("quick-morph", "hold-frames", 12, 0, 60);
  g.cancel_morph = SmModOptionBool(PACKAGE, "quick-morph", "item-cancel", true);
  g.air_morph=SmModOptionBool(PACKAGE,"quick-morph","air-morph",false);
  /* The shoulder plugin consumes the raw chord itself when enabled, so the
   * physical L gesture also works if item cancel was rebound to a shoulder. */
  if (g.air_morph && !snes_mod_runtime_feature_enabled_c("super-metroid.controls","zero-mission"))
    snes_mod_register_input_transform(QuickMorphInput);
  fprintf(stderr, "[mods] quick morph: hold %u frames, item cancel %s\n",
          g.hold_frames, g.cancel_morph ? "on" : "off");
  ARM("quick-morph", 0x9181A9);
}

static void SmSpaceJumpActivate(void) {
  SmModValue window_value;
  const char *window = SmModOption(&window_value, PACKAGE, "space-jump", "window", "falling");
  g.space_jump = true;
  g.space_jump_anytime = !strcmp(window, "anytime");
  fprintf(stderr, "[mods] space jump window: %s\n", window);
  ARM("space-jump", 0x90A488);
}

static void SmBombControlActivate(void) {
  SmModValue mode_value;
  const char *mode = SmModOption(&mode_value, PACKAGE, "bomb-control", "mode", "instant");
  g.bomb_control = true;
  g.bomb_instant = strcmp(mode, "no-jump") != 0;
  fprintf(stderr, "[mods] bomb control: %s\n", mode);
  ARM("bomb-control", 0x90BFFE, 0xA0984B);
}

static void SmStoreShinesparkActivate(void) {
  g.store_spark = true;
  ARM("store-shinespark", 0x91F7B0, 0x91DB08, 0x90D036);
}

static void SmMovementReset(void) { memset(&g, 0, sizeof(g)); }

SNES_MOD_CONSTRUCTOR(sm_register_movement_plugins) {
  snes_mod_register_reset_callback(SmMovementReset);
  static const struct { const char *id; void (*activate)(void); } kPlugins[] = {
    {"super-metroid.movement.respin", SmRespinActivate},
    {"super-metroid.movement.shinespark-steering", SmShinesparkSteeringActivate},
    {"super-metroid.movement.landing-momentum", SmLandingMomentumActivate},
    {"super-metroid.movement.speed-ball", SmSpeedBallActivate},
    {"super-metroid.movement.quick-morph", SmQuickMorphActivate},
    {"super-metroid.movement.space-jump", SmSpaceJumpActivate},
    {"super-metroid.movement.bomb-control", SmBombControlActivate},
    {"super-metroid.movement.store-shinespark", SmStoreShinesparkActivate},
  };
  for (size_t i = 0; i < sizeof(kPlugins) / sizeof(kPlugins[0]); ++i)
    snes_mod_register_activation_plugin(kPlugins[i].id, kPlugins[i].activate);
}
