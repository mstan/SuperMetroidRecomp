#pragma once

/*
 * Options -- the third pause-menu tab (Map, Equipment, Options).
 *
 * Super Metroid's pause menu has room for exactly two tabs. Its L/R handler
 * hard-codes the Map<->Equipment pair, and its text is pre-composed tilemap
 * data DMA'd out of the ROM (EquipmentScreenTransferBG1Tilemap -> ram3800
 * copies fixed blobs), with no string compositor to hang a new screen off.
 * So the third tab is a host panel:
 *
 *   - The rows are a recomp-ui runtime-UI model (recomp_runtime_ui.h): it
 *     owns navigation, disabled rows and persistence. They edit g_sm_video
 *     (the same settings the launcher's Mods page edits, saved to
 *     SM_VIDEO_CONFIG_FILE) and, through host_api.h, the framework's own
 *     live display settings: fullscreen, vsync, frame blending, run-ahead
 *     and window scale. Those apply to the running window immediately, and
 *     the host writes config.ini at shutdown so they survive the restart
 *     and the launcher opens on what the player left. An Exit row asks the
 *     host to end the run gracefully (snesrecomp_request_exit): the host
 *     writes any config the panel dirtied and shuts down on its next loop
 *     pass, after the requesting frame has presented.
 *   - This module draws that model in the pause screen's style with the
 *     framework's 8x8-font overlay primitives (snes_overlay_draw.h).
 *   - SmPauseOptions_GuestTick() is the only guest-facing part. It runs at
 *     the entry of GameState_15_Paused_Async ($82:90E8, see
 *     HleSmGameState_15_Paused_Async), before the ROM's own routine, in both
 *     execution modes. No-pin model: the guest stays on whichever pause tab
 *     it was on (Map menu_index 0 or Equipment 1) and every button but Start
 *     is removed from the pad, so the ROM sees only Start (unpause).
 *   - Opening rides the ROM's tab-change effects on the screen it opens over:
 *     the 0x38 SFX and a host-driven screen fade (fast out, black, slow in)
 *     that reproduces the native tab-change effect byte for byte.
 *   - Leaving is the ROM's own: R/B/"Back to map" start the tab change the
 *     pause screen makes itself (PauseMenu_2's fade-out to Equipment,
 *     PauseMenu_5's to the Map), which the panel then rides, dimmed with the
 *     screen, and closes on. A same-tab shoulder is the ROM's total no-op and
 *     just closes the panel.
 *
 * Whether the panel is up changes what the guest does next frame, so it is
 * part of every snapshot (SmStateSaveExtra / SmExecStateSave). Whether it is
 * leaving is read from guest RAM. A file load reopens on the first row;
 * execution snapshots preserve the cursor so rollback cannot keep navigation
 * performed by a speculative frame.
 */

#include <stdbool.h>
#include <stdint.h>

/* One paused frame: read joypad1_newkeys, step the host's own enter fade
 * (a phase marker in the guest's screen_fade_delay word), drive the panel,
 * and open or close it on the shoulder presses. Must run before the ROM's
 * ReleaseButtonsFilter for the frame. */
void SmPauseOptions_GuestTick(void);

/* Opening resets the cursor to the first row; closing hides the panel. */
void SmPauseOptions_SetActive(bool active);
bool SmPauseOptions_IsActive(void);

/* Execution-snapshot cursor. File loads deliberately reopen on row zero. */
unsigned SmPauseOptions_Cursor(void);
bool SmPauseOptions_CursorValid(unsigned row);
void SmPauseOptions_RestoreCursor(unsigned row);

/* Close the panel once the guest has moved on (the fade it was leaving
 * through is over, or the game left the paused states). Called before each
 * frame and from the tick. */
void SmPauseOptions_SyncToGuest(void);

/* Whether to composite the panel over this frame. */
bool SmPauseOptions_Visible(void);

/* Composite the panel over an ARGB8888 frame, dimmed with the screen's
 * current brightness. stride is in pixels. */
void SmPauseOptions_Render(uint32_t *dst, int stride, int width, int height);
