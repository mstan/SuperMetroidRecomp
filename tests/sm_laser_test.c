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
static bool needs_mask;
static int occupied_x = -1, occupied_y = -1;
void SmRendererRequireSightMask(bool enabled) { needs_mask = enabled; }
bool SmRendererSightOccluded(int x, int y) { return x == occupied_x && y == occupied_y; }
int snes_mod_register_reset_callback(SNESModActivationCallback cb) { (void)cb; return 1; }
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
  assert(needs_mask);
  /* A visible projectile/cannon pixel keeps its exact existing color; the
   * adjacent transparent sprite pixel still admits the sight. */
  occupied_x=x; occupied_y=y; draw(43);
  assert(!pixels[y*342+x] && pixels[y*342+x+1]==0xffff3030u);
  occupied_x=occupied_y=-1;
  /* Per-frame native cannon offsets move the sight with the run cycle. */
  image[0x847df]=0x00;image[0x847e0]=0xd0;
  image[0x85000]=1; image[0x85002]=10;image[0x85003]=20;
  image[0x85004]=11;image[0x85005]=22;
  put(shown,0xa96,1); draw(43);
  assert(pixels[(y+2)*342+x+1]==0xffff3030u);
  put(shown,0xa96,0); image[0x847df]=image[0x847e0]=0;
  /* Native plain-run entries are empty sentinels, rather than an array
   * extending into the following pose. Gun-running supplies the same bob. */
  image[0x847df+9*2]=0xd9;image[0x847e0+9*2]=0xc9;
  image[0x847df+11*2]=0x00;image[0x847e0+11*2]=0xd0;
  int dx,dy;CannonMotion(9,1,&dx,&dy);assert(dx==1 && dy==2);
  /* A valid custom plain-run table takes precedence over that fallback. */
  image[0x847df+9*2]=0x00;image[0x847e0+9*2]=0xd0;
  CannonMotion(9,1,&dx,&dy);assert(dx==1 && dy==2);
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
  SmLaserSightReset();assert(!needs_mask);
  puts("sm_laser_test: displayed pose/origin, centered width, wall/HUD clipping and stock fallback PASS");
  return 0;
}
