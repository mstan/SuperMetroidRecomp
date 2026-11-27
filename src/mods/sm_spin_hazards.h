#pragma once
#include <stdbool.h>
#include <stdint.h>
/* Fire Geyser is an indestructible enemy body, rather than an ordinary
 * enemy projectile. Native touch consumes a charged spin even though it
 * cannot hurt the plume, leaving the next frame vulnerable. */
static inline bool SmSpinProtectsFireGeyser(const uint8_t *ram) {
  unsigned contact=ram[0xa6e] | ram[0xa6f]<<8;
  unsigned index=ram[0xe54] | ram[0xe55]<<8;
  if ((contact!=3 && contact!=4) || index>=2048 || (index&63)) return false;
  return (ram[0xf78+index] | ram[0xf79+index]<<8)==0xe07f;
}
bool SmCombatSpinHazardsActive(void);
