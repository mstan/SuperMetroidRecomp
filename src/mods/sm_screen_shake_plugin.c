/* super-metroid.pacing / screen-shake: reduce earthquake shaking.
 *
 * Earthquakes (earthquake_type $183E, earthquake_timer $1840) offset the
 * screen in two stock tables, indexed by type:
 *   HandleRoomShaking ($A0:8687) adds kRoomShakes ($A0:872D, 36 x 4 words:
 *     BG1 X/Y, BG2 X/Y) to the BG scroll registers, flipping sign every
 *     other frame;
 *   GetValuesForScreenShaking ($86:8427) reads kScreenShakeOffsets
 *     ($86:846B, 36 x 2 words) so enemy projectiles drawn on the background
 *     move with it.
 * Both tables hold 0..3 px. Hooks supply mod-owned offsets: "half" uses
 * ceil(v / 2) (3 -> 2, 2 -> 1, 1 -> 1); "off" uses zero and also raises the
 * CMP #$0012 at $A0:8705 to #$7FFF, so strong quakes (type >= $12) no longer
 * jitter every enemy sprite 1 px through SetAllEnemiesToShakeFor2Frames
 * (`mod_operand` in recomp/bank20.cfg). Timers and types are untouched, so
 * every quake still lasts and triggers exactly what it does in the original;
 * only what is drawn changes. Cartridge data is never changed.
 */

#include <stdio.h>
#include <string.h>

#include "mod_runtime.h"
#include "mod_hook.h"
#include "sm_mod_support.h"

#define PACKAGE "super-metroid.pacing"
#define FEATURE "screen-shake"

static const uint8_t kRoomShakesStock[144] = {
  1, 0, 0, 0, 0, 1, 0, 0, 1, 1, 0, 0, 2, 0, 0, 0,
  0, 2, 0, 0, 2, 2, 0, 0, 3, 0, 0, 0, 0, 3, 0, 0,
  3, 3, 0, 0, 1, 0, 1, 0, 0, 1, 0, 1, 1, 1, 1, 1,
  2, 0, 2, 0, 0, 2, 0, 2, 2, 2, 2, 2, 3, 0, 3, 0,
  0, 3, 0, 3, 3, 3, 3, 3, 1, 0, 1, 0, 0, 1, 0, 1,
  1, 1, 1, 1, 2, 0, 2, 0, 0, 2, 0, 2, 2, 2, 2, 2,
  3, 0, 3, 0, 0, 3, 0, 3, 3, 3, 3, 3, 0, 0, 1, 0,
  0, 0, 0, 1, 0, 0, 1, 1, 0, 0, 2, 0, 0, 0, 0, 2,
  0, 0, 2, 2, 0, 0, 3, 0, 0, 0, 0, 3, 0, 0, 3, 3,
};
static const uint8_t kEprojShakesStock[72] = {
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 1, 0, 0, 1, 1, 1, 2, 0, 0, 2, 2, 2,
  3, 0, 0, 3, 3, 3, 1, 0, 0, 1, 1, 1, 2, 0, 0, 2,
  2, 2, 3, 0, 0, 3, 3, 3,
};

static bool g_off;
static uint16_t Word(const CpuState *cpu, unsigned a) {
  return (uint16_t)(cpu->ram[a] | cpu->ram[a + 1] << 8);
}
static void SetWord(CpuState *cpu, unsigned a, uint16_t value) {
  cpu->ram[a] = (uint8_t)value;
  cpu->ram[a + 1] = (uint8_t)(value >> 8);
}
static int Offset(unsigned stock, bool negative) {
  int value = g_off ? 0 : (int)((stock + 1) / 2);
  return negative ? -value : value;
}

/* The stock guards and TAX have already run. Leave its timer, enemy-shake
 * threshold, frame counter and register epilogue to the original program. */
uint32 SmShakeRoomOffsets(CpuState *cpu, uint32 pc24) {
  unsigned row = cpu->X / 2;
  if (row + 3 >= sizeof(kRoomShakesStock)) return 0;
  bool negative = (Word(cpu, 0x1840) & 2) != 0;
  for (unsigned i = 0; i < 4; ++i) {
    unsigned addr = (uint16_t)(cpu->D + 0xB1 + i * 2);
    uint16_t base = Word(cpu, addr);
    uint16_t offset = (uint16_t)Offset(kRoomShakesStock[row + i], negative);
    uint16_t result = (uint16_t)(base + offset);
    SetWord(cpu, addr, result);
    /* The final ADC's V survives the following DEC/LDA/CMP. Those stock
     * instructions replace A/N/Z/C, so only V must be carried past the hook. */
    cpu->_flag_V = ((~(base ^ offset) & (base ^ result)) & 0x8000) != 0;
  }
  return (pc24 & 0xFF0000u) | 0x86FFu;
}

uint32 SmShakeProjectileOffsets(CpuState *cpu, uint32 pc24) {
  unsigned row = cpu->X / 2;
  if (row + 1 >= sizeof(kEprojShakesStock)) return 0;
  bool negative = (Word(cpu, 0x1840) & 2) != 0;
  SetWord(cpu, (uint16_t)(cpu->D + 0x24),
          (uint16_t)Offset(kEprojShakesStock[row], negative));
  cpu->A = (uint16_t)Offset(kEprojShakesStock[row + 1], negative);
  SetWord(cpu, (uint16_t)(cpu->D + 0x22), cpu->A);
  cpu->_flag_N = (cpu->A & 0x8000) != 0;
  cpu->_flag_Z = cpu->A == 0;
  return (pc24 & 0xFF0000u) | 0x8465u;
}

static void SmScreenShakeActivate(void) {
  SmModValue level_value;
  const char *level = SmModOption(&level_value, PACKAGE, FEATURE, "shake", "half");
  g_off = !strcmp(level, "off");
  static const uint32_t hooks[] = {0xA086A5, 0x86843C};
  size_t armed = SmModArmHooks("screen-shake", hooks, 2);
  bool threshold = !g_off || snes_mod_operand_set(0xA08705, 0x7FFF);
  fprintf(stderr, "[mods] screen shake %s: %zu/2 hooks, threshold %s\n",
          level, armed, threshold ? "ok" : "FAILED");
}

SNES_MOD_CONSTRUCTOR(sm_register_screen_shake_plugin) {
  snes_mod_register_activation_plugin("super-metroid.pacing.screen-shake",
                                      SmScreenShakeActivate);
}
