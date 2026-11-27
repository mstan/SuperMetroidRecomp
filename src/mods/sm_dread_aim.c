/* Hold-to-lock aiming through native poses and collision/movement handlers.
 * Native art supports up, diagonals and horizontal on the ground; straight
 * down is available in the air. Ground Down selects the facing down diagonal.
 * The three unused native words below are saved with current-build WRAM.
 * Native reset/death/grapple initialization already clears the speed words;
 * native crouch/knockback handling clears the lock word. */
#include "sm_dread_aim.h"
#include "common_rtl.h"
#include "mod_runtime.h"
#include "sm_mod_support.h"

enum { Held=0x8b,New=0x8f,State=0x998,Pose=0xa1c,Facing=0xa1e,Movement=0xa1f,
  NextPose=0xa28,BombJump=0xa56,Frozen=0xa78,
  Lock=0xaa4,SpeedSub=0xb2a,Speed=0xb38,
  Base=0xb46,BaseSub=0xb48,Extra=0xb42,ExtraSub=0xb44,Accel=0xb4a,
  Locked=0x8000,Air=0x4000,Moving=0x2000,LockJumpUsed=0x1000,JumpUsed=0x8000,SpeedMask=0xfff };
static bool enabled;
static unsigned shoulder=0x10;
static uint16_t W(const uint8_t *r,unsigned a) { return (uint16_t)(r[a]|r[a+1]<<8); }
static void P(uint8_t *r,unsigned a,unsigned v) { r[a]=(uint8_t)v;r[a+1]=(uint8_t)(v>>8); }
static uint32_t Fixed(const uint8_t *r,unsigned high,unsigned low) { return (uint32_t)W(r,high)<<16|W(r,low); }
static bool Button(const uint8_t *r,unsigned config) { return (W(r,Held)&W(r,config))!=0; }
static unsigned At(uint32_t pc,unsigned target) { return (pc&0xff0000u)|target; }
static unsigned Angle(unsigned pose) {
  if (pose>0xfc) return 2;
  unsigned dir=SmModReadRom16(0x91b62b+pose*8)>>8;
  return dir<5 ? dir : dir<10 ? 9-dir : 2;
}
static void Release(uint8_t *r) {
  unsigned lock=W(r,Lock),type=r[Movement];
  bool air=type==2 || type==3 || type==6 || type==0x14 || type==0x17 || type==0x18;
  if (enabled && air && (lock&(Locked|Air))==(Locked|Air)) {
    /* Native mode1 moves opposite the current facing while decelerating.
     * Keep physical direction when aim and momentum face opposite ways. */
    P(r,Base,W(r,Speed)&SpeedMask);P(r,BaseSub,W(r,SpeedSub));
    P(r,Extra,0);P(r,ExtraSub,0);P(r,Accel,(r[Facing]&15)==(lock&15) ? 2 : 1);
  }
  P(r,Lock,0);P(r,Speed,0);P(r,SpeedSub,0);
}
void SmDreadAimConfigure(bool active,unsigned native_shoulder) {
  Release(g_ram);
  enabled=active;shoulder=native_shoulder==0x20 ? 0x20 : 0x10;
  if (active) {
    static const uint32_t hooks[]={0x9181a9,0x908ea9,0x908eda,0x908ede};
    SmModArmHooks("Dread shoulder aim",hooks,sizeof(hooks)/sizeof(*hooks));
  }
}
static void Reset(void) { SmDreadAimConfigure(false,0x10); }
SNES_MOD_CONSTRUCTOR(sm_register_dread_aim) { snes_mod_register_reset_callback(Reset); }

