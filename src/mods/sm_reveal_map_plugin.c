/* super-metroid.map-hud / reveal-map: every area's map is shown as if its
 * map station had been used, optionally including secret rooms (Super
 * Metroid Redux widescreen bonus "Display the entire blue map").
 *
 *   - $80:85B1 STA has_area_map ($0789) in LoadMirrorOfExploredMapTiles:
 *     A becomes nonzero, so the HUD minimap shows unexplored tiles.
 *   - $82:947F BNE in LoadPauseMenuMapTilemap (the map-station test):
 *     Z is cleared, so the pause map takes the station path.
 *   - $82:94EF BCS in the same loop (a tile missing from the station data):
 *     C is set, so the tile is drawn anyway; this shows secret rooms.
 * Explored-tile and station bits in WRAM/SRAM are never written, so turning
 * the feature off restores the normal map. Hooks in recomp/bank00.cfg and
 * recomp/bank02.cfg.
 */

#include <stdio.h>
#include <string.h>

#include "mod_hook.h"
#include "mod_runtime.h"
#include "sm_mod_support.h"

uint32 SmModRevealMapHasMap(CpuState *cpu, uint32 pc24) {
  (void)pc24;
  cpu->A |= 1;
  return 0;
}
uint32 SmModRevealMapStation(CpuState *cpu, uint32 pc24) {
  (void)pc24;
  cpu->_flag_Z = 0;
  return 0;
}
uint32 SmModRevealMapSecrets(CpuState *cpu, uint32 pc24) {
  (void)pc24;
  cpu->_flag_C = 1;
  return 0;
}

static void SmRevealMapActivate(void) {
  SmModValue secrets;
  bool all = !strcmp(SmModOption(&secrets, "super-metroid.map-hud", "reveal-map",
                                 "secrets", "hidden"), "shown");
  int armed = snes_mod_hook_arm(0x8085B1, true) + snes_mod_hook_arm(0x82947F, true);
  if (all) armed += snes_mod_hook_arm(0x8294EF, true);
  fprintf(stderr, "[mods] reveal map (secret rooms %s): %d hooks\n", all ? "shown" : "hidden", armed);
}

static void SmRevealMapReset(void) {
  snes_mod_hook_arm(0x8085B1, false);
  snes_mod_hook_arm(0x82947F, false);
  snes_mod_hook_arm(0x8294EF, false);
}

SNES_MOD_CONSTRUCTOR(sm_register_reveal_map_plugin) {
  snes_mod_register_reset_callback(SmRevealMapReset);
  snes_mod_register_activation_plugin("super-metroid.map-hud.reveal-map",
                                      SmRevealMapActivate);
}
