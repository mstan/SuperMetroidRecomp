#include "sm_room_collision.h"
#include "sm_video.h"
#include "sm_rooms.h"
#include "sm_shot_view.h"
#include "mod_hook.h"
#include <stdio.h>
#include <stdlib.h>
/* The body dispatch PCs are shared with the elective screw-attack block
 * behavior. That callback applies this core room query before its own policy. */
extern bool SmCombatShotBlockDispatchActive(void);
static SmEprojLoan eproj_loan;
static unsigned entity_phase;
bool SmRoomCollisionFrameBegin(void) {
  bool enabled=g_sm_video.enhanced && g_sm_video.seamless;
  const uint32_t body[]={0x949527,0x94953e},inside[]={0x949b9e,0x949bd3,0x949c18};
  const uint32_t shots[]={0x94a1b5,0x94a1d6,0x94a1cd,0x94a1ee,
      0x94a2a8,0x94a337,0x94a3bb,0x94a3c3,0x94a3cd,0x94a44d,0x94a455,0x94a45f,0x94a4c6,0x94a530};
  const uint32_t eprojs[]={0x868886,0x86889e,0x86889d,0x8688b5};
  const uint32_t entities[]={0xa08fd4,0xa09168,0x868104,0x868124,0x868091,0x8680fe,0xa09419,0x938070,0x9381d0};
  entity_phase=0;
  for (unsigned i=0; i<sizeof(entities)/sizeof(*entities); ++i)
    if (!snes_mod_hook_arm(entities[i],g_sm_video.enhanced) && g_sm_video.enhanced) return false;
  if (eproj_loan.active) { fprintf(stderr,"[sm-rooms] unfinished native projectile terrain loan\n");return false; }
  for (unsigned i=0; i<sizeof(eprojs)/sizeof(*eprojs); ++i)
    if (!snes_mod_hook_arm(eprojs[i],g_sm_video.enhanced) && g_sm_video.enhanced) return false;
  for (unsigned i=0; i<sizeof(shots)/sizeof(*shots); ++i)
    if (!snes_mod_hook_arm(shots[i],g_sm_video.enhanced) && g_sm_video.enhanced) return false;
  for (unsigned i=0; i<2; ++i)
    if (!snes_mod_hook_arm(body[i],enabled || SmCombatShotBlockDispatchActive())) return false;
  for (unsigned i=0; i<3; ++i) if (!snes_mod_hook_arm(inside[i],enabled)) {
    if (enabled) fprintf(stderr,"[sm-rooms] missing generated collision hook $%06X\n",(unsigned)inside[i]);
    return !enabled;
  }
  return true;
}
uint32 SmRoomEntityHook(CpuState *cpu,uint32 pc24) {
  unsigned pc=pc24&0x7fffff;
  if (pc==0x208fd4) entity_phase=1;
  else if (pc==0x068104) entity_phase=2;
  else if (pc==0x209168 || pc==0x068124) entity_phase=0;
  else if ((pc==0x068091 || pc==0x0680fe) && cpu->A<36 && !(cpu->A&1))
    SmRoomsEntityBorn(cpu->ram,cpu->ram,0,0,32+cpu->A/2,entity_phase);
  else if (pc==0x209419 && cpu->X<2048 && !(cpu->X&63))
    SmRoomsEntityBorn(cpu->ram,cpu->ram,0,0,cpu->X/64,entity_phase);
  else if ((pc==0x138070 || pc==0x1381d0) && cpu->X<20 && !(cpu->X&1))
    SmRoomsEntityBorn(cpu->ram,cpu->ram,0,0,50+cpu->X/2,3);
  return 0;
}
static bool active_eproj_tile(void *context,const uint8_t source[0x20000],int x,int y,SmEprojTile *tile) {
  (void)context;return SmRoomsEprojTile(source,source,0,0,x,y,tile);
}
uint32 SmRoomEprojBlockHook(CpuState *cpu,uint32 pc24) {
  unsigned pc=pc24&65535;
  if (pc==0x8886 || pc==0x889e) {
    if (g_sm_video.enhanced)
      SmEprojBegin(cpu->ram,&cpu->X,pc==0x8886,active_eproj_tile,NULL,&eproj_loan);
  } else if (SmEprojEnd(cpu->ram,&cpu->X,pc==0x889d,&eproj_loan)) {
    cpu->_flag_N=(cpu->X&0x8000)!=0;cpu->_flag_Z=cpu->X==0;
    cpu->P=(uint8_t)((cpu->P&~0x82) | cpu->_flag_Z<<1 | cpu->_flag_N<<7);
  }
  return 0;
}
uint32 SmRoomInsideDispatchHook(CpuState *cpu,uint32 pc24) {
  (void)pc24;
  if (g_sm_video.enhanced && g_sm_video.seamless && SmRoomCollisionInside(cpu->ram)>=0)
    cpu->X=0; /* native ordinary-air reaction: no foreign PLM/script execution */
  return 0;
}
uint32 SmRoomShotBoundsHook(CpuState *cpu,uint32 pc24) {
  if (!g_sm_video.enhanced || !SmRoomsShotsActive(cpu->ram)) return 0;
  /* The following block reaction resolves signed world cells. Keep the
   * cartridge's movement, radii, span loops, impacts and wave-beam rules. */
  unsigned pc=pc24&65535;
  cpu->_flag_N=pc==0xa3bb || pc==0xa44d ? 0 : 1;
  cpu->P=(uint8_t)((cpu->P&~0x80) | (cpu->_flag_N ? 0x80 : 0));
  return 0;
}
uint32 SmRoomShotDispatchHook(CpuState *cpu,uint32 pc24) {
  (void)pc24;
  if (g_sm_video.enhanced && SmRoomsShotsActive(cpu->ram) && cpu->X==18 &&
      SmRoomsShotDoorAir(cpu->ram,0,0,SmShotWord(cpu->ram,0xdc4))) cpu->X=0;
  return 0;
}
uint32 SmRoomShotBlockHook(CpuState *cpu,uint32 pc24) {
  if (!g_sm_video.enhanced) return 0;
  bool horizontal=(pc24&65535)==0xa1b5;
  uint16_t index=cpu->X;
  int result=SmRoomsShotBlock(cpu->ram,horizontal,&index);
  cpu->X=index;
  if (result==-1) return 0;
  if (result<0) {
    fprintf(stderr,"[sm-rooms] native world projectile reaction failed at $%06X\n",(unsigned)pc24);
    abort();
  }
  /* Match the native PLX/RTS result. Every caller consumes carry and the
   * native span counters, then overwrites A before using it again. */
  cpu->_flag_C=result!=0; cpu->_flag_N=(index&0x8000)!=0; cpu->_flag_Z=index==0;
  cpu->P=(uint8_t)((cpu->P&~0x83) | cpu->_flag_C | cpu->_flag_Z<<1 | cpu->_flag_N<<7);
  return (pc24&0xff0000u) | (horizontal ? 0xa1d5 : 0xa1f6);
}
