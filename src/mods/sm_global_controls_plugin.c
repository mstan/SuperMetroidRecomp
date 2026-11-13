/* An optional, durable mod plan applies the native control words to every
 * gameplay file. Options participate in replay/netplay mod identity. No SRAM
 * is edited directly and disabled profiles leave native per-file choices. */
#include "sm_global_controls_plugin.h"
#include "sm_mod_support.h"
#include "mod_runtime.h"
#include <stdbool.h>
#include <string.h>

static bool enabled;
static uint16_t values[9];
static const unsigned addresses[]={0x9b2,0x9b4,0x9b6,0x9b8,0x9ba,0x9bc,0x9be,0x9e4,0x9ea};
static const char *const options[]={"shoot","jump","run","cancel","select","aim-down","aim-up"};
static const char *const defaults[]={"X","A","B","Y","Select","L","R"};
static uint16_t button(const char *name) {
  static const char *const names[]={"A","B","X","Y","L","R","Select"};
  static const uint16_t bits[]={0x80,0x8000,0x40,0x4000,0x20,0x10,0x2000};
  for (unsigned i=0;i<7;++i) if (!strcmp(name,names[i])) return bits[i];
  return 0;
}
void SmGlobalControlsApply(uint8_t ram[0x20000]) {
  if (!enabled || (ram[0x998]|ram[0x999]<<8)!=8) return;
  for (unsigned i=0;i<9;++i) {
    ram[addresses[i]]=(uint8_t)values[i];
    ram[addresses[i]+1]=(uint8_t)(values[i]>>8);
  }
}
static void activate(void) {
  SmModValue value;
  for (unsigned i=0;i<7;++i) {
    values[i]=button(SmModOption(&value,"super-metroid.controls","global-game-controls",options[i],defaults[i]));
    if (!values[i]) values[i]=button(defaults[i]);
  }
  values[7]=SmModOptionBool("super-metroid.controls","global-game-controls","moonwalk",false);
  values[8]=SmModOptionBool("super-metroid.controls","global-game-controls","auto-cancel",false);
  enabled=true;
}
static void reset(void) { enabled=false; }
SNES_MOD_CONSTRUCTOR(sm_register_global_controls) {
  snes_mod_register_reset_callback(reset);
  snes_mod_register_activation_plugin("super-metroid.controls.global-game-controls",activate);
}
