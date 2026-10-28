/* Map views share read-only cartridge facts. Only the optional offline
 * minimap pause uses a guest hook; it skips the gameplay step at its normal
 * entry/epilogue and leaves interrupts/audio servicing to the stock runner. */
#include "sm_map_plugin.h"
#include "sm_map_view.h"
#include "sm_mod_overlay.h"
#include "sm_mod_support.h"
#include "../sm_pause_options.h"
#include "mod_hook.h"
#include "mod_runtime.h"
#include "common_rtl.h"
#include "netplay/snes_netplay.h"
#include "snes_overlay_draw.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PACKAGE "super-metroid.map-hud"
static SmWorld world;
static int16_t layout[SM_WORLD_ROOMS][2];
static bool ready,failed,area_enabled,world_enabled,mini_enabled;
static bool world_open,mini_open,mini_pause,registered;
static uint16_t previous;
static unsigned pan_tick,zoom=1;
static int center_x16,center_y16,mini_width=128;
static uint32_t *backdrop;
static size_t backdrop_count;
static int backdrop_width,backdrop_height;
static uint64_t timeline;
static const int scales[]={2,3,4,6,8,12,16};
static uint16_t W(const uint8_t *p,unsigned a) { return (uint16_t)(p[a]|p[a+1]<<8); }
static uint8_t Read(void *context,uint32_t at) {
  (void)context;
  /* The word helper requires both bytes to be mapped. For a bank's final
   * byte, read the preceding word so an unmapped $0000 cannot hide $FFFF. */
  return (at&0xffff)==0xffff ? (uint8_t)(SmModReadRom16(at-1)>>8) : (uint8_t)SmModReadRom16(at);
}
static bool Ensure(void) {
  if (ready || failed) return ready;
  if (!SmWorldLoad(&world,Read,NULL)) {
    failed=true; fprintf(stderr,"[mods] map model unavailable: %s\n",world.error); return false;
  }
  unsigned bends=SmWorldLayout(&world,layout); ready=true;
  fprintf(stderr,"[mods] map model: %u rooms, %u doors, %u states, %u markers; %u atlas connectors\n",
    world.room_count,world.door_count,world.state_count,world.marker_count,bends);
  return true;
}
static bool MapPage(const uint8_t *ram) {
  return W(ram,0x998)==15 && W(ram,0x727)==0 && !SmPauseOptions_Visible();
}
static void SyncTimeline(void) {
  uint64_t current=RtlStateGeneration();
  if (timeline==current) return;
  timeline=current; world_open=mini_open=false; previous=0; backdrop_width=backdrop_height=0;
}
bool SmMapFullScreen(const uint8_t *ram) { SyncTimeline(); return world_open && MapPage(ram); }
static bool Frozen(const uint8_t *ram) {
  SyncTimeline();
  return mini_enabled && mini_open && mini_pause && !snes_netplay_active() && W(ram,0x998)==8 && !W(ram,0xa78);
}
static void AtlasCenter(void) {
  int left=32767,top=32767,right=-32768,bottom=-32768;
  for (unsigned i=0; i<world.room_count; ++i) {
    const SmWorldRoom *r=&world.rooms[i];
    if (layout[i][0]<left) left=layout[i][0];
    if (layout[i][1]<top) top=layout[i][1];
    if (layout[i][0]+r->width>right) right=layout[i][0]+r->width;
    if (layout[i][1]+r->height>bottom) bottom=layout[i][1]+r->height;
  }
  center_x16=(left+right)*8; center_y16=(top+bottom)*8;
}
static uint16_t Input(uint16_t buttons,const SnesModInputContext *context) {
  if (context->player || !context->wram) return buttons;
  SyncTimeline();
  const uint8_t *ram=context->wram; uint16_t pressed=(uint16_t)(buttons&~previous);
  bool chord=(buttons&(SNES_PAD_SELECT|SNES_PAD_START))==(SNES_PAD_SELECT|SNES_PAD_START);
  bool old_chord=(previous&(SNES_PAD_SELECT|SNES_PAD_START))==(SNES_PAD_SELECT|SNES_PAD_START);
  previous=buttons;
  if (W(ram,0x998)==8 && !W(ram,0xa78) && mini_enabled) {
    world_open=false;
    if (chord && !old_chord && Ensure()) mini_open=!mini_open;
    if (mini_open && mini_pause && !snes_netplay_active()) return 0;
    return chord ? (uint16_t)(buttons&~(SNES_PAD_SELECT|SNES_PAD_START)) : buttons;
  }
  mini_open=false;
  if (!MapPage(ram) || !world_enabled) { world_open=false; return buttons; }
  if ((pressed&SNES_PAD_X) && Ensure()) {
    world_open=!world_open;
    if (world_open) { zoom=1; pan_tick=0; AtlasCenter(); }
    buttons=(uint16_t)(buttons&~SNES_PAD_X);
  }
  if (!world_open) return buttons;
  if (pressed&(SNES_PAD_B|SNES_PAD_START)) {
    world_open=false; return (uint16_t)(buttons&~SNES_PAD_B);
  }
  if ((pressed&SNES_PAD_L) && zoom) --zoom;
  if ((pressed&SNES_PAD_R) && zoom+1<sizeof(scales)/sizeof(scales[0])) ++zoom;
  if (pressed&SNES_PAD_Y) SmMapPlayerPoint(&world,layout,ram,true,&center_x16,&center_y16);
  if (++pan_tick%3==0 || (pressed&(SNES_PAD_UP|SNES_PAD_DOWN|SNES_PAD_LEFT|SNES_PAD_RIGHT))) {
    if (buttons&SNES_PAD_LEFT) center_x16-=16;
    if (buttons&SNES_PAD_RIGHT) center_x16+=16;
    if (buttons&SNES_PAD_UP) center_y16-=16;
    if (buttons&SNES_PAD_DOWN) center_y16+=16;
    if (center_x16 < -512) center_x16=-512;
    if (center_x16>2048) center_x16=2048;
    if (center_y16 < -512) center_y16=-512;
    if (center_y16>2048) center_y16=2048;
  }
  return (uint16_t)(buttons&~(SNES_PAD_X|SNES_PAD_Y|SNES_PAD_B|SNES_PAD_SELECT|
    SNES_PAD_L|SNES_PAD_R|SNES_PAD_UP|SNES_PAD_DOWN|SNES_PAD_LEFT|SNES_PAD_RIGHT));
}
/* Reveal-map is a gameplay mod; query its committed plan without changing
 * discovery bits. Empty values mean the reveal feature is disabled. */
