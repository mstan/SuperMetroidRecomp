#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "mod_runtime.h"
#include "sm_mod_support.h"
static const char *shoot="X";
int snes_mod_register_activation_plugin(const char *id,SNESModActivationCallback callback) { (void)id;(void)callback;return 1; }
int snes_mod_register_reset_callback(SNESModActivationCallback callback) { (void)callback;return 1; }
const char *SmModOption(SmModValue *out,const char *package,const char *feature,const char *option,const char *fallback) {
  (void)package;(void)feature;snprintf(out->text,sizeof(out->text),"%s",!strcmp(option,"shoot")?shoot:fallback);return out->text;
}
bool SmModOptionBool(const char *package,const char *feature,const char *option,bool fallback) {
  (void)package;(void)feature;(void)fallback;return !strcmp(option,"moonwalk");
}
#include "../src/mods/sm_global_controls_plugin.c"
static unsigned word(const uint8_t *ram,unsigned address) { return ram[address]|ram[address+1]<<8; }
int main(void) {
  static uint8_t ram[0x20000],before[0x20000];
  memset(ram,0x5a,sizeof(ram));memcpy(before,ram,sizeof(ram));
  SmGlobalControlsApply(ram);assert(!memcmp(ram,before,sizeof(ram)));
  activate();SmGlobalControlsApply(ram);assert(!memcmp(ram,before,sizeof(ram))); /* menus untouched */
  ram[0x998]=8;ram[0x999]=0;SmGlobalControlsApply(ram);
  const unsigned defaults[]={0x40,0x80,0x8000,0x4000,0x2000,0x20,0x10,1,0};
  for (unsigned i=0;i<9;++i) assert(word(ram,addresses[i])==defaults[i]);
  shoot="R";activate();ram[0x9b2]=0;SmGlobalControlsApply(ram);assert(word(ram,0x9b2)==0x10);
  /* A freshly loaded second file gets the same configured values. */
  memset(ram+0x9b2,0,14);SmGlobalControlsApply(ram);assert(word(ram,0x9b2)==0x10 && word(ram,0x9b4)==0x80);
  reset();ram[0x9b2]=0x80;SmGlobalControlsApply(ram);assert(word(ram,0x9b2)==0x80);
  puts("Global native controls: disabled/menu isolation, all native defaults, remapping, second file and reset pass.");
}
