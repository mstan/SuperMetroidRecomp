/* Swap the two native item PLMs without changing their location/save bits.
 * A partial file identifies its layout through location flags and collected
 * abilities, so an existing stock file keeps its missing item and a started
 * swap remains coherent after the elective feature is switched off. */
#include "mod_hook.h"
#include "mod_runtime.h"
#include "sm_mod_support.h"
#include "sm_item_layout_plugin.h"

static bool swap_enabled;
static unsigned Word(const CpuState *cpu, unsigned at) {
  return cpu->ram[at] | cpu->ram[at + 1] << 8;
}

/* SpawnRoomPLM, immediately before storing the loaded header through Y.
 * X still names the original 8F room-list entry. The native routine retains
 * the room argument, sprite/script setup, pickup, sound and save handlers. */
uint32 SmItemLayoutHeaderHook(CpuState *cpu, uint32 pc) {
  (void)pc;
  unsigned slot = cpu->Y, header = cpu->A;
  if (cpu->m_flag || cpu->x_flag || slot >= 0x50 || (slot & 1)) return 0;
  bool spring = header == 0xef03 || header == 0xef57 || header == 0xefab;
  bool xray = header == 0xef0f || header == 0xef63 || header == 0xefb7;
  if (!spring && !xray) return 0;
  unsigned argument = Word(cpu, 0x1dc7 + slot);
  if (argument != (spring ? 0x96u : 0x26u)) return 0;

  /* Verified JU room entries: 8F:C6E5 Spring Ball (flag96), 8F:8876 X-ray
   * (flag26). Native item_bit_array starts at 7E:D870. */
  bool spring_location = (cpu->ram[0xd882] & 0x40) != 0;
  bool xray_location = (cpu->ram[0xd874] & 0x40) != 0;
  bool has_spring = (Word(cpu, 0x9a4) & 2) != 0;
  bool has_xray = (Word(cpu, 0x9a4) & 0x8000) != 0;
  bool swapped_partial = (spring_location && !xray_location && has_xray && !has_spring) ||
      (xray_location && !spring_location && has_spring && !has_xray);
  bool stock_partial = (spring_location && !xray_location && has_spring && !has_xray) ||
      (xray_location && !spring_location && has_xray && !has_spring);
  if ((!swap_enabled && !swapped_partial) || stock_partial) return 0;
  cpu->A = (uint16_t)(spring ? header + 12 : header - 12);
  return 0;
}

void SmItemLayoutReset(void) {
  swap_enabled = false;
  static const uint32_t hooks[] = {0x8484ad};
  SmModArmHooks("item-layout save continuity", hooks, 1);
}
static void activate(void) { swap_enabled = true; }
SNES_MOD_CONSTRUCTOR(sm_register_item_layout) {
  snes_mod_register_activation_plugin("super-metroid.balance-fixes.swap-spring-xray", activate);
}
