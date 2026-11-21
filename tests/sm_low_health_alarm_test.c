#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "mod_runtime.h"
#include "sm_mod_support.h"

static const char *mode="short";
static unsigned armed;
int snes_mod_register_activation_plugin(const char *id,SNESModActivationCallback callback) {
  (void)id;(void)callback;return 1;
}
const char *SmModOption(SmModValue *out,const char *package,const char *feature,
    const char *option,const char *fallback) {
  (void)package;(void)feature;(void)option;(void)fallback;
  snprintf(out->text,sizeof(out->text),"%s",mode);return out->text;
}
size_t SmModArmHooks(const char *name,const uint32_t *pcs,size_t count) {
  assert(!strcmp(name,"low-health-alarm") && count==4 && pcs[0]==0x90ea98 && pcs[3]==0x90f339);
  armed=(unsigned)count;return count;
}
#include "../src/mods/sm_low_health_alarm_plugin.c"

int main(void) {
  static uint8_t ram[0x20000];CpuState cpu={0};cpu.ram=ram;
  SmLowHealthAlarmActivate();assert(armed==4);
  assert(!SmLowHealthAlarmHook(&cpu,0x90ea98));
  cpu.P=0x82;cpu._flag_N=cpu._flag_Z=1;
  assert(SmLowHealthAlarmHook(&cpu,0x90ea9d)==0x90ea92);
  assert(Warning(&cpu)==31 && cpu.A==2 && !(cpu.P&0x82) && !cpu._flag_N && !cpu._flag_Z);
  for (unsigned i=0;i<29;++i) assert(!SmLowHealthAlarmHook(&cpu,0x90ea98));
  assert(Warning(&cpu)==2 && SmLowHealthAlarmHook(&cpu,0x90ea98)==0x90ea8f && Warning(&cpu)==1);
  for (unsigned i=0;i<120;++i) assert(!SmLowHealthAlarmHook(&cpu,0x90ea98) && Warning(&cpu)==1);
  assert(SmLowHealthAlarmHook(&cpu,0x90f339)==0x90f340);
  assert(!SmLowHealthAlarmHook(&cpu,0x90ea92)); /* native stop command executes */
  /* The only duration state is already guest-owned and restores exactly. */
  SetWarning(&cpu,18);uint8_t saved[2]={ram[0xa6a],ram[0xa6b]};
  assert(!SmLowHealthAlarmHook(&cpu,0x90f339)); /* resume the unfinished burst */
  for (unsigned i=0;i<17;++i) SmLowHealthAlarmHook(&cpu,0x90ea98);
  assert(Warning(&cpu)==1);memcpy(ram+0xa6a,saved,2);
  for (unsigned i=0;i<16;++i) assert(!SmLowHealthAlarmHook(&cpu,0x90ea98));
  assert(SmLowHealthAlarmHook(&cpu,0x90ea98)==0x90ea8f && Warning(&cpu)==1);
  mode="muted";SmLowHealthAlarmActivate();SetWarning(&cpu,0);
  assert(SmLowHealthAlarmHook(&cpu,0x90f339)==0x90f340);
  assert(SmLowHealthAlarmHook(&cpu,0x90ea9d)==0x90eaa4); /* native flag store, no start */
  SetWarning(&cpu,1);
  for (unsigned i=0;i<120;++i) assert(!SmLowHealthAlarmHook(&cpu,0x90ea98) && Warning(&cpu)==1);
  assert(SmLowHealthAlarmHook(&cpu,0x90ea92)==0x90eaaa); /* no unrelated SFX stop */
  cpu.m_flag=1;assert(!SmLowHealthAlarmHook(&cpu,0x90ea9d));
  puts("Native alarm start/stop policy, one brief episode, muted isolation and restored duration pass.");
  return 0;
}
