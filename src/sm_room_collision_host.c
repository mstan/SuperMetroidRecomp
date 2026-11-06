#include "sm_room_collision.h"
#include "sm_video.h"
#include "sm_rooms.h"
#include "sm_shot_view.h"
#include "sm_activity.h"
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
  const uint32_t actors[]={0xa0c845,0xa0c879};
  const uint32_t entities[]={0xa08fd4,0xa09168,0xa0a391,0x868104,0x868124,0x868118,0x868091,0x8680fe,0xa09419,0x938070,0x9381d0};
  const uint32_t contacts[]={0xa0a07a,0xa09a5a,0xa0a143,0xa09b7f,0xa0a236,0xa09d23,0xa09923,0xa099f9,0x86efea,0xa0a326,0x868d5c};
  const uint32_t viewport[]={0xa0ad79,0xa0ad85,0xa0adf5,0xa0ae05};
  for (unsigned i=0; i<4; ++i) if (!snes_mod_hook_arm(viewport[i],SmActivityExtra()!=0)) return false;
  entity_phase=0;
  for (unsigned i=0; i<sizeof(entities)/sizeof(*entities); ++i)
    if (!snes_mod_hook_arm(entities[i],g_sm_video.enhanced) && g_sm_video.enhanced) return false;
  if (eproj_loan.active) { fprintf(stderr,"[sm-rooms] unfinished native projectile terrain loan\n");return false; }
  for (unsigned i=0; i<sizeof(eprojs)/sizeof(*eprojs); ++i)
    if (!snes_mod_hook_arm(eprojs[i],g_sm_video.enhanced) && g_sm_video.enhanced) return false;
  for (unsigned i=0; i<sizeof(actors)/sizeof(*actors); ++i)
    if (!snes_mod_hook_arm(actors[i],g_sm_video.enhanced) && g_sm_video.enhanced) return false;
  for (unsigned i=0; i<sizeof(contacts)/sizeof(*contacts); ++i)
    if (!snes_mod_hook_arm(contacts[i],g_sm_video.enhanced) && g_sm_video.enhanced) return false;
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
uint32 SmRoomActivityHook(CpuState *cpu,uint32 pc24) {
  unsigned pc=pc24&65535,extra=SmActivityExtra();
  if (extra && cpu->_flag_N && SmActivityEnemyHorizontalVisible(cpu->ram,SmShotWord(cpu->ram,0xe54),extra,pc==0xadf5 || pc==0xae05)) {
    cpu->_flag_N=0;cpu->P&=(uint8_t)~0x80;
  }
  return 0;
}
uint32 SmRoomEntityHook(CpuState *cpu,uint32 pc24) {
  unsigned pc=pc24&0x7fffff;
  if (pc==0x208fd4) {
    entity_phase=1;
    if (!SmRoomsFilterActiveQueues(cpu->ram)) abort();
  }
  else if (pc==0x068104) entity_phase=2;
  else if (pc==0x209168 || pc==0x20a391 || pc==0x068124) entity_phase=0;
  else if ((pc==0x068091 || pc==0x0680fe) && cpu->A<36 && !(cpu->A&1))
    SmRoomsEntityBorn(cpu->ram,cpu->ram,0,0,32+cpu->A/2,entity_phase);
  else if (pc==0x209419 && cpu->X<2048 && !(cpu->X&63))
    SmRoomsEntityBorn(cpu->ram,cpu->ram,0,0,cpu->X/64,entity_phase);
  else if ((pc==0x138070 || pc==0x1381d0) && cpu->X<20 && !(cpu->X&1))
    SmRoomsEntityBorn(cpu->ram,cpu->ram,0,0,50+cpu->X/2,3);
  else if (pc==0x068118 && cpu->X<36 && !(cpu->X&1) &&
      !(SmRoomsActiveRules(cpu->ram,32+cpu->X/2)&SM_ROOM_SIMULATES)) return (pc24&0xff0000u)|0x811e;
  return 0;
}
uint32 SmRoomInteractionHook(CpuState *cpu,uint32 pc24) {
  if (!g_sm_video.enhanced) return 0;
  unsigned pc=pc24&0xffff,index,slot,target;
  if (pc==0xa326) entity_phase=1; /* native PB death/drop children inherit their actor's spatial room */
  if ((pc24&0x7fffff)==0x068d5c) {
    index=cpu->X;if (index>=36 || (index&1)) return 0;
    slot=32+index/2;target=0x8d98; /* RTS after PHX/PHY cleanup; no prologue was executed */
  } else if ((pc24&0x7fffff)==0x06efea) {
    index=cpu->X;if (index>=36 || (index&1)) return 0;
    slot=32+index/2;target=0xf0aa; /* native PLY/PLX/RTS, after lifespan decrement */
  } else if (pc==0x9923 || pc==0x99f9) {
    index=cpu->X;if (index>=36 || (index&1)) return 0;
    slot=32+index/2;target=pc==0x9923 ? 0x996b : 0x9a59;
  } else {
    index=SmShotWord(cpu->ram,0xe54);if (index>=2048 || (index&63)) return 0;
    slot=index/64;
    target=pc==0xa326 ? 0xa391 : pc==0xa07a ? 0xa0b7 : pc==0x9a5a ? 0x9b7e : pc==0xa143 ? 0xa15e :
        pc==0x9b7f ? 0x9d16 : pc==0xa236 ? 0xa251 : 0x9d65;
  }
  return (SmRoomsActiveRules(cpu->ram,slot)&SM_ROOM_INTERACTS) ? 0 : (pc24&0xff0000u)|target;
}
static bool active_eproj_tile(void *context,const uint8_t source[0x20000],int x,int y,SmEprojTile *tile) {
  (void)context;return SmRoomsEprojTile(source,source,0,0,x,y,tile);
}
uint32 SmRoomEnemyBlockHook(CpuState *cpu,uint32 pc24) {
  if (!g_sm_video.enhanced) return 0;
  bool horizontal=(pc24&65535)==0xc845;uint16_t original=cpu->X,index=original;
  int result=SmRoomsEnemyBlock(cpu->ram,cpu->ram,0,0,horizontal,&index);cpu->X=index;
  if (result==-1) {
    if (index!=original) cpu->A=(uint16_t)SmShotWord(cpu->ram,0x10002+index);
    return 0;
  }
  if (result<0) { fprintf(stderr,"[sm-rooms] native world actor reaction failed\n");abort(); }
  cpu->_flag_C=result!=0;cpu->_flag_N=(index&0x8000)!=0;cpu->_flag_Z=index==0;
  cpu->P=(uint8_t)((cpu->P&~0x83) | cpu->_flag_C | cpu->_flag_Z<<1 | cpu->_flag_N<<7);
  return (pc24&0xff0000u) | (horizontal ? 0xc858 : 0xc88c);
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