static void Visibility(bool *reveal,bool *secrets) {
  *reveal=snes_mod_runtime_feature_enabled_c(PACKAGE,"reveal-map")!=0;
  SmModValue value; const char *choice=SmModOption(&value,PACKAGE,"reveal-map","secrets","hidden");
  *secrets=*reveal && !strcmp(choice,"shown");
}
static void Text(uint32_t *p,int stride,int height,int x,int y,const char *text,uint32_t color) {
  snes_ovl_draw_text(p,stride,height,x,y,text,color,1);
}
static void Backdrop(uint32_t *pixels,int stride,int width,int height,bool freeze) {
  size_t count=(size_t)width*(size_t)height;
  if (!freeze) {
    if (backdrop_count<count) {
      uint32_t *p=realloc(backdrop,count*sizeof(*p));
      if (!p) return;
      backdrop=p; backdrop_count=count;
    }
    for (int y=0; y<height; ++y) memcpy(backdrop+(size_t)y*width,pixels+(size_t)y*stride,(size_t)width*sizeof(*pixels));
    backdrop_width=width; backdrop_height=height;
  } else if (backdrop) {
    int copy_width=width<backdrop_width ? width : backdrop_width;
    int copy_height=height<backdrop_height ? height : backdrop_height;
    int old_x=(backdrop_width-copy_width)/2,new_x=(width-copy_width)/2;
    for (int y=0; y<copy_height; ++y)
      memcpy(pixels+(size_t)y*stride+new_x,backdrop+(size_t)y*backdrop_width+old_x,(size_t)copy_width*sizeof(*pixels));
  }
}
static void Draw(uint32_t *pixels,int stride,int width,int height,int stage_x,const uint8_t *ram) {
  SyncTimeline();
  if (mini_enabled && W(ram,0x998)==8) Backdrop(pixels,stride,width,height,Frozen(ram));
  bool map=MapPage(ram) && (ram[0x51]&0x8f)==15;
  if (!(map && (area_enabled||world_enabled)) && !(mini_enabled && mini_open && W(ram,0x998)==8)) return;
  if (!Ensure()) return;
  bool reveal,secrets; Visibility(&reveal,&secrets);
  if (mini_enabled && mini_open && W(ram,0x998)==8) {
    int x,y; SmMapPlayerPoint(&world,layout,ram,false,&x,&y);
    SmMapView v={stage_x+248-mini_width,54,mini_width,88,6,x,y,W(ram,0x79f),reveal,secrets,true};
    SmMapViewDraw(pixels,stride,height,&world,layout,Read,NULL,ram,v);
    snes_ovl_stroke_rect(pixels,stride,height,v.x,v.y,v.width,v.height,0xff58758d);
    Text(pixels,stride,height,v.x+4,v.y+4,Frozen(ram)?"MAP - PAUSED":"MAP - LIVE",0xfff1e6ad);
    Text(pixels,stride,height,v.x+4,v.y+v.height-10,"SEL+START CLOSE",0xffc3d2df);
    return;
  }
  if (world_open) {
    snes_ovl_fill_rect(pixels,stride,height,0,0,width,height,0xff080f1e);
    SmMapView v={4,40,width-8,height-68,scales[zoom],center_x16,center_y16,-1,reveal,secrets,false};
    SmMapViewDraw(pixels,stride,height,&world,layout,Read,NULL,ram,v);
    Text(pixels,stride,height,8,4,"ZEBES",0xfff0e2b2);
    const char *area=SmMapAreaName(W(ram,0x79f));
    Text(pixels,stride,height,width-8-(int)strlen(area)*8,4,area,SmMapAreaColor(W(ram,0x79f)));
    const unsigned legend[]={0,3,5,1,4,2};
    const int columns[]={8,88,192};
    for (unsigned i=0; i<6; ++i)
      Text(pixels,stride,height,columns[i%3],16+(int)(i/3)*10,SmMapAreaName(legend[i]),SmMapAreaColor(legend[i]));
    Text(pixels,stride,height,8,height-22,"L/R ZOOM  D-PAD PAN",0xffcbd5e1);
    Text(pixels,stride,height,8,height-12,"X/B BACK  Y SAMUS",0xffcbd5e1);
  } else if (area_enabled) {
    int sx=(int16_t)W(ram,0xb1),sy=(int16_t)W(ram,0xb3);
    snes_ovl_fill_rect(pixels,stride,height,stage_x,0,256,36,0xff080f1e);
    Text(pixels,stride,height,stage_x+8,8,SmMapAreaName(W(ram,0x79f)),0xfff0e2b2);
    if (world_enabled) Text(pixels,stride,height,stage_x+184,8,"X WORLD",0xffa9c7e8);
    Text(pixels,stride,height,stage_x+8,24,"M",0xffff65b8);
    Text(pixels,stride,height,stage_x+32,24,"S",0xff7dff50);
    Text(pixels,stride,height,stage_x+56,24,"PB",0xffffe64c);
    Text(pixels,stride,height,stage_x+96,24,"> ONE-WAY",0xffd2dce6);
    SmMapView v={stage_x+8,38,240,156,8,(128+sx)*2,(116+sy)*2,W(ram,0x79f),reveal,secrets,false};
    SmMapViewDraw(pixels,stride,height,&world,layout,Read,NULL,ram,v);
  } else {
    snes_ovl_fill_rect(pixels,stride,height,stage_x+176,24,72,12,0xff080f1e);
    Text(pixels,stride,height,stage_x+184,26,"X WORLD",0xffa9c7e8);
  }
}
uint32 SmMapPauseHook(CpuState *cpu,uint32 pc24) {
  return Frozen(cpu->ram) ? (pc24&0xff0000u)|0x8bb7 : 0;
}
static void Register(void) {
  if (registered) return;
  registered=true; snes_mod_register_input_transform(Input); SmModOverlayRegisterPriority(Draw,10);
}
static void AreaActivate(void) { area_enabled=true; Register(); }
static void WorldActivate(void) { world_enabled=true; Register(); }
static void MiniActivate(void) {
  SmModValue value; mini_enabled=true;
  mini_pause=!strcmp(SmModOption(&value,PACKAGE,"minimap","background","live"),"paused");
  mini_width=!strcmp(SmModOption(&value,PACKAGE,"minimap","size","small"),"large") ? 160 : 128;
  if (mini_pause) { static const uint32_t pc[]={0x828b47}; SmModArmHooks("minimap",pc,1); }
  Register();
}
static void Reset(void) {
  ready=failed=area_enabled=world_enabled=mini_enabled=world_open=mini_open=mini_pause=registered=false;
  previous=0; pan_tick=0; zoom=1; free(backdrop); backdrop=NULL; backdrop_count=0; backdrop_width=backdrop_height=0;
  timeline=RtlStateGeneration();
}
SNES_MOD_CONSTRUCTOR(sm_register_map_plugins) {
  snes_mod_register_reset_callback(Reset);
  snes_mod_register_activation_plugin("super-metroid.map-hud.pause-map",AreaActivate);
  snes_mod_register_activation_plugin("super-metroid.map-hud.world-map",WorldActivate);
  snes_mod_register_activation_plugin("super-metroid.map-hud.minimap",MiniActivate);
}
