#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "mod_hook.h"
#include "mod_runtime.h"
#include "sm_mod_support.h"
#include "../src/sm_room_runtime.h"
static SmRoomInstructionHook resident_hook;
void SmRoomRuntimeSetInstructionHook(SmRoomInstructionHook hook) { resident_hook=hook; }
static SNESModActivationCallback callbacks[3],reset;
static unsigned count,armed;
static const char *incoming_option="2",*outgoing_option="1.5",*enemy_option="1.5",*boss_option="2",*beam_option="1.125",*missile_option="1.25";
int snes_mod_register_activation_plugin(const char *id,SNESModActivationCallback fn) { (void)id;assert(count<3);callbacks[count++]=fn;return 1; }
int snes_mod_register_reset_callback(SNESModActivationCallback fn) { reset=fn;return 1; }
size_t SmModArmHooks(const char *name,const uint32_t *pcs,size_t n) { (void)name;(void)pcs;armed+=(unsigned)n;return n; }
uint16_t SmModReadRom16(uint32_t address) { return address==0xa0ec4f || address==0xa0e18f; }
const char *SmModOption(SmModValue *v,const char *p,const char *f,const char *o,const char *fallback) {
 (void)p;(void)f;const char *s=fallback;
 if (!strcmp(o,"incoming")) s=incoming_option;
 if (!strcmp(o,"outgoing")) s=outgoing_option;
 if (!strcmp(o,"enemies")) s=enemy_option;
 if (!strcmp(o,"bosses")) s=boss_option;
 if (!strcmp(o,"beams")) s=beam_option;
 if (!strcmp(o,"missiles")) s=missile_option;
 snprintf(v->text,sizeof(v->text),"%s",s);return v->text;
}
#include "sm_difficulty_plugins.c"
static uint8_t ram[0x20000];
int main(void) {
 CpuState cpu={0};cpu.ram=ram;
 assert(count==3 && reset);for (unsigned i=0;i<3;++i) callbacks[i]();assert(armed==20);
 cpu.D=0x100;SetW(&cpu,0x112,150);SmIncomingDamageHook(&cpu,0x91df6b);assert(W(&cpu,0x112)==300);
 /* The original sentinel is checked before this hook; a scaled 300 is real damage. */
 SetW(&cpu,0xa50,1);SetW(&cpu,0xa4e,0x8000);SmIncomingDamageHook(&cpu,0x90ea1a);assert(W(&cpu,0xa50)==3 && W(&cpu,0xa4e)==0);
 cpu.A=100;cpu.P=0x43;SmOutgoingDamageHook(&cpu,0x93803f);assert(cpu.A==150 && cpu.P==0x41);
 cpu.A=0;SmOutgoingDamageHook(&cpu,0x1380b5);assert(cpu.A==0 && cpu._flag_Z);
 cpu.A=0xffff;SmOutgoingDamageHook(&cpu,0x93803f);assert(cpu.A==0xffff);
 SetW(&cpu,0x112,200);SmContactDamageHook(&cpu,0xa0a54c);assert(W(&cpu,0x112)==300);
 SetW(&cpu,0x187a,200);SmPowerBombDamageHook(&cpu,0x20a62b);assert(W(&cpu,0x187a)==300);
 cpu.Y=0x80;SetW(&cpu,0xff8,0xd93f);SetW(&cpu,0x100c,60);SmEnemyHealthHook(&cpu,0xa08b93);assert(W(&cpu,0x100c)==90);
 cpu.Y=0;SetW(&cpu,0xf78,0xe17f);SetW(&cpu,0xf8c,18000);SmEnemyHealthHook(&cpu,0x2093d1);assert(W(&cpu,0xf8c)==0x7ffe);
 SetW(&cpu,0xfb8,0xec3f);SetW(&cpu,0xfcc,36000);SmEnemyHealthHook(&cpu,0x29bfb6);assert(W(&cpu,0xfcc)==0xfffe);
 for (unsigned v=0;v<3;++v) { unsigned hp=v==0 ? 0 : v==1 ? 0x7fff : 0xffff;SetW(&cpu,0xf8c,hp);SmEnemyHealthHook(&cpu,0xa08b93);assert(W(&cpu,0xf8c)==hp); }
 cpu.Y=1;SetW(&cpu,0xf8d,100);SmEnemyHealthHook(&cpu,0xa08b93);assert(W(&cpu,0xf8d)==100);
 cpu.X=0;SetW(&cpu,0xc18,0x8000);cpu.A=0x400;SmProjectileSpeedHook(&cpu,0x94a248);assert(cpu.A==0x480);
 cpu.A=(uint16_t)-0x400;SmProjectileSpeedHook(&cpu,0x14a2d7);assert((int16_t)cpu.A==-0x480 && cpu._flag_N && (cpu.P&0x80));
 SetW(&cpu,0xc18,0x100);cpu.A=0x400;SmProjectileSpeedHook(&cpu,0x94a483);assert(cpu.A==0x500);
 cpu.A=0xe00;SmProjectileSpeedHook(&cpu,0x94a4ed);assert(cpu.A==0xf00);
 cpu.A=0x1400;SmProjectileSpeedHook(&cpu,0x94a483);assert(cpu.A==0x1400);
 SetW(&cpu,0xc18,0x500);cpu.A=0x400;SmProjectileSpeedHook(&cpu,0x94a483);assert(cpu.A==0x400);
 cpu.m_flag=1;cpu.A=100;SmOutgoingDamageHook(&cpu,0x93803f);assert(cpu.A==100);cpu.m_flag=0;
 Interp816 resident={0};resident.k=0xa0;resident.pc=0x8b93;resident.y=0x80;
 SetW(&cpu,0xff8,0xd93f);SetW(&cpu,0x100c,60);assert(resident_hook);
 resident_hook(&resident,ram);assert(W(&cpu,0x100c)==90);
 resident.k=0x91;resident.pc=0xdf6b;resident.dp=0x100;SetW(&cpu,0x112,150);
 resident_hook(&resident,ram);assert(W(&cpu,0x112)==300);
 resident.pc=0xdf6a;SetW(&cpu,0x112,150);resident_hook(&resident,ram);assert(W(&cpu,0x112)==150);
 reset();assert(!resident_hook);cpu.A=100;SmOutgoingDamageHook(&cpu,0x93803f);assert(cpu.A==100);
 incoming_option="invalid";callbacks[0]();SetW(&cpu,0x112,100);SmIncomingDamageHook(&cpu,0x91df6b);assert(W(&cpu,0x112)==100);
 puts("Native damage, health, projectile speed and reset cases passed.");return 0;
}
