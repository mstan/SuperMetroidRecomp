/* The resident machine shares the active game's fixed mod plan. Compose its
 * title policies at native instructions, without installing the active bus. */
#include "mod_hook.h"
#include "mod_runtime.h"
#include "sm_item_layout_plugin.h"
#include "sm_spin_hazards.h"
#include "../sm_room_runtime.h"
#include "snes/interp816.h"
void SmDifficultyResidentInstruction(Interp816 *cpu,uint8_t ram[0x20000]);
static void ResidentMods(Interp816 *c,uint8_t ram[0x20000]) {
  uint32_t pc=((uint32_t)(c->k&127)<<16)|c->pc;
  if (pc==0x0484ad) {
    CpuState cpu={0};cpu.ram=ram;cpu.A=c->a;cpu.Y=c->y;
    cpu.m_flag=c->mf;cpu.x_flag=c->xf;
    SmItemLayoutHeaderHook(&cpu,pc);c->a=cpu.A;
  } else if (pc==0x20a07a && SmCombatSpinHazardsActive() && SmSpinProtectsFireGeyser(ram)) {
    c->pc=0xa0b7;
  }
  SmDifficultyResidentInstruction(c,ram);
}
static void Reset(void) { SmRoomRuntimeSetInstructionHook(ResidentMods); }
SNES_MOD_CONSTRUCTOR(sm_register_resident_mods) { snes_mod_register_reset_callback(Reset); }
