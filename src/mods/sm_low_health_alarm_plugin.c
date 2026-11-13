/* Optional low-health audio policy. The stock warning word is otherwise a
 * boolean used only by this routine. Brief mode uses it as a countdown, so
 * native file states, rewind and rollback also retain the remaining burst.
 * Native health checks, warning visuals, sound queues and recovery stay owned
 * by the cartridge. Muting never issues a stop command for unrelated SFX. */
#include "mod_hook.h"
#include "mod_runtime.h"
#include "sm_mod_support.h"
#include <stdbool.h>
#include <string.h>

static bool alarm_muted;
static uint16_t Warning(const CpuState *cpu) {
  return (uint16_t)(cpu->ram[0xa6a]|cpu->ram[0xa6b]<<8);
}
static void SetWarning(CpuState *cpu,unsigned value) {
  cpu->ram[0xa6a]=(uint8_t)value;cpu->ram[0xa6b]=(uint8_t)(value>>8);
}
static uint32 AlarmAt(uint32 pc,unsigned target) { return (pc&0xff0000u)|target; }

uint32 SmLowHealthAlarmHook(CpuState *cpu,uint32 pc) {
  if (cpu->m_flag) return 0;
  switch (pc&65535) {
    case 0xea98: /* Native low-health path, before reading warning state. */
      if (!alarm_muted && Warning(cpu)>1) {
        unsigned remaining=Warning(cpu)-1;SetWarning(cpu,remaining);
        if (remaining==1) return AlarmAt(pc,0xea8f); /* native stop-SFX tail */
      }
      break;
    case 0xea9d: /* New low-health episode, before native LDA #2/start SFX. */
      if (alarm_muted) return AlarmAt(pc,0xeaa4); /* set native flag, no sound */
      SetWarning(cpu,31);cpu->A=2; /* thirty following Samus updates */
      cpu->_flag_N=cpu->_flag_Z=0;cpu->P&=(uint8_t)~0x82;
      return AlarmAt(pc,0xea92); /* native queue call then return; retain timer */
    case 0xea92:
      if (alarm_muted) return AlarmAt(pc,0xeaaa); /* no alarm was started */
      break;
    case 0xf339: /* Native resume after a station, message or pause. */
      if (alarm_muted || Warning(cpu)==1) return AlarmAt(pc,0xf340);
      break;
  }
  return 0;
}
static void SmLowHealthAlarmActivate(void) {
  SmModValue value;
  alarm_muted=!strcmp(SmModOption(&value,"super-metroid.pacing","low-health-alarm","mode","short"),"muted");
  static const uint32_t hooks[]={0x90ea98,0x90ea9d,0x90ea92,0x90f339};
  SmModArmHooks("low-health-alarm",hooks,4);
}
SNES_MOD_CONSTRUCTOR(sm_register_low_health_alarm) {
  snes_mod_register_activation_plugin("super-metroid.pacing.low-health-alarm",SmLowHealthAlarmActivate);
}
