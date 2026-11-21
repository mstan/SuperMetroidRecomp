/* Optional challenge settings modify values at their native consumption or
 * initialization points. No frame history, ROM edits or extra snapshot data. */
#include "mod_hook.h"
#include "mod_runtime.h"
#include "sm_mod_support.h"
#include "../sm_room_runtime.h"
#include "snes/interp816.h"
#include <string.h>

#define PACKAGE "super-metroid.combat"
static unsigned incoming=8,outgoing=8,enemy_health=8,boss_health=8,beam_speed=8,missile_speed=8;
static unsigned W(const CpuState *cpu,unsigned a) { return cpu->ram[a]|cpu->ram[a+1]<<8; }
static void SetW(CpuState *cpu,unsigned a,unsigned v) { cpu->ram[a]=(uint8_t)v;cpu->ram[a+1]=(uint8_t)(v>>8); }
static unsigned Scale(unsigned value,unsigned factor,unsigned limit) {
  if (!value || factor==8) return value;
  unsigned scaled=(value*factor+7)/8;
  return scaled>limit ? limit : scaled;
}
static void SetA(CpuState *cpu,unsigned value) {
  cpu->A=(uint16_t)value;cpu->_flag_N=(value>>15)&1;cpu->_flag_Z=value==0;
  cpu->P=(uint8_t)((cpu->P&~0x82)|(cpu->_flag_N ? 0x80 : 0)|(cpu->_flag_Z ? 2 : 0));
}
static unsigned Factor(const char *feature,const char *option,unsigned fallback) {
  SmModValue v;const char *s=SmModOption(&v,PACKAGE,feature,option,"1");
  static const char *const names[]={"0.5","1","1.125","1.25","1.5","2","3"};
  static const unsigned factors[]={4,8,9,10,12,16,24};
  for (unsigned i=0;i<7;++i) if (!strcmp(s,names[i])) return factors[i];
  return fallback;
}
uint32 SmIncomingDamageHook(CpuState *cpu,uint32 pc) {
  if (cpu->m_flag) return 0;
  if ((pc&65535)==0xdf6b) {
    /* Negative/300 sentinels and frozen time were already checked natively. */
    unsigned a=(uint16_t)(cpu->D+0x12);
    SetW(cpu,a,Scale(W(cpu,a),incoming,0x7fff));
  } else {
    /* After native suit reduction, before the full 16.16 environment debit. */
    uint64_t amount=((uint64_t)W(cpu,0xa50)<<16)|W(cpu,0xa4e);
    amount=(amount*incoming)/8;
    if (amount>0x7fffffff) amount=0x7fffffff;
    SetW(cpu,0xa50,(unsigned)(amount>>16));SetW(cpu,0xa4e,(unsigned)amount);
  }
  return 0;
}
uint32 SmOutgoingDamageHook(CpuState *cpu,uint32 pc) {
  (void)pc;
  if (!cpu->m_flag && !(cpu->A&0x8000)) SetA(cpu,Scale(cpu->A,outgoing,0x7fff));
  return 0;
}
uint32 SmContactDamageHook(CpuState *cpu,uint32 pc) {
  (void)pc;
  if (!cpu->m_flag) {
    unsigned a=(uint16_t)(cpu->D+0x12);
    SetW(cpu,a,Scale(W(cpu,a),outgoing,0x7fff));
  }
  return 0;
}
uint32 SmPowerBombDamageHook(CpuState *cpu,uint32 pc) {
  (void)pc;
  if (!cpu->m_flag) SetW(cpu,0x187a,Scale(W(cpu,0x187a),outgoing,0x7fff));
  return 0;
}
static void ScaleHealth(CpuState *cpu,unsigned index) {
  if (index>=0x800 || (index&63)) return;
  unsigned id=W(cpu,0xf78+index),health=W(cpu,0xf8c+index);
  if (!id || !health || health==0x7fff || health==0xffff) return;
  bool boss=SmModReadRom16(0xa00000u+id+0x10)!=0;
  unsigned factor=boss ? boss_health : enemy_health;
  /* Keep positive signed health positive; native unsigned boss phases can
   * already exceed that range. Zero/invulnerable sentinels stay untouched. */
  SetW(cpu,0xf8c+index,Scale(health,factor,health<0x7fff ? 0x7ffe : 0xfffe));
}
uint32 SmEnemyHealthHook(CpuState *cpu,uint32 pc) {
  if (!cpu->m_flag) ScaleHealth(cpu,((pc&0x7fffff)==0x298d6d || (pc&0x7fffff)==0x29bfb6) ? 0x40 : cpu->Y);
  return 0;
}
uint32 SmProjectileSpeedHook(CpuState *cpu,uint32 pc) {
  (void)pc;
  if (cpu->m_flag || cpu->X>=20 || (cpu->X&1)) return 0;
  unsigned type=W(cpu,0xc18+cpu->X),kind=type&0xf00,factor=8;
  if (!kind && (type&255)<0x20) factor=beam_speed;
  else if (kind==0x100 || kind==0x200) factor=missile_speed;
  int speed=(int16_t)cpu->A;unsigned magnitude=speed<0 ? (unsigned)-speed : (unsigned)speed;
  /* Do not introduce a new whole-block step. Faster native shots retain
   * their own speed, acceleration, collision and deletion behavior. */
  unsigned limit=magnitude>0xf00 ? magnitude : 0xf00;
  unsigned scaled=Scale(magnitude,factor,limit);
  SetA(cpu,(uint16_t)(speed<0 ? -(int)scaled : (int)scaled));
  return 0;
}
static void ResidentInstruction(Interp816 *c,uint8_t ram[0x20000]) {
  uint32 pc=((uint32)(c->k&127)<<16)|c->pc;
  SnesModHookFn hook;
  switch (pc) {
  case 0x208b93: case 0x2093d1: case 0x298d6d: case 0x29bfb6: hook=SmEnemyHealthHook;break;
  case 0x11df6b: case 0x10ea1a: hook=SmIncomingDamageHook;break;
  case 0x20a54c: hook=SmContactDamageHook;break;
  case 0x20a62b: hook=SmPowerBombDamageHook;break;
  default:return;
  }
  CpuState cpu={0};cpu.ram=ram;cpu.A=c->a;cpu.X=c->x;cpu.Y=c->y;cpu.D=c->dp;
  cpu.m_flag=c->mf;cpu.x_flag=c->xf;cpu.emulation=c->e;
  cpu._flag_N=c->n;cpu._flag_Z=c->z;cpu._flag_V=c->v;cpu._flag_C=c->c;
  cpu.P=(uint8_t)((c->n?128:0)|(c->v?64:0)|(c->mf?32:0)|(c->xf?16:0)|
      (c->d?8:0)|(c->i?4:0)|(c->z?2:0)|(c->c?1:0));
  hook(&cpu,pc);
  c->a=cpu.A;c->x=cpu.X;c->y=cpu.Y;c->dp=cpu.D;
  c->n=cpu._flag_N!=0;c->z=cpu._flag_Z!=0;c->v=cpu._flag_V!=0;c->c=cpu._flag_C!=0;
}
static void DamageActivate(void) {
  incoming=Factor("damage-multipliers","incoming",8);outgoing=Factor("damage-multipliers","outgoing",8);
  static const uint32_t pcs[]={0x91df6b,0x90ea1a,0x93803f,0x938086,0x9380b5,0x938148,0x938185,0x9381b9,0xa0a54c,0xa0a62b};
  SmModArmHooks("damage-multipliers",pcs,sizeof(pcs)/sizeof(*pcs));
  SmRoomRuntimeSetInstructionHook(ResidentInstruction);
}
static void HealthActivate(void) {
  enemy_health=Factor("health-multipliers","enemies",8);boss_health=Factor("health-multipliers","bosses",8);
  static const uint32_t pcs[]={0xa08b93,0xa093d1,0xa98d6d,0xa9bfb6};
  SmModArmHooks("health-multipliers",pcs,sizeof(pcs)/sizeof(*pcs));
  SmRoomRuntimeSetInstructionHook(ResidentInstruction);
}
static void SpeedActivate(void) {
  beam_speed=Factor("projectile-speed","beams",8);missile_speed=Factor("projectile-speed","missiles",8);
  static const uint32_t pcs[]={0x94a248,0x94a2d7,0x94a35f,0x94a3f1,0x94a483,0x94a4ed};
  SmModArmHooks("projectile-speed",pcs,sizeof(pcs)/sizeof(*pcs));
}
static void Reset(void) {
  incoming=outgoing=enemy_health=boss_health=beam_speed=missile_speed=8;
  SmRoomRuntimeSetInstructionHook(NULL);
}
SNES_MOD_CONSTRUCTOR(sm_register_difficulty) {
  snes_mod_register_reset_callback(Reset);
  snes_mod_register_activation_plugin(PACKAGE ".damage-multipliers",DamageActivate);
  snes_mod_register_activation_plugin(PACKAGE ".health-multipliers",HealthActivate);
  snes_mod_register_activation_plugin(PACKAGE ".projectile-speed",SpeedActivate);
}
