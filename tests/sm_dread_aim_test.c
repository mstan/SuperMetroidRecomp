#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "mod_runtime.h"
#include "sm_mod_support.h"
uint8_t g_ram[0x20000];
static SNESModActivationCallback reset;
int snes_mod_register_reset_callback(SNESModActivationCallback cb) { reset=cb;return 1; }
size_t SmModArmHooks(const char *name,const uint32_t *pcs,size_t n) {
  (void)name;assert(n==4 && pcs[0]==0x9181a9 && pcs[1]==0x908ea9);return n;
}
/* Native pose table values at the observation points used by this test. */
uint16_t SmModReadRom16(uint32_t at) {
  assert(at>=0x91b62b && (at-0x91b62b)%8==0);
  unsigned pose=(at-0x91b62b)/8;
  return (uint16_t)((pose==3 ? 0 : pose==0x2d ? 4 : pose==0x2e ? 5 : 2)<<8);
}
#include "sm_dread_aim.c"
static void init(CpuState *cpu) {
  memset(g_ram,0,sizeof(g_ram));memset(cpu,0,sizeof(*cpu));cpu->ram=g_ram;
  P(g_ram,State,8);P(g_ram,Pose,1);g_ram[Facing]=8;
  const unsigned cfg[]={0x800,0x400,0x200,0x100,0x40,0x80};
  for(unsigned i=0;i<6;++i)P(g_ram,0x9aa+2*i,cfg[i]);
  SmDreadAimConfigure(true,0x10);
}
static uint32_t tick(CpuState *c,unsigned held,unsigned pressed) {
  P(g_ram,Held,held);P(g_ram,New,pressed);return SmDreadAimTransition(c,0x9181a9);
}
int main(void) {
  CpuState c;init(&c);P(g_ram,Base,3);P(g_ram,Extra,2);
  assert(tick(&c,0x110,0x100)==0x9181e2 && W(g_ram,NextPose)==1);
  assert(!W(g_ram,Base) && !W(g_ram,Extra) && W(g_ram,Held)==0);
  assert(tick(&c,0x210,0x200)==0x9181e2 && W(g_ram,NextPose)==2);
  assert(tick(&c,0xa10,0)==0x9181e2 && W(g_ram,NextPose)==6); /* up-left */
  assert(tick(&c,0x410,0)==0x9181e2 && W(g_ram,NextPose)==7); /* grounded down diagonal */
  assert(tick(&c,0x90,0x80)==0x9181e2 && W(g_ram,NextPose)==0x59);
  assert(W(g_ram,Speed)&JumpUsed);
  assert(tick(&c,0x90,0x80)==0x9181e2 && W(g_ram,NextPose)==7 && !(W(g_ram,New)&0x80));
  /* Lock in air while decelerating: preserve physical motion separately
   * from a new aim facing. Keep fractions and current native collisions. */
  init(&c);g_ram[Movement]=6;P(g_ram,Pose,0x29);P(g_ram,0xb36,2);
  P(g_ram,Base,2);P(g_ram,BaseSub,0xc000);P(g_ram,Extra,1);P(g_ram,ExtraSub,0x8000);P(g_ram,Accel,1);
  assert(tick(&c,0x110,0)==0x9181e2 && (W(g_ram,Lock)&15)==4);
  assert((W(g_ram,Speed)&SpeedMask)==4 && W(g_ram,SpeedSub)==0x4000 && W(g_ram,Accel)==2);
  assert(!SmDreadMoveXHook(&c,0x908ea9) && g_ram[Facing]==4);
  assert(W(g_ram,0x12)==4 && W(g_ram,0x14)==0x4000);
  assert(!SmDreadMoveXEndHook(&c,0x908ede) && g_ram[Facing]==8);
  assert(tick(&c,0x410,0)==0x9181e2 && W(g_ram,NextPose)==0x2d);
  SmDreadMoveXHook(&c,0x908ea9);P(g_ram,0xdd0,1);SmDreadMoveXEndHook(&c,0x908eda);
  assert(!(W(g_ram,Speed)&SpeedMask) && !W(g_ram,SpeedSub));
  assert(tick(&c,0x80,0x80)==0 && !W(g_ram,Lock) && W(g_ram,New)==0x80);
  init(&c);g_ram[Movement]=6;P(g_ram,Pose,0x29);P(g_ram,Base,3);P(g_ram,Accel,2);
  tick(&c,0x210,0);g_ram[Facing]=4; /* native pose update after aiming left */
  SmDreadMoveXHook(&c,0x908ea9);SmDreadMoveXEndHook(&c,0x908ede);
  assert(!tick(&c,0,0) && W(g_ram,Base)==3 && W(g_ram,Accel)==1 && g_ram[Facing]==4);
  init(&c);tick(&c,0x90,0x80);g_ram[Movement]=2;P(g_ram,Pose,0x13);
  tick(&c,0x10,0);assert(W(g_ram,Lock)&LockJumpUsed);
  /* SamusFunc_F1D3 clears both unused speed words on a real landing. */
  P(g_ram,Speed,0);P(g_ram,SpeedSub,0);g_ram[Movement]=0;P(g_ram,Pose,1);
  assert(tick(&c,0x10,0)==0x9181e2 && W(g_ram,NextPose)==1); /* retain the aim too */
  assert(tick(&c,0x90,0x80)==0x9181e2 && W(g_ram,NextPose)==1 && !(W(g_ram,New)&0x80));
  tick(&c,0,0);assert(tick(&c,0x90,0x80)==0x9181e2 && W(g_ram,NextPose)==0x4b);
  init(&c);P(g_ram,State,15);assert(!tick(&c,0x110,0x100) && W(g_ram,Held)==0x110);
  P(g_ram,State,8);c.m_flag=1;assert(!tick(&c,0x110,0));
  c.m_flag=0;SmDreadAimConfigure(true,0x20);assert(!tick(&c,0x10,0));
  assert(tick(&c,0x820,0)==0x9181e2 && W(g_ram,NextPose)==3);
  reset();assert(!tick(&c,0x820,0));
  puts("PASS: planted native aim, jump/release contract, physical air momentum and native collision seam");
}
