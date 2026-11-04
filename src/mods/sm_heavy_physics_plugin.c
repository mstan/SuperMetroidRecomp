/* Redux Heavy Physics v3 / Project Base, expressed at the stock consumers.
 * No cartridge data or program bytes are written. Stock water/lava physics,
 * fractional speeds, run bonuses and control flow remain in the guest. */
#include <stdbool.h>
#include <stdio.h>

#include "mod_hook.h"
#include "mod_runtime.h"
#include "sm_mod_support.h"

static void LoadA(CpuState *cpu, uint16_t value) {
  cpu->A = value;
  cpu->_flag_N = (value & 0x8000) != 0;
  cpu->_flag_Z = value == 0;
}

uint32 SmHeavyWaterAnimation(CpuState *cpu, uint32 pc24) {
  (void)pc24;
  LoadA(cpu, 2);
  return 0;
}

uint32 SmHeavyAirGravity(CpuState *cpu, uint32 pc24) {
  (void)pc24;
  LoadA(cpu, 0x3400);
  return 0;
}

uint32 SmHeavyJumpSpeed(CpuState *cpu, uint32 pc24) {
  if (cpu->X != 0) return 0; /* air/Gravity Suit only */
  uint16_t value;
  switch (pc24 & 0xFFFFu) {
    case 0x9908: value = 6; break;
    case 0x9916: value = 9; break;
    case 0x9995: value = 7; break;
    case 0x99A3: value = 8; break;
    case 0x9A6D: value = 3; break;
    default: return 0;
  }
  LoadA(cpu, value);
  return 0;
}

static unsigned AirMaxSpeed(uint16_t row) {
  /* max_speed at row+4 of the stock twelve-byte SamusSpeedTableEntry. */
  switch ((uint16_t)(row + 4)) {
    case 0x9F71: case 0x9F7D: case 0x9FA1: case 0x9FB9: case 0xA03D: return 2;
    case 0xA031: case 0xA049: return 3;
    default: return 0;
  }
}

uint32 SmHeavyMaxSpeedCompare(CpuState *cpu, uint32 pc24) {
  unsigned max = AirMaxSpeed(cpu->X);
  if (!max) return 0;
  uint16_t difference = (uint16_t)(cpu->A - max);
  cpu->_flag_N = (difference & 0x8000) != 0;
  cpu->_flag_Z = difference == 0;
  cpu->_flag_C = cpu->A >= max;
  return (pc24 & 0xFF0000u) | (uint16_t)(pc24 + 3);
}

uint32 SmHeavyMaxSpeedLoad(CpuState *cpu, uint32 pc24) {
  (void)pc24;
  unsigned max = AirMaxSpeed(cpu->X);
  if (max) LoadA(cpu, (uint16_t)max);
  return 0;
}

/* Before both reads of the normal-bomb fuse. BFFE's bomb-control hook still
 * runs afterwards, so Down+instant-bomb takes precedence over this fuse. */
uint32 SmHeavyBombFuse(CpuState *cpu, uint32 pc24) {
  LoadA(cpu, 50);
  return (pc24 & 0xFF0000u) | (uint16_t)(pc24 + 3);
}

typedef struct HeavyDemoFrame {
  uint16_t instruction, frames, held, pressed;
} HeavyDemoFrame;

/* Timed input records from the Redux specification, including the intro.
 * Keys identify existing six-byte demo records, never arbitrary ROM reads.
 * Zero-duration records retain the original demo engine's timer semantics.
 * All 25 stock demo objects were traversed to establish record boundaries. */
