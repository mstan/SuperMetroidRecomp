/* Game-specific RAM variable declarations for Super Metroid.
 *
 * Scaffold: just the framework-protocol variables the host
 * orchestration in sm_rtl.c needs. The recompiled gen code refers to
 * memory by raw address; named regions can be added as needed (the
 * snesrev/sm decomp's src/variables.h is the authoritative RAM map).
 */

#ifndef VARIABLES_H
#define VARIABLES_H

#include "types.h"

/* g_ram is declared by snesrecomp/runner/src/common_rtl.h. */

/* Host-protocol frame counter (framework-shaped, not game-specific). */
extern uint16 counter_global_frames;

/* Super Metroid's vblank handshake lives at $7E:05B4 (decomp:
 * `waiting_for_nmi`, src/variables.h `g_ram+0x5B4`). WaitForNMI
 * ($80:8338) sets it to 1 and spins until the NMI handler ($80:9583,
 * Vector_NMI) clears it. In the recompiled build there is no mid-frame
 * NMI, so that spin never terminates — WaitForNMI is HLE-replaced by a
 * host-fiber yield (HleSmWaitForNmi -> sm_host_yield in sm_rtl.c). */
#define waiting_for_nmi (*(uint8*)(g_ram + 0x05B4))

#endif /* VARIABLES_H */
