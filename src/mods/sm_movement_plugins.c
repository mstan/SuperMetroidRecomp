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
 *   ($90:D0D7; the handler table at $91:FAFC is pointed at it), whose hooks
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
 *   (`live_operand` $90:910F), stronger jumps, faster air control, the bomb
 *   timer retimed for bomb jumps and the intro/title demos re-timed. All
 *   data and immediates, applied as guarded ROM patches.
 */

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mod_hook.h"
#include "mod_runtime.h"
#include "sm_mod_support.h"

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
  bool steering; uint32_t steer_step;
  bool momentum, keep_boost;
  bool speed_ball, speed_ball_hold_run;
  bool quick_morph; unsigned hold_frames; bool cancel_morph;
  bool space_jump, space_jump_anytime;
  bool bomb_control, bomb_instant;
  bool store_spark;
} g;

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

static void SmShinesparkSteeringActivate(void) {
  SmModValue strength_value;
  const char *strength = SmModOption(&strength_value, PACKAGE, "shinespark-steering",
                                     "strength", "normal");
  g.steering = true;
  g.steer_step = !strcmp(strength, "weak") ? 0x8000u : !strcmp(strength, "strong") ? 0x20000u : 0x10000u;
  /* kSamusFunc_F468_Shinespark: horizontal, horizontal, vertical, vertical
   * -> the diagonal handler (the diagonal entries already are). */
  static const uint8_t stock[8] = {0x06, 0xD1, 0x06, 0xD1, 0xAB, 0xD0, 0xAB, 0xD0};
  static const uint8_t diag[8] = {0xD7, 0xD0, 0xD7, 0xD0, 0xD7, 0xD0, 0xD7, 0xD0};
  bool table = SmModRomPatch("shinespark-steering", 0x91FAFC, stock, diag, sizeof(diag));
  fprintf(stderr, "[mods] shinespark steering %s: handler table %s\n", strength,
          table ? "ok" : "refused");
  if (table) ARM("shinespark-steering", 0x90D0E6, 0x90D0EC, 0x90D0EF, 0x90D0F2);
  else g.steering = false;
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

/* Heavy Physics v3 (Super Metroid Redux, from Project Base / GBA Style):
 * every changed run of bytes against the stock ROM. */
typedef struct { uint32_t addr; uint8_t size; uint8_t stock[10]; uint8_t value[10]; } DataPatch;
static const DataPatch kHeavyPhysics[] = {
  /* Samus_MoveY_WithSpeedCalc: CMP #$0005 terminal fall speed -> 7 */
  {0x909110, 1, {0x05}, {0x07}},
  /* kSamusPhys_AnimDelayInWater 3 -> 2 */
  {0x909E93, 1, {0x03}, {0x02}},
  /* gravity in air $1C00 -> $3400 (Samus_DetermineAccel_Y) */
  {0x909EA2, 1, {0x1C}, {0x34}},
  /* jump / hi-jump / wall jump / hi-jump wall jump / bomb jump speeds in air */
  {0x909EB9, 1, {0x04}, {0x06}},
  {0x909EC5, 1, {0x06}, {0x09}},
  {0x909ED1, 1, {0x04}, {0x07}},
  {0x909EDD, 1, {0x05}, {0x08}},
  {0x909EF5, 1, {0x02}, {0x03}},
  /* airborne max X speeds (normal/spin jump, falling, morph falling, spring
   * ball in air/falling, wall jump) */
  {0x909F71, 1, {0x01}, {0x02}},
  {0x909F7D, 1, {0x01}, {0x02}},
  {0x909FA1, 1, {0x01}, {0x02}},
  {0x909FB9, 1, {0x01}, {0x02}},
  {0x90A031, 1, {0x01}, {0x03}},
  {0x90A03D, 1, {0x01}, {0x02}},
  {0x90A049, 1, {0x01}, {0x03}},
  /* bomb timer $3C -> $32 so bomb jumps stay consistent ("Bombs fix") */
  {0x90BF9B, 1, {0x3C}, {0x32}},
  /* intro (Mother Brain) demo: left input timer */
  {0x9186D0, 1, {0x07}, {0x03}},
  /* title screen demo inputs */
  {0x918AF8, 1, {0x28}, {0x08}},
  {0x918C29, 5, {0x00, 0x14, 0x00, 0x00, 0x01}, {0x02, 0x02, 0x00, 0x00, 0x02}},
  {0x918C30, 10, {0x01, 0x00, 0x40, 0x01, 0x40, 0x00, 0x03, 0x00, 0x40, 0x01},
                 {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}},
  {0x918C98, 1, {0x14}, {0x08}},
  {0x918CF8, 1, {0x3A}, {0x20}},
  {0x918ECC, 1, {0x06}, {0x03}},
  {0x918EF0, 1, {0x14}, {0x0C}},
  {0x918F26, 1, {0x0A}, {0x02}},
  {0x918F6A, 1, {0x66}, {0x58}},
  {0x919020, 1, {0x30}, {0x20}},
  {0x919044, 1, {0x91}, {0x60}},
  {0x91905C, 1, {0x36}, {0x18}},
  {0x919068, 1, {0x90}, {0x50}},
  {0x91908C, 7, {0x0E, 0x00, 0x80, 0x01, 0x00, 0x00, 0x0F}, {0x04, 0x00, 0x80, 0x01, 0x00, 0x00, 0x08}},
  {0x9190B0, 1, {0x0C}, {0x08}},
  {0x9190D1, 7, {0x01, 0x00, 0x01, 0x0F, 0x00, 0x80, 0x01}, {0x02, 0x00, 0x02, 0x0F, 0x00, 0x80, 0x02}},
  {0x919112, 7, {0x80, 0x02, 0x80, 0x00, 0x04, 0x00, 0x80}, {0x00, 0x02, 0x00, 0x00, 0x04, 0x00, 0x00}},
  {0x919125, 7, {0x82, 0x00, 0x80, 0x24, 0x00, 0x00, 0x82}, {0x02, 0x00, 0x00, 0x24, 0x00, 0x00, 0x02}},
  {0x9191A8, 1, {0x06}, {0x02}},
  {0x9191CC, 1, {0x06}, {0x02}},
  {0x9191F0, 7, {0x08, 0x00, 0x80, 0x01, 0x00, 0x00, 0x1C}, {0x02, 0x00, 0x80, 0x01, 0x00, 0x00, 0x18}},
  {0x91920E, 1, {0x0B}, {0x06}},
  {0x919244, 1, {0x08}, {0x04}},
  {0x91925C, 1, {0x06}, {0x04}},
  {0x9192A4, 7, {0x07, 0x00, 0x80, 0x00, 0x00, 0x00, 0x11}, {0x06, 0x00, 0x80, 0x00, 0x00, 0x00, 0x10}},
  {0x9192C8, 7, {0x0B, 0x00, 0x80, 0x00, 0x00, 0x00, 0x1E}, {0x04, 0x00, 0x80, 0x00, 0x00, 0x00, 0x0B}},
  {0x9192EC, 7, {0x05, 0x00, 0x80, 0x00, 0x00, 0x00, 0x0D}, {0x06, 0x00, 0x80, 0x00, 0x00, 0x00, 0x10}},
  {0x919496, 7, {0x13, 0x00, 0x80, 0x02, 0x00, 0x00, 0x13}, {0x0C, 0x00, 0x80, 0x02, 0x00, 0x00, 0x0D}},
  {0x9194BA, 7, {0x15, 0x00, 0x80, 0x02, 0x00, 0x00, 0x25}, {0x04, 0x00, 0x80, 0x02, 0x00, 0x00, 0x20}},
  {0x9194CC, 7, {0x0E, 0x00, 0x80, 0x02, 0x00, 0x00, 0x21}, {0x04, 0x00, 0x80, 0x02, 0x00, 0x00, 0x12}},
  {0x9194DE, 1, {0x09}, {0x02}},
  {0x91953A, 1, {0x2B}, {0x1B}},
  {0x919578, 7, {0x16, 0x00, 0x80, 0x01, 0x00, 0x00, 0x41}, {0x12, 0x00, 0x80, 0x01, 0x00, 0x00, 0x28}},
  {0x9196A2, 1, {0x15}, {0x10}},
  {0x9197A6, 1, {0x12}, {0x10}},
  {0x9197CA, 1, {0x16}, {0x15}},
  {0x91982A, 1, {0x0A}, {0x08}},
  {0x919878, 1, {0x20}, {0x25}},
  {0x919916, 1, {0x1D}, {0x18}},
  {0x91994C, 1, {0x2B}, {0x24}},
  {0x91999A, 1, {0x10}, {0x06}},
  {0x919A40, 1, {0x14}, {0x11}},
  {0x919A52, 1, {0x06}, {0x03}},
  {0x919B0E, 1, {0x0B}, {0x06}},
  {0x919B4A, 1, {0x20}, {0x16}},
  {0x919B5C, 1, {0x25}, {0x1B}},
  {0x919B6E, 1, {0x26}, {0x1C}},
  {0x919B80, 1, {0x26}, {0x1C}},
  {0x919B92, 1, {0x27}, {0x1D}},
  {0x919BA4, 1, {0x28}, {0x1E}},
  {0x919D26, 1, {0x23}, {0x14}},
  {0x919D80, 1, {0x14}, {0x0D}},
};

/* The guarded-patch registry holds 64 patches of up to 256 bytes, shared by
 * every enabled mod, so nearby runs are applied as one patch per window.
 * Each run's bytes are still guarded against their stock values; the bytes
 * between runs are expected and kept as they are. Returns the patches
 * applied and sets *windows to the number attempted. */
enum { kWindowMax = 256 };
static size_t ApplyDataPatches(const char *name, const DataPatch *p, size_t n,
                               size_t *windows) {
  size_t applied = 0;
  *windows = 0;
  for (size_t i = 0; i < n;) {
    uint32_t start = p[i].addr;
    size_t j = i;
    while (j + 1 < n && (p[j + 1].addr >> 16) == (start >> 16) &&
           p[j + 1].addr + p[j + 1].size - start <= kWindowMax)
      ++j;
    size_t size = p[j].addr + p[j].size - start;
    uint8_t stock[kWindowMax], value[kWindowMax];
    for (size_t k = 0; k < size; ++k)
      stock[k] = value[k] = (uint8_t)SmModRomWord(start + (uint32_t)k);
    for (size_t r = i; r <= j; ++r) {
      memcpy(stock + (p[r].addr - start), p[r].stock, p[r].size);
      memcpy(value + (p[r].addr - start), p[r].value, p[r].size);
    }
    applied += SmModRomPatch(name, start, stock, value, size);
    ++*windows;
    i = j + 1;
  }
  return applied;
}

static void SmHeavyPhysicsActivate(void) {
  size_t windows;
  size_t applied = ApplyDataPatches("heavy-physics", kHeavyPhysics,
                                    sizeof(kHeavyPhysics) / sizeof(kHeavyPhysics[0]), &windows);
  fprintf(stderr, "[mods] heavy physics: %zu/%zu patches\n", applied, windows);
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
    {"super-metroid.movement.heavy-physics", SmHeavyPhysicsActivate},
  };
  for (size_t i = 0; i < sizeof(kPlugins) / sizeof(kPlugins[0]); ++i)
    snes_mod_register_activation_plugin(kPlugins[i].id, kPlugins[i].activate);
}
