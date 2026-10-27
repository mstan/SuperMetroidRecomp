/* Synthetic renderer fixtures supply a complete stock image; they never
 * claim hardware lines. Real deferred readback is checked against the full
 * PPU in the framework and by built-game capture/state comparisons. */
#include "snes/ppu.h"
#include <stdlib.h>
void ppu_drawCapturedLine(Ppu *p, int line, bool pixels) {
  (void)p; (void)line; (void)pixels;
  abort();
}
