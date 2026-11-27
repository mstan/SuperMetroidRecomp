/* Select the native Space Jump pose from the ordinary spin animation's own
 * transition script. The actual equipment bit and jump physics stay native. */
#include "mod_hook.h"
#include "mod_runtime.h"
#include "sm_mod_support.h"

uint32 SmSpinAnimationHook(CpuState *cpu,uint32 pc) {
  if (cpu->m_flag) return 0;
  unsigned pose=cpu->ram[0xa1c]|cpu->ram[0xa1d]<<8;
  /* Wall jumps own a separate animation-delay script. Its foot frames run
   * first, then this branch chooses ordinary/Space Jump/Screw Attack art.
   * The liquid and Screw Attack checks have already run at this point. */
  if ((pc & 0xffffu) == 0x844du)
    return pose == 0x83 || pose == 0x84 ? (pc & 0xff0000u) | 0x8463u : 0;
  if (pose!=0x19 && pose!=0x1a) return 0;
  /* These fallthroughs follow the native liquid, Screw Attack and actual
   * Space Jump checks. Enter only the native art/sound selection branch. */
  return (pc&0xff0000u)|((pc&0xffffu)==0xf6bfu ? 0xf6d4u : 0xf735u);
}
static void activate(void) {
  static const uint32_t pcs[]={0x91f6bf,0x91f720,0x90844d};
  SmModArmHooks("spin-jump-animation",pcs,3);
}
SNES_MOD_CONSTRUCTOR(sm_register_spin_animation) {
  snes_mod_register_activation_plugin("super-metroid.movement.spin-jump-animation",activate);
}
