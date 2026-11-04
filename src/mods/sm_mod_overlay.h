/* Host-drawn overlays for Super Metroid mods (radial weapon ring, HUD
 * readouts, map views).
 *
 * Presentation only: a draw callback reads guest WRAM and draws on top of the
 * composed frame; it never writes guest state. Register during a plugin's
 * activation; every registration is cleared before the next activation, so a
 * disabled feature draws nothing. src/main.c composes the overlays after the
 * renderer and below the pause-menu Options panel.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

/* `pixels` is the ARGB frame, `stride` pixels per row, `width` x `height`
 * its size, and `stage_x` the x of the 256-pixel stock picture inside it
 * (non-zero when widescreen margins are shown). */
typedef void (*SmModOverlayDraw)(uint32_t *pixels, int stride, int width, int height,
                                 int stage_x, const uint8_t *wram);

bool SmModOverlayRegister(SmModOverlayDraw draw);
/* Higher priorities draw later; equal priorities retain registration order. */
bool SmModOverlayRegisterPriority(SmModOverlayDraw draw, int priority);
bool SmModOverlayActive(void);
void SmModOverlayRender(uint32_t *pixels, int stride, int width, int height, int stage_x);
