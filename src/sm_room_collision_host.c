#include "sm_room_collision.h"
#include "sm_video.h"
#include "mod_hook.h"
#include <stdio.h>
/* The body dispatch PCs are shared with the elective screw-attack block
 * behavior. That callback applies this core room query before its own policy. */
extern bool SmCombatShotBlockDispatchActive(void);
bool SmRoomCollisionFrameBegin(void) {
  bool enabled=g_sm_video.enhanced && g_sm_video.seamless;
  const uint32_t body[]={0x949527,0x94953e},inside[]={0x949b9e,0x949bd3,0x949c18};
  for (unsigned i=0; i<2; ++i)
    if (!snes_mod_hook_arm(body[i],enabled || SmCombatShotBlockDispatchActive())) return false;
  for (unsigned i=0; i<3; ++i) if (!snes_mod_hook_arm(inside[i],enabled)) {
    if (enabled) fprintf(stderr,"[sm-rooms] missing generated collision hook $%06X\n",(unsigned)inside[i]);
    return !enabled;
  }
  return true;
}
uint32 SmRoomInsideDispatchHook(CpuState *cpu,uint32 pc24) {
  (void)pc24;
  if (g_sm_video.enhanced && g_sm_video.seamless && SmRoomCollisionInside(cpu->ram)>=0)
    cpu->X=0; /* native ordinary-air reaction: no foreign PLM/script execution */
  return 0;
}
