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
 * Vector_NMI) clears it. The interruptible scheduler yields at this polling
 * boundary, then injects NMI before resuming the original guest bytes. */
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
 * states also read to pick the screen they land on (0 Map, 2 Equipment).
 * The shoulder-press effects HandlePauseScreenLrInput writes ($82:A50C). */
#define menu_index (*(uint16*)(g_ram + 0x0727))
#define pausemenu_button_label_mode (*(uint16*)(g_ram + 0x0753))
#define pausemenu_start_lr_pressed_highlight_timer (*(uint16*)(g_ram + 0x0729))
#define pausemenu_shoulder_button_highlight (*(uint16*)(g_ram + 0x0751))

/* Input filter state written by ReleaseButtonsFilter ($80:8146), which the
 * pause screens read as newly_held_down_timed_held_input: a button counts
 * as newly held only once it has been stably held through the decay window
 * (the pause screen's "timed held" input), so e.g. the unpause Start edge
 * lands several frames after the press. */
#define joypad_released_keys (*(uint16*)(g_ram + 0x05D9))
#define timed_held_input_timer (*(uint16*)(g_ram + 0x05DB))
#define timed_held_input (*(uint16*)(g_ram + 0x05DF))
#define previous_timed_held_input (*(uint16*)(g_ram + 0x05E3))

/* PPU shadow register INIDISP (brightness 0-15, bit 7 force-blank), the one
 * the fade routines step; Vector_NMI (NmiUpdateIoRegisters, $80:9583)
 * copies it to PPU $2100 each frame. */
#define reg_INIDISP (*(uint8*)(g_ram + 0x0051))

/* Screen fade words stepped by HandleFadeOut/In ($80:8924/894D): the delay
 * word is the counter's reload value, so 0 steps INIDISP every frame (fast
 * tab-change fade) and 1 every other frame (slow unpause fade). */
#define screen_fade_delay (*(uint16*)(g_ram + 0x0723))
#define screen_fade_counter (*(uint16*)(g_ram + 0x0725))

/* SFX channel 1 queue as QueueSfx1_Internal ($80:9051) keeps it: a 16-slot
 * ring ($0656) with rp/wp, plus the one-byte max-queued marker. The guest
 * pumps it once per frame. */
#define sfx_readpos ((uint8*)(g_ram + 0x0643))
#define sfx_writepos ((uint8*)(g_ram + 0x0646))
#define sfx_max_queued ((uint8*)(g_ram + 0x0653))
#define sfx1_queue ((uint8*)(g_ram + 0x0656))

/* Gates QueueSfx1_Internal applies before writing the ring. */
#define power_bomb_explosion_status (*(uint16*)(g_ram + 0x0592))
#define debug_disable_sounds (*(uint16*)(g_ram + 0x05F5))

/* The main game-state machine counter (the recompiled code's game_state). */
#define game_state (*(uint16*)(g_ram + 0x0998))

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
