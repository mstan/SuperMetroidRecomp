#include "sm_direct_missile.h"
#include "mod_hook.h"
#include "runner_input.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

SmDirectMissileState g_sm_direct_missile;
static uint16_t word(const CpuState *cpu, unsigned a) {
  return (uint16_t)(cpu->ram[a] | cpu->ram[a+1] << 8);
}
static void set_word(CpuState *cpu, unsigned a, uint16_t v) {
  cpu->ram[a]=(uint8_t)v; cpu->ram[a+1]=(uint8_t)(v>>8);
}
void SmDirectMissileReset(void) {
  memset(&g_sm_direct_missile,0,sizeof(g_sm_direct_missile));
  if (!snes_mod_hook_arm(0x90DD3D,true) || !snes_mod_hook_arm(0x90DD5F,true)) {
    fprintf(stderr,"[sm-input] native missile firing observers are missing\n");
    abort();
  }
}
int SmDirectMissileStateValid(const SmDirectMissileState *s) {
  return s->held<=1 && s->pending<=1 && s->active<=1 && (!s->active || s->selection<=6);
}
int SmDirectMissileInputsValid(uint32_t inputs) {
  /* This title declares one action. Player 2's same action remains isolated. */
  return !(inputs & 0x36000000u);
}
void SmDirectMissileFrame(uint32_t *inputs) {
  if (!SmDirectMissileInputsValid(*inputs)) {
    fprintf(stderr,"[sm-input] unsupported recorded game actions: %08x\n",(unsigned)*inputs);
    abort();
  }
  const unsigned held=(SnesInputSeat(*inputs,0)>>12)&1u;
  g_sm_direct_missile.pending=held && !g_sm_direct_missile.held;
  g_sm_direct_missile.held=(uint16_t)held;
  *inputs &= ~0x3f000000u; /* Recorded intent must never reach guest joypads. */
}
/* Normal firing already excludes morphing and unsuitable movement states.
 * The stock routine owns ammo, slots, cooldown, direction, sound and effects.
 * Its selected-item reads occur only within this bracket, before HUD drawing.
 * Preserve the bracket in execution snapshots if a scheduler yield occurs. */
uint32_t SmDirectMissileBeginHook(CpuState *cpu, uint32_t pc24) {
  (void)pc24;
  SmDirectMissileState *s=&g_sm_direct_missile;
  if (!s->pending || s->active || word(cpu,0x998)!=8 || word(cpu,0xA78) ||
      word(cpu,0xD32)!=0xC4F0 || !word(cpu,0x9C6)) return 0;
  s->pending=0; s->active=1;
  s->selection=word(cpu,0x9D2); s->cancel=word(cpu,0xA04);
  s->new_keys=word(cpu,0x8F); s->filtered=word(cpu,0xE00);
  set_word(cpu,0x9D2,1); set_word(cpu,0xA04,0);
  set_word(cpu,0x8F,(uint16_t)(s->new_keys|word(cpu,0x9B2)));
  return 0;
}
uint32_t SmDirectMissileEndHook(CpuState *cpu, uint32_t pc24) {
  (void)pc24;
  SmDirectMissileState *s=&g_sm_direct_missile;
  if (s->active) {
    set_word(cpu,0x9D2,s->selection); set_word(cpu,0xA04,s->cancel);
    set_word(cpu,0x8F,s->new_keys); set_word(cpu,0xE00,s->filtered);
    s->active=0;
  }
  return 0;
}