uint32 SmDreadAimTransition(CpuState *cpu,uint32 pc) {
  if (!enabled) return 0;
  uint8_t *r=cpu->ram;unsigned state=W(r,State),type=r[Movement],pose=W(r,Pose);
  bool ground=type==0 || type==1 || type==5 || type==0xe;
  bool air=type==2 || type==3 || type==6 || type==0x14 || type==0x17 || type==0x18;
  bool takeoff=type==0xf && (pose==0x4b || pose==0x4c || (pose>=0x55 && pose<=0x5a));
  if (cpu->m_flag || cpu->x_flag || (state!=7 && state!=8 && state!=18) || W(r,Frozen) ||
      !(W(r,Held)&shoulder) || (!ground && !air && !takeoff)) {
    Release(r);return 0;
  }
  unsigned old=W(r,Lock),face=r[Facing]==4 ? 4 : 8;
  /* Native landing clears the speed words. Keep the hold's jump latch in
   * the lock word too, so landing does not authorize another jump. */
  unsigned stored=W(r,Speed),used=((stored&JumpUsed) || (old&LockJumpUsed)) ? JumpUsed : 0;
  unsigned angle=(old&Locked) ? (old>>5)&7 : used ? (stored>>12)&7 : Angle(pose);
  if(angle>4)angle=2;
  if (air && !(old&Air)) {
    uint32_t speed=Fixed(r,Base,BaseSub)+Fixed(r,Extra,ExtraSub);
    if (speed>0x0fffffffu) speed=0x0fffffffu;
    unsigned physical_face=W(r,Accel)==1 ? (face^12) : face;
    stored=(speed>>16)|JumpUsed;used=JumpUsed;
    P(r,SpeedSub,speed);P(r,Lock,Locked|Air|LockJumpUsed|physical_face);
  } else if (ground) {
    stored=used;P(r,SpeedSub,0);P(r,Lock,Locked|face|(used ? LockJumpUsed : 0));
    P(r,Base,0);P(r,BaseSub,0);P(r,Extra,0);P(r,ExtraSub,0);P(r,Accel,0);
  }
  if (air) P(r,Accel,2); /* native fall mover must run with the D-pad consumed */
  bool left=Button(r,0x9ae),right=Button(r,0x9b0),up=Button(r,0x9aa),down=Button(r,0x9ac);
  if (left!=right) face=left ? 4 : 8;
  if (up!=down) angle=up ? (left!=right ? 1 : 0) : (air && left==right ? 4 : 3);
  else if (left!=right) angle=2;
  if (ground && angle==4) angle=3;
  unsigned directions=W(r,0x9aa)|W(r,0x9ac)|W(r,0x9ae)|W(r,0x9b0)|shoulder;
  bool jump=(W(r,New)&W(r,0x9b4))!=0;
  P(r,Held,W(r,Held)&~directions);P(r,New,W(r,New)&~directions);
  if (air || used) P(r,New,W(r,New)&~W(r,0x9b4)); /* release before respin/rejump */
  P(r,Speed,(stored&(SpeedMask|JumpUsed))|(angle<<12));
  P(r,Lock,(W(r,Lock)&~0xe0u)|(angle<<5));
  if (takeoff) return 0; /* native takeoff animation and gravity */
  static const uint16_t standing[]={3,5,1,7,7};
  static const uint16_t jumping[]={0x15,0x69,0x13,0x6b,0x17};
  static const uint16_t falling[]={0x2b,0x6d,0x29,0x6f,0x2d};
  unsigned target=(ground ? standing : W(r,0xb36)==2 ? falling : jumping)[angle]+(face==4);
  if(ground && jump && !used) {
    static const unsigned takeoff_pose[]={0x55,0x57,0x4b,0x59,0x59};
    target=takeoff_pose[angle]+(face==4);P(r,Speed,W(r,Speed)|JumpUsed);P(r,Lock,W(r,Lock)|LockJumpUsed);
  }
  P(r,NextPose,target);P(r,BombJump,0);
  return At(pc,0x81e2); /* native CLC/RTS; native pose transitions own artwork */
}

/* Samus_MoveX enters with the native 16.16 integer in $12, fraction in $14. Keep
 * captured airborne momentum, but let its original collision and current
 * displacement handlers perform the actual movement. */
uint32 SmDreadMoveXHook(CpuState *cpu,uint32 pc) {
  (void)pc;
  uint8_t *r=cpu->ram;unsigned lock=W(r,Lock);
  if (!enabled || (lock&(Locked|Air))!=(Locked|Air) || cpu->m_flag || cpu->x_flag) return 0;
  unsigned aim=r[Facing];
  P(r,Lock,(lock&0xd0ef)|Moving|((aim&15)<<8));r[Facing]=(uint8_t)(lock&15);
  P(r,0x12,W(r,Speed)&SpeedMask);P(r,0x14,W(r,SpeedSub));
  P(r,Base,W(r,Speed)&SpeedMask);P(r,BaseSub,W(r,SpeedSub));P(r,Extra,0);P(r,ExtraSub,0);P(r,Accel,2);
  return 0;
}
/* The two RTS instructions after native MoveRight/MoveLeft. Restore aim
 * facing after the movement decision; a real wall collision stops momentum. */
uint32 SmDreadMoveXEndHook(CpuState *cpu,uint32 pc) {
  (void)pc;
  uint8_t *r=cpu->ram;unsigned lock=W(r,Lock);
  if (!enabled || !(lock&Moving)) return 0;
  r[Facing]=(uint8_t)((lock>>8)&15);P(r,Lock,lock&~Moving);
  if (W(r,0xdd0)) { P(r,Speed,W(r,Speed)&~SpeedMask);P(r,SpeedSub,0); }
  return 0;
}
