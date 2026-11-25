#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include "sm_direct_missile.h"
#include "runner_input.h"

static unsigned arms;
bool snes_mod_hook_arm(uint32_t pc, bool on) {
  assert(on && (pc==0x90DD3D || pc==0x90DD5F)); ++arms; return true;
}
extern uint32_t SmDirectMissileBeginHook(CpuState *,uint32_t);
extern uint32_t SmDirectMissileEndHook(CpuState *,uint32_t);
static uint8_t ram[0x20000];
static void w(unsigned a,unsigned v) { ram[a]=(uint8_t)v;ram[a+1]=(uint8_t)(v>>8); }
static unsigned r(unsigned a) { return ram[a]|ram[a+1]<<8; }
int main(void) {
  CpuState cpu={0};cpu.ram=ram;
  SmDirectMissileReset();assert(arms==2);
  assert(SmDirectMissileInputsValid(0xC9FFFFFFu));
  assert(!SmDirectMissileInputsValid(1u<<25));
  assert(!SmDirectMissileInputsValid(1u<<29));
  /* Exhaustively retain both pad words and all three action bits per seat. */
  for (unsigned x=0;x<=SNES_INPUT_SEAT_MASK;++x) {
    uint32_t both=SnesInputPackSeat((uint16_t)x,0)|SnesInputPackSeat((uint16_t)(x^0x7fff),1)|(3u<<30);
    assert(SnesInputSeat(both,0)==x && SnesInputSeat(both,1)==(x^0x7fff));
    assert((both>>30)==3);
  }
  uint32_t in=SnesInputPackSeat(0x1000,1);
  SmDirectMissileFrame(&in);assert(!g_sm_direct_missile.pending && in==0);
  in=SnesInputPackSeat(0x1000,0)|0x123;
  SmDirectMissileFrame(&in);assert(in==0x123 && g_sm_direct_missile.pending);
  w(0x998,8);w(0xD32,0xC4F0);w(0x9C6,2);w(0x9D2,3);w(0xA04,3);
  w(0x9B2,0x40);w(0x8F,0x8000);w(0xE00,0x2000);
  SmDirectMissileBeginHook(&cpu,0x90DD3D);
  assert(g_sm_direct_missile.active && r(0x9D2)==1 && r(0xA04)==0 && r(0x8F)==0x8040);
  /* A mid-handler snapshot preserves the bracket; native ammo writes remain. */
  SmDirectMissileState snapshot=g_sm_direct_missile;
  assert(SmDirectMissileStateValid(&snapshot));w(0x9C6,1);
  memset(&g_sm_direct_missile,0,sizeof(g_sm_direct_missile));g_sm_direct_missile=snapshot;
  SmDirectMissileEndHook(&cpu,0x90DD5F);
  assert(!g_sm_direct_missile.active && r(0x9D2)==3 && r(0xA04)==3 && r(0x8F)==0x8000 && r(0xE00)==0x2000 && r(0x9C6)==1);
  in=SnesInputPackSeat(0x1000,0);SmDirectMissileFrame(&in);
  assert(!g_sm_direct_missile.pending); /* one native shot per physical press */
  in=0;SmDirectMissileFrame(&in);in=SnesInputPackSeat(0x1000,0);SmDirectMissileFrame(&in);
  w(0xA78,1);SmDirectMissileBeginHook(&cpu,0x90DD3D);assert(!g_sm_direct_missile.active);
  snapshot.active=2;assert(!SmDirectMissileStateValid(&snapshot));
  puts("direct missile: seat transport, edge, native firing bracket and snapshot continuation pass");
  return 0;
}
