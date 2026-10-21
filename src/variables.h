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

/* g_ram is declared by snesrecomp/runner/src/cpu/common_rtl.h. */

/* Host-protocol frame counter (framework-shaped, not game-specific). */
extern uint16 counter_global_frames;

/* Super Metroid's vblank handshake lives at $7E:05B4 (decomp:
 * `waiting_for_nmi`, src/variables.h `g_ram+0x5B4`). WaitForNMI
 * ($80:8338) sets it to 1 and spins until the NMI handler ($80:9583,
 * Vector_NMI) clears it. In the recompiled build there is no mid-frame
 * NMI, so that spin never terminates — WaitForNMI is HLE-replaced by a
 * host-fiber yield (HleSmWaitForNmi -> sm_host_yield in sm_rtl.c). */
#define waiting_for_nmi (*(uint8*)(g_ram + 0x05B4))

/* Pad state written by ReadJoypadInputs ($80:9459): lastkeys is this frame's
 * held sample, newkeys the newly-pressed edge. ReleaseButtonsFilter
 * ($80:8146) derives newly_held_down_timed_held_input from them, and the
 * pause screens read that. */
#define joypad1_lastkeys (*(uint16*)(g_ram + 0x008B))
#define joypad1_newkeys (*(uint16*)(g_ram + 0x008F))
#define newly_held_down_timed_held_input (*(uint16*)(g_ram + 0x05E1))

/* Pause menu. menu_index selects kPauseMenuFuncs ($82:90FF);
 * pausemenu_button_label_mode is the lit L/R label, which the Load/FadeIn
 * states also read to pick the screen they land on (0 Map, 2 Equipment). */
#define menu_index (*(uint16*)(g_ram + 0x0727))
#define pausemenu_button_label_mode (*(uint16*)(g_ram + 0x0753))

/* Button bits in joypad1_newkeys/lastkeys: the SNES auto-joypad order the ROM
 * reads (decomp: src/ida_types.h `enum Buttons`). */
#define kButton_R       0x0010
#define kButton_L       0x0020
#define kButton_X       0x0040
#define kButton_A       0x0080
#define kButton_Right   0x0100
#define kButton_Left    0x0200
#define kButton_Down    0x0400
#define kButton_Up      0x0800
#define kButton_Start   0x1000
#define kButton_Select  0x2000
#define kButton_Y       0x4000
#define kButton_B       0x8000

#endif /* VARIABLES_H */