static const HeavyDemoFrame kDemoFrames[] = {
  {0x86D0,   3, 0x0280, 0x0000},
  {0x8AF8,   8, 0x0280, 0x0000},
  {0x8C24,   4, 0x0200, 0x0200},
  {0x8C2A,   2, 0x0200, 0x0000},
  {0x8C30,   0, 0x0000, 0x0000},
  {0x8C36,   0, 0x0000, 0x0000},
  {0x8C98,   8, 0x02C0, 0x0000},
  {0x8CF8,  32, 0x0140, 0x0000},
  {0x8ECC,   3, 0x0180, 0x0000},
  {0x8EF0,  12, 0x0280, 0x0000},
  {0x8F26,   2, 0x0180, 0x0000},
  {0x8F6A,  88, 0x0200, 0x0000},
  {0x9020,  32, 0x8200, 0x0000},
  {0x9044,  96, 0x8200, 0x0000},
  {0x905C,  24, 0x8000, 0x0000},
  {0x9068,  80, 0x8200, 0x0000},
  {0x908C,   4, 0x0180, 0x0000},
  {0x9092,   8, 0x0100, 0x0000},
  {0x90B0,   8, 0x0280, 0x0000},
  {0x90CE,   1, 0x0280, 0x0200},
  {0x90D4,  15, 0x0280, 0x0000},
  {0x9110,   1, 0x0200, 0x0000},
  {0x9116,   4, 0x0200, 0x0000},
  {0x9122,   1, 0x0200, 0x0000},
  {0x9128,  36, 0x0200, 0x0000},
  {0x91A8,   2, 0x0180, 0x0000},
  {0x91CC,   2, 0x0180, 0x0000},
  {0x91F0,   2, 0x0180, 0x0000},
  {0x91F6,  24, 0x0100, 0x0000},
  {0x920E,   6, 0x0180, 0x0000},
  {0x9244,   4, 0x0180, 0x0000},
  {0x925C,   4, 0x0080, 0x0000},
  {0x92A4,   6, 0x0080, 0x0000},
  {0x92AA,  16, 0x0000, 0x0000},
  {0x92C8,   4, 0x0080, 0x0000},
  {0x92CE,  11, 0x0000, 0x0000},
  {0x92EC,   6, 0x0080, 0x0000},
  {0x92F2,  16, 0x0000, 0x0000},
  {0x9496,  12, 0x0280, 0x0000},
  {0x949C,  13, 0x0200, 0x0000},
  {0x94BA,   4, 0x0280, 0x0000},
  {0x94C0,  32, 0x0200, 0x0000},
  {0x94CC,   4, 0x0280, 0x0000},
  {0x94D2,  18, 0x0200, 0x0000},
  {0x94DE,   2, 0x0280, 0x0000},
  {0x953A,  27, 0x8100, 0x0000},
  {0x9578,  18, 0x0180, 0x0000},
  {0x957E,  40, 0x0100, 0x0000},
  {0x96A2,  16, 0x0080, 0x0000},
  {0x97A6,  16, 0x0150, 0x0000},
  {0x97CA,  21, 0x0450, 0x0000},
  {0x982A,   8, 0x0110, 0x0000},
  {0x9878,  37, 0x0000, 0x0000},
  {0x9916,  24, 0x0200, 0x0000},
  {0x994C,  36, 0x0200, 0x0000},
  {0x999A,   6, 0x0280, 0x0000},
  {0x9A40,  17, 0x0200, 0x0000},
  {0x9A52,   3, 0x0100, 0x0000},
  {0x9B0E,   6, 0x0080, 0x0000},
  {0x9B4A,  22, 0x0000, 0x0000},
  {0x9B5C,  27, 0x0000, 0x0000},
  {0x9B6E,  28, 0x0000, 0x0000},
  {0x9B80,  28, 0x0000, 0x0000},
  {0x9B92,  29, 0x0000, 0x0000},
  {0x9BA4,  30, 0x0000, 0x0000},
  {0x9D26,  20, 0x0180, 0x0000},
  {0x9D80,  13, 0x0140, 0x0000},
};

static void SetWord(CpuState *cpu, unsigned addr, uint16_t value) {
  cpu->ram[addr] = (uint8_t)value;
  cpu->ram[addr + 1] = (uint8_t)(value >> 8);
}

uint32 SmHeavyDemoInput(CpuState *cpu, uint32 pc24) {
  unsigned lo = 0, hi = sizeof(kDemoFrames) / sizeof(kDemoFrames[0]);
  while (lo < hi) {
    unsigned mid = lo + (hi - lo) / 2;
    if (kDemoFrames[mid].instruction < cpu->Y) lo = mid + 1;
    else hi = mid;
  }
  if (lo == sizeof(kDemoFrames) / sizeof(kDemoFrames[0]) ||
      kDemoFrames[lo].instruction != cpu->Y) return 0;
  const HeavyDemoFrame *frame = &kDemoFrames[lo];
  SetWord(cpu, 0x0A7C, frame->frames);
  SetWord(cpu, 0x0A84, frame->held);
  SetWord(cpu, 0x0A86, frame->pressed);
  return (pc24 & 0xFF0000u) | 0x841Eu;
}

static void SmHeavyPhysicsActivate(void) {
  static const uint32_t hooks[] = {
    0x9080CE, 0x91FB52, 0x909C7E,
    0x909908, 0x909916, 0x909995, 0x9099A3, 0x909A6D,
    0x909A99, 0x909AA3, 0x909B3D, 0x909B47,
    0x90BFFB, 0x90C06D, 0x91840F,
  };
  size_t count = sizeof(hooks) / sizeof(hooks[0]);
  size_t armed = SmModArmHooks("heavy-physics", hooks, count);
  bool terminal = snes_mod_operand_set(0x90910F, 7);
  fprintf(stderr, "[mods] heavy physics: %zu/%zu hooks, terminal speed %s\n",
          armed, count, terminal ? "ok" : "FAILED");
}

SNES_MOD_CONSTRUCTOR(sm_register_heavy_physics_plugin) {
  snes_mod_register_activation_plugin("super-metroid.movement.heavy-physics",
                                      SmHeavyPhysicsActivate);
}
