#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "sm_display.h"
#include "sm_renderer.h"
#include "sm_mod_overlay.h"
#include "sm_mod_support.h"
#include "mod_runtime.h"

static uint8_t image[0x90000], live[0x20000], shown[0x20000];
const uint8_t *g_rom = image;
static uint32_t pixels[342 * 224];
static bool use_shown = true;
static SmPlayerView player;
bool SmDisplay_GetPlayer(SmPlayerView *out) { *out = player; return use_shown; }
bool SmModOverlayRegister(SmModOverlayDraw draw) { (void)draw; return true; }
int snes_mod_register_presentation_plugin(const char *id, SNESModActivationCallback cb) {
  (void)id; (void)cb; return 1;
}
const char *SmModOption(SmModValue *out, const char *package, const char *feature,
                        const char *option, const char *fallback) {
  (void)package; (void)feature; (void)option; (void)fallback;
  strcpy(out->text,"always"); return out->text;
}
#include "../src/mods/sm_laser_sight_plugin.c"
static void put(uint8_t *ram, unsigned at, unsigned value) { ram[at]=value; ram[at+1]=value>>8; }
static void draw(int stage) {
  memset(pixels,0,sizeof(pixels)); SmLaserSightDraw(pixels,342,342,224,stage,live);
}
int main(void) {
  put(live,0x998,8); put(live,0x7a5,32); put(live,0x7a7,32);
  put(live,0xaf6,40); put(live,0xafa,140); put(live,0xa1c,1);
  memcpy(shown,live,sizeof(live));
  image[0x8b629+3]=1; image[0x8b629+8+3]=3;
  player=(SmPlayerView){shown,0,0,64,120,0,0};
  SmLaserSightActivate(); draw(43);
  /* Native standing up-right muzzle (13,-13), six pixels along the ray.
   * Live RAM deliberately describes a different pose and location. */
  const int x=83+43,y=101;
  assert(pixels[y*342+x]==0xffff3030u);
  assert(pixels[y*342+x-1]==0xffff3030u && pixels[y*342+x+1]==0xffff3030u);
  assert(pixels[(y-1)*342+x]==0xffff3030u && pixels[(y+1)*342+x]==0xffff3030u);
  assert(!pixels[150*342+59+43]);
  /* Thickness cannot paint into the first solid wall or over the HUD. */
  for (unsigned by=0;by<32;++by) put(shown,0x10002+2*(by*32+6),0x8000);
  draw(43);
  for (unsigned py=0;py<224;++py) for (unsigned px=96+43;px<112+43;++px)
    assert(!pixels[py*342+px]);
  memset(shown+0x10002,0,0x6400); player.y=60; draw(43);
  for (unsigned py=0;py<32;++py) for (unsigned px=0;px<342;++px) assert(!pixels[py*342+px]);
  image[0x8b629+3]=8; player.x=90; player.y=140; draw(0);
  assert(pixels[115*342+65]==0xffff3030u); /* native standing left-up (-19,-19) */
  player.movement=1; draw(0);
  assert(pixels[118*342+69]==0xffff3030u); /* native running left-up (-15,-16) */
  use_shown=false; draw(0);
  assert(pixels[150*342+59]==0xffff3030u); /* native renderer keeps the live path */
  assert(!memcmp(shown,live,0x10002));
  puts("sm_laser_test: displayed pose/origin, centered width, wall/HUD clipping and stock fallback PASS");
  return 0;
}
