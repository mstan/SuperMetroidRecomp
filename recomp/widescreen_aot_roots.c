#include "cpu_state.h"
#include "funcs.h"

/* Generator-only roots for the runtime-gated widescreen override injector.
 *
 * These functions may be absent from the attract coverage profile, but
 * tools/apply_widescreen_overrides.py needs their exact generated blocks.
 * tools/regen.sh scans this file; CMake does not compile it into the game. */
void SmWidescreenOverrideAotRoots(CpuState *cpu) {
  DetermineWhichEnemiesToProcess(cpu);
  CheckIfEnemyIsOnScreen(cpu);
  EnemyWithNormalSpritesIsOffScreen(cpu);
}
