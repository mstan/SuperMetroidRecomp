#include "sm_renderer.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint8_t ram[0x20000];
static Ppu ppu;
static uint32_t stock[256 * 224], output[SM_MAX_WIDTH * 224];
static uint8_t rom[0x1b0000];
static void romword(unsigned address, unsigned value) {
  rom[address] = (uint8_t)value; rom[address + 1] = (uint8_t)(value >> 8);
}
static void word(unsigned address, unsigned value) {
  ram[address] = (uint8_t)value;
  ram[address + 1] = (uint8_t)(value >> 8);
}

/* Most synthetic fixtures intentionally use identical pre/post RAM. Make
 * that explicit while preserving fixtures that supply distinct owner RAM. */
static bool fixture_owner_latched;
static void ResetFixtureRenderer(void) {
  SmRendererReset(); fixture_owner_latched = false;
}
static void LatchFixtureState(const uint8_t *owner) {
  SmRendererLatchObjectState(owner); fixture_owner_latched = true;
}
static void BeginFixtureFrame(const uint8_t *state, unsigned number) {
  if (!fixture_owner_latched) SmRendererLatchObjectState(state);
  SmRendererBeginFrame(state, number); fixture_owner_latched = false;
}

#ifndef SM_RASTER_FULL_COPY
#define SM_RASTER_FULL_COPY 0
#endif
static void unexpected_capture_callback(Ppu *p,uint y,bool sub,void *context) {
  (void)p;(void)y;(void)sub;(void)context;assert(false);
}
static void raster_storage(void) {
  ResetFixtureRenderer();SmRendererSetRom(NULL,0);
  memset(ram,0,sizeof(ram));memset(&ppu,0,sizeof(ppu));
  word(0x998,8);ppu.inidisp=15;ppu.bgmode=1;ppu.screenEnabled[0]=1;
  ppu.bgTileAdr=2;ppu.cgram[1]=31;ppu.cgram[2]=31<<5;ppu.cgram[3]=31<<10;
  for(unsigned row=0;row<8;++row)ppu.vram[0x2000+row]=0xff;
  BeginFixtureFrame(ram,100);
  for(unsigned line=1;line<=224;++line) {
    if(line==65 || line==129)
      for(unsigned row=0;row<8;++row)ppu.vram[0x2000+row]=line==65?0xff00:0xff;
    if(line==193)ppu.vram[0x7fff]=1;
    SmRendererCaptureLine(&ppu,line);
  }
  assert(SmRendererEndFrame(stock));
  SmRasterStats storage=SmRendererGetRasterStats();
  assert(storage.vram_images==(SM_RASTER_FULL_COPY?224u:4u));
  assert(storage.vram_copied_bytes==(uint64_t)storage.vram_images*65536);
  memset(ppu.vram,0,sizeof(ppu.vram));
  SmViewport view={342,43,16.0/9,true};
  for(unsigned draw=0;draw<3;++draw) {
    assert(SmRendererDraw(output,view,false,1));
    for(unsigned y=0;y<224;++y)
      assert(output[y*view.width+view.extra+128]==
          (y>=64 && y<128?0x00ff00u:0xff0000u));
  }
  for(unsigned row=0;row<8;++row)ppu.vram[0x2000+row]=0xffff;
  BeginFixtureFrame(ram,101);
  SmRendererBeginHardwareRaster(&ppu);
  for(unsigned line=1;line<=224;++line) {
    if(line==65) { ppu.vram[0x7fff]=2;++ppu.vramWriteCount; }
    if(line==129) { ppu.vram[0x7fff]=3;SmRendererHardwareVramChanged(); }
    if(line==193)ppu.widescreenLineEnhancer=unexpected_capture_callback;
    if(line==194) { ppu.widescreenLineEnhancer=NULL;ppu.vram[0x7fff]=4; }
    SmRendererCaptureLine(&ppu,line);
  }
  SmRendererEndHardwareRaster();
  assert(SmRendererEndFrame(stock));storage=SmRendererGetRasterStats();
  assert(storage.vram_images==(SM_RASTER_FULL_COPY?224u:4u));
  assert(storage.vram_capacity_bytes==(uint64_t)(SM_RASTER_FULL_COPY?448u:8u)*65536);
  if(!SM_RASTER_FULL_COPY)assert(storage.vram_compared_bytes==
#if SM_RASTER_TRACK_WRITES && !SNESRECOMP_TRACE
      34u*65536);
#else
      223u*65536);
#endif
  memset(ppu.vram,0,sizeof(ppu.vram));
  assert(SmRendererDraw(output,view,false,1));
  for(unsigned y=0;y<224;++y)assert(output[y*view.width+view.extra+128]==0x0000ff);
  BeginFixtureFrame(ram,102);
  for(unsigned line=1;line<=224;++line)SmRendererCaptureLine(&ppu,line);
  assert(SmRendererEndFrame(stock));
  assert(SmRendererGetRasterStats().vram_capacity_bytes==storage.vram_capacity_bytes);
  ResetFixtureRenderer();memset(ram,0,sizeof(ram));memset(&ppu,0,sizeof(ppu));
}

static void pause_menu_fade(void) {
  /* State 16 still draws the equipment/map menu while it fades out. State
   * 17 restores gameplay graphics under forced blank; only 18 fades them in.
   * Stale room RAM must not turn either menu into widened room tiles. */
  const unsigned states[] = {14, 15, 16, 17};
  const SmViewport views[] = {{256, 0, 4.0 / 3, false},
      {342, 43, 16.0 / 9, true}, {448, 96, 21.0 / 9, true},
      {682, 213, 32.0 / 9, true}};
  memset(ram, 0, sizeof(ram)); memset(&ppu, 0, sizeof(ppu));
  ppu.inidisp = 15; ppu.bgmode = 1; ppu.screenEnabled[0] = 1;
  ppu.bgTileAdr = 1; ppu.cgram[1] = 31;
  for (unsigned y = 0; y < 8; ++y) ppu.vram[4096 + y] = 255;
  for (unsigned y = 0; y < 224; ++y)
    for (unsigned x = 0; x < 256; ++x)
      stock[y * 256 + x] = 0x123456 ^ (y << 16) ^ (x << 8);
  for (unsigned s = 0; s < sizeof(states) / sizeof(states[0]); ++s) {
    ResetFixtureRenderer(); word(0x998, states[s]);
    for (unsigned frame = 1; frame <= 2; ++frame) {
      BeginFixtureFrame(ram, frame);
      for (unsigned y = 1; y <= 224; ++y) SmRendererCaptureLine(&ppu, y);
      assert(SmRendererEndFrame(stock));
    }
    for (unsigned v = 0; v < sizeof(views) / sizeof(views[0]); ++v)
      for (unsigned anchored = 0; anchored <= 1; ++anchored)
        for (unsigned phase = 0; phase <= 2; ++phase) {
          SmViewport view = views[v];
          assert(SmRendererDraw(output, view, anchored != 0, phase * 0.5));
          for (unsigned y = 0; y < 224; ++y)
            for (int x = 0; x < view.width; ++x)
              assert(output[y * view.width + x] ==
                  (x >= view.extra && x < view.extra + 256 ?
                   stock[y * 256 + x - view.extra] : 0));
          assert(SmRendererGetStats().stock_lines == 224);
        }
  }
  ResetFixtureRenderer(); memset(ram, 0, sizeof(ram));
  memset(&ppu, 0, sizeof(ppu)); memset(stock, 0, sizeof(stock));
}

static void hud_colour_math_margins(void) {
  /* HUD/dialogue rows stay screen-space through boss/message scrolling.
   * Liquid FX in the second page of $5A remain genuine world content. */
  const unsigned maps[] = {0x58, 0x5a, 0x5c};
  const SmViewport views[] = {{256, 0, 4.0 / 3, false},
      {342, 43, 16.0 / 9, true}, {448, 96, 21.0 / 9, true},
      {682, 213, 32.0 / 9, true}};
  for (unsigned map = 0; map < 3; ++map) {
    ResetFixtureRenderer(); memset(ram, 0, sizeof(ram));
    memset(&ppu, 0, sizeof(ppu)); memset(stock, 0, sizeof(stock));
    word(0x998, 8);
    ppu.inidisp = 15; ppu.bgmode = 1; ppu.bgTileAdr = 0x400;
    ppu.bgXsc[2] = maps[map];
    ppu.screenEnabled[0] = 1; ppu.screenEnabled[1] = 4;
    ppu.cgadsub = 32; ppu.cgwsel = 2; ppu.cgram[1] = 0x7fff;
    /* A transparent subscreen selects the pink power-bomb/boss COLDATA.
     * The cleared HUD plane must instead supply actual opaque black. */
    ppu.fixedColor = map == 1 ? 31 | (31 << 10) : 0;
    unsigned base = (ppu.bgXsc[2] & 0xfc) * 256;
    for (unsigned tile = 0; tile < 2048; ++tile) ppu.vram[base + tile] = 1;
    for (unsigned y = 0; y < 8; ++y) ppu.vram[0x4008 + y] = 255;
    BeginFixtureFrame(ram, 1);
    for (unsigned y = 1; y <= 224; ++y) {
      ppu.hScroll[2] = y < 64 ? 0 : 108;
      ppu.vScroll[2] = y < 128 ? 0 : 384;
      SmRendererCaptureLine(&ppu, y);
    }
    assert(SmRendererEndFrame(stock));
    for (unsigned v = 0; v < sizeof(views) / sizeof(views[0]); ++v) {
      SmViewport view = views[v];
      assert(SmRendererDraw(output, view, true, 1));
      for (unsigned y = 30; y < 224; ++y)
        for (int x = 0; x < view.width; ++x) {
          bool native = x >= view.extra && x < view.extra + 256;
          assert(output[y * view.width + x] ==
              (native || map == 2 ? 0xffffffu : 0));
        }
    }
  }
  ResetFixtureRenderer(); memset(ram, 0, sizeof(ram));
  memset(&ppu, 0, sizeof(ppu)); memset(stock, 0, sizeof(stock));
}

static void shared_hud_liquid_map(void) {
  const SmViewport views[] = {{256, 0, 4.0 / 3, false}, {342, 43, 16.0 / 9, true},
      {448, 96, 21.0 / 9, true}, {682, 213, 32.0 / 9, true}};
  ResetFixtureRenderer(); memset(ram, 0, sizeof(ram)); memset(&ppu, 0, sizeof(ppu));
  word(0x998, 8); word(0x7a5, 128); word(0x7a7, 16); word(0x911, 768);
  ppu.inidisp = 15; ppu.bgmode = 1; ppu.bgTileAdr = 0x400; ppu.bgXsc[2] = 0x5a;
  ppu.screenEnabled[0] = 4; ppu.cgram[1] = 0x7fff; ppu.cgram[2] = 31 << 5;
  for (unsigned tile = 0; tile < 1024; ++tile) {
    ppu.vram[0x5800 + tile] = 1;  /* UI page, white */
    ppu.vram[0x5c00 + tile] = 2;  /* FX page, green */
  }
  for (unsigned y = 0; y < 8; ++y) {
    ppu.vram[0x4008 + y] = 255; ppu.vram[0x4010 + y] = 0xff00;
  }
  BeginFixtureFrame(ram, 1);
  for (unsigned y = 1; y <= 224; ++y) {
    ppu.hScroll[2] = 773; ppu.vScroll[2] = y < 192 ? 0 : 55;
    SmRendererCaptureLine(&ppu, y);
  }
  assert(SmRendererEndFrame(stock));
  for (unsigned v = 0; v < sizeof(views) / sizeof(views[0]); ++v) {
    SmViewport view = views[v]; assert(SmRendererDraw(output, view, false, 1));
    for (int x = 0; x < view.width; ++x) {
      bool native = x >= view.extra && x < view.extra + 256;
      assert(output[199 * view.width + x] == (native ? 0xffffffu : 0));
      assert(output[200 * view.width + x] == 0x00ff00);
      assert(output[223 * view.width + x] == 0x00ff00);
    }
  }
  /* The ROM uploads FX beginning at $5BE0, not $5C00. Preserve the
   * eight-pixel surface/bubble row while keeping earlier HUD rows black. */
  word(0x1964, 0x8000);
  BeginFixtureFrame(ram, 2);
  for (unsigned y = 1; y <= 224; ++y) {
    ppu.hScroll[2] = 773; ppu.vScroll[2] = y < 192 ? 0 : 55;
    SmRendererCaptureLine(&ppu, y);
  }
  assert(SmRendererEndFrame(stock));
  for (unsigned v = 0; v < sizeof(views) / sizeof(views[0]); ++v) {
    SmViewport view = views[v]; assert(SmRendererDraw(output, view, false, 1));
    for (int x = 0; x < view.width; ++x) {
      bool native = x >= view.extra && x < view.extra + 256;
      assert(output[190 * view.width + x] == (native ? 0xffffffu : 0));
      assert(output[199 * view.width + x] == 0xffffffu);
      assert(output[200 * view.width + x] == 0x00ff00);
    }
  }
  ResetFixtureRenderer(); memset(ram, 0, sizeof(ram)); memset(&ppu, 0, sizeof(ppu));
}

static void eye_beam_margins(void) {
  const SmViewport view = {342, 43, 16.0 / 9, true};
  for (unsigned fault = 0; fault < 7; ++fault) {
    ResetFixtureRenderer(); memset(ram, 0, sizeof(ram));
    memset(&ppu, 0, sizeof(ppu)); memset(rom, 0, sizeof(rom));
    SmRendererSetRom(rom, sizeof(rom)); word(0x998, 8);
    word(0x7a5, 32); word(0x7a7, 32);
    word(0xfb8, 0xe6bf); word(0xfba, 128); word(0xfbe, 180); word(0xfee, 32);
    rom[0x1066bf + 12] = fault == 1 ? 0xa7 : 0xa8;
    romword(0x1066bf + 18, 0x9058);
    word(0x18c0, 0x8820); word(0x18f0, fault == 2 ? 0x84b8 : 0xea3c);
    word(0x192c, 4);
    romword(0x8c9d4 + 28 * 2, 128); romword(0x8c9d4 + 36 * 2, 384);
    /* Independent half/three-halves Q8 edge walks, aimed up/right. */
    for (int row = 0; row < 230; ++row) {
      int distance = 179 - row;
      unsigned l = distance >= 0 ? 128 + distance / 2 : 255;
      unsigned r = distance >= 0 ? 128 + distance * 3 / 2 : 0;
      word(0x9100 + row * 2, l | (r > 255 ? 255 : r) << 8);
    }
    if (fault == 3) word(0x9100 + 100 * 2, 0);
    ppu.inidisp = 15; ppu.bgmode = 1; ppu.bgTileAdr = 1;
    ppu.screenEnabled[0] = 1; ppu.cgadsub = 1; ppu.cgwsel = 0x20;
    ppu.windowsel = 2u << 20; ppu.fixedColor = 31 << 10; ppu.cgram[1] = 31;
    for (unsigned row = 0; row < 8; ++row) ppu.vram[4096 + row] = 255;
    if (fault == 5) {
      LatchFixtureState(ram);
      word(0xfee, 96); /* Enemy changed its angle after HDMA ran. */
    }
    if (fault == 6) SmRendererBeginFrame(ram, 1); /* Missing owner latch. */
    else BeginFixtureFrame(ram, 1);
    for (unsigned y = 1; y <= 224; ++y) {
      unsigned bounds = ram[0x9100 + y * 2] | ram[0x9101 + y * 2] << 8;
      ppu.window1left = fault == 4 ? 0 : bounds & 255;
      ppu.window1right = fault == 4 ? 0 : bounds >> 8;
      SmRendererCaptureLine(&ppu, y);
    }
    assert(SmRendererEndFrame(stock));
    assert(SmRendererDraw(output, view, false, 1));
    bool valid = fault == 0 || fault == 5;
    assert(SmRendererGetStats().eye_beam_lines == (valid ? 224 : 0));
    if (valid) {
      assert(output[90 * view.width + view.extra + 270] == 0xff00ff);
      assert(output[70 * view.width + view.extra + 270] == 0xff0000);
      assert(output[90 * view.width + view.extra + 200] == 0xff0000);
    }
  }
  ResetFixtureRenderer(); SmRendererSetRom(NULL, 0);
  memset(ram, 0, sizeof(ram)); memset(&ppu, 0, sizeof(ppu));
  memset(rom, 0, sizeof(rom)); memset(stock, 0, sizeof(stock));
}

static void eye_beam_clamped_tail(void) {
  const SmViewport views[] = {{256,0,4.0/3,false},{342,43,16.0/9,true},
      {448,96,21.0/9,true},{682,213,32.0/9,true}};
  for (unsigned mirror=0;mirror<2;++mirror) {
    ResetFixtureRenderer(); memset(ram,0,sizeof(ram));
    memset(&ppu,0,sizeof(ppu)); memset(rom,0,sizeof(rom));
    SmRendererSetRom(rom,sizeof(rom)); word(0x998,8);
    word(0x7a5,128); word(0x7a7,32); word(0x911,1030);
    word(0xfb8,0xe6bf); word(0xfba,mirror?1000:1316); word(0xfbe,72);
    word(0xfee,mirror?74:186);
    rom[0x1066bf+12]=0xa8; romword(0x1066bf+18,0x9058);
    word(0x18c0,0x8820); word(0x18f0,0xea3c); word(0x192c,4);
    romword(0x8c9d4+(mirror?70:54)*2,mirror?1536:512);
    romword(0x8c9d4+(mirror?78:62)*2,mirror?512:1536);
    /* Independent integer slopes: downward cone edges advance 2 and 6
     * pixels/row. Offscreen ROM walks saturate entire tails to $0000/$FFFF. */
    for(int row=0;row<230;++row) {
      int d=row-71,l=mirror?-30+2*d:286-6*d,r=mirror?-30+6*d:286-2*d;
      unsigned bounds=0x00ff;
      if(d>0) bounds=r<0?0:l>255?0xffff:(unsigned)(l<0?0:l)|((unsigned)(r>255?255:r)<<8);
      word(0x9100+row*2,bounds);
    }
    ppu.inidisp=15; ppu.bgmode=1; ppu.bgTileAdr=1;
    ppu.screenEnabled[0]=1; ppu.cgadsub=1; ppu.cgwsel=0x20;
    ppu.windowsel=2u<<20; ppu.fixedColor=31<<10; ppu.cgram[1]=31;
    for(unsigned row=0;row<8;++row) ppu.vram[4096+row]=255;
    BeginFixtureFrame(ram,1);
    for(unsigned y=1;y<=224;++y) {
      unsigned b=ram[0x9100+y*2]|ram[0x9101+y*2]<<8;
      ppu.window1left=b&255; ppu.window1right=b>>8;
      SmRendererCaptureLine(&ppu,y);
    }
    assert(SmRendererEndFrame(stock));
    uint32_t native[256*224];
    assert(SmRendererDraw(native,views[0],false,1));
    for(unsigned v=0;v<4;++v) {
      SmViewport view=views[v]; assert(SmRendererDraw(output,view,false,1));
      assert(SmRendererGetStats().eye_beam_lines==224);
      for(unsigned y=0;y<224;++y)
        assert(!memcmp(native+y*256,output+y*view.width+view.extra,256*sizeof(*native)));
      if(!view.extra)continue;
      int outside=mirror?266:-10,inside=mirror?290:-30;
      assert(output[223*view.width+view.extra+outside]==0xff00ff);
      assert(output[223*view.width+view.extra+inside]==0xff0000);
    }
  }
  ResetFixtureRenderer(); SmRendererSetRom(NULL,0);
  memset(ram,0,sizeof(ram)); memset(&ppu,0,sizeof(ppu)); memset(rom,0,sizeof(rom));
}

static void static_background_room_edges(void) {
  const SmViewport view = {342, 43, 16.0 / 9, true};
  for (unsigned camera = 0; camera < 2; ++camera) {
    ResetFixtureRenderer(); memset(ram, 0, sizeof(ram)); memset(&ppu, 0, sizeof(ppu));
    word(0x998, 8); word(0x7a5, 32); word(0x7a7, 32);
    word(0x911, camera * 256); word(0x91b, 1); /* Static/parallax BG2. */
    ppu.inidisp = 15; ppu.bgmode = 1; ppu.screenEnabled[0] = 2;
    ppu.bgXsc[1] = 0x48; ppu.bgTileAdr = 0x10; ppu.cgram[1] = 31 << 5;
    for (unsigned y = 0; y < 8; ++y) ppu.vram[4096 + y] = 255;
    BeginFixtureFrame(ram, 1);
    for (unsigned y = 1; y <= 224; ++y) SmRendererCaptureLine(&ppu, y);
    assert(SmRendererEndFrame(stock)); assert(SmRendererDraw(output, view, false, 1));
    assert(output[100 * view.width + view.extra + 255] == 0x00ff00);
    assert(output[100 * view.width + view.extra - 1] == (camera ? 0x00ff00 : 0));
    assert(output[100 * view.width + view.extra + 256] == (camera ? 0 : 0x00ff00));
  }
  ResetFixtureRenderer(); memset(ram, 0, sizeof(ram)); memset(&ppu, 0, sizeof(ppu));
}

static void bg2_object_canvas(void) {
  const SmViewport view = {342, 43, 16.0 / 9, true};
  for (unsigned tagged = 0; tagged < 2; ++tagged) {
    ResetFixtureRenderer(); memset(ram, 0, sizeof(ram)); memset(&ppu, 0, sizeof(ppu));
    memset(rom, 0, sizeof(rom)); SmRendererSetRom(rom, sizeof(rom));
    word(0x998, 8); word(0x7a5, 128); word(0x7a7, 32);
    word(0x911, 256); word(0x91b, 0x101);
    word(0xf78, 0x9000); word(0xf7a, 320); word(0xf7e, 128);
    word(0xf88, 4); word(0xf8e, 0x8000); ram[0xfa6] = 0xa2;
    romword(0x110000, 1); romword(0x110006, 0x9000);
    romword(0x111000, tagged ? 0xfffe : 1);
    ppu.inidisp = 15; ppu.bgmode = 1; ppu.screenEnabled[0] = 2;
    ppu.bgXsc[1] = 0x48; ppu.bgTileAdr = 0x10; ppu.cgram[1] = 31 << 5;
    for (unsigned row = 0; row < 8; ++row) ppu.vram[4096 + row] = 255;
    for (unsigned scroll = 0; scroll < 2; ++scroll) {
      ppu.hScroll[1] = scroll ? 224 : 0;
      BeginFixtureFrame(ram, scroll + 1);
      for (unsigned y = 1; y <= 224; ++y) SmRendererCaptureLine(&ppu, y);
      assert(SmRendererEndFrame(stock)); assert(SmRendererDraw(output, view, false, 1));
      for (int x = 0; x < view.width; ++x) {
        int native_x = x - view.extra;
        bool native = native_x >= 0 && native_x < 256;
        bool canvas = native_x >= (scroll ? 32 : 0) && native_x < (scroll ? 288 : 256);
        assert(output[100 * view.width + x] ==
            (native || !tagged || canvas ? 0x00ff00u : 0));
      }
    }
  }
  ResetFixtureRenderer(); SmRendererSetRom(NULL, 0);
  memset(ram, 0, sizeof(ram)); memset(&ppu, 0, sizeof(ppu)); memset(rom, 0, sizeof(rom));
}

static void crocomire_parked_bg2(void) {
  const SmViewport views[] = {{256, 0, 4.0 / 3, false}, {342, 43, 16.0 / 9, true},
      {448, 96, 21.0 / 9, true}, {682, 213, 32.0 / 9, true}};
  const int positions[][2] = {{-300, -292}, {-160, -152}, {340, 348},
      {400, 408}, {700, 708}, {333, 341}, {341, 333}, {-136, -128}, {-128, -136}};
  for (unsigned i = 0; i < sizeof(positions) / sizeof(positions[0]); ++i) {
    ResetFixtureRenderer(); memset(ram, 0, sizeof(ram)); memset(&ppu, 0, sizeof(ppu));
    memset(rom, 0, sizeof(rom)); SmRendererSetRom(rom, sizeof(rom));
    word(0x998, 8); word(0x7a5, 128); word(0x7a7, 16);
    word(0x911, 768); word(0x91b, 0x101);
    word(0xf78, 0xddbf); word(0xf7e, 120); word(0xf88, 4);
    word(0xf8e, 0x8000); ram[0xfa6] = 0xa4;
    rom[0x105dbf + 12] = 0xa4;
    romword(0x105dbf + 18, 0x8a5a); romword(0x105dbf + 24, 0x8c04);
    romword(0x120000, 1); romword(0x120006, 0x9000); romword(0x121000, 0xfffe);
    ppu.inidisp = 15; ppu.bgmode = 1; ppu.screenEnabled[0] = 2;
    ppu.bgXsc[1] = 0x49; ppu.bgTileAdr = 0x10; ppu.cgram[1] = 31;
    ppu.hScroll[1] = 256; ppu.vram[0x4800 + 12 * 32] = 1;
    for (unsigned y = 0; y < 8; ++y) ppu.vram[4096 + 16 + y] = 255;
    for (unsigned frame = 1; frame <= 2; ++frame) {
      int owner_x = positions[i][frame - 1];
      word(0xf7a, 768 + owner_x);
      ppu.hScroll[1] = owner_x >= 335 || owner_x < -128 ? 256 : (51 - owner_x) & 1023;
      BeginFixtureFrame(ram, frame);
      for (unsigned y = 1; y <= 224; ++y) SmRendererCaptureLine(&ppu, y);
      assert(SmRendererEndFrame(stock));
    }
    for (unsigned v = 0; v < sizeof(views) / sizeof(views[0]); ++v)
      for (unsigned phase = 0; phase <= 2; ++phase) {
        SmViewport view = views[v]; assert(SmRendererDraw(output, view, false, phase * .5));
        int left = positions[i][0] + (positions[i][1] - positions[i][0]) * (int)phase / 2 - 51;
        for (int x = 0; x < view.width; ++x) {
          int native_x = x - view.extra;
          bool body = native_x >= left && native_x < left + 8;
          assert(output[100 * view.width + x] == (body ? 0xff0000u : 0));
        }
      }
  }
  ResetFixtureRenderer(); SmRendererSetRom(NULL, 0);
  memset(ram, 0, sizeof(ram)); memset(&ppu, 0, sizeof(ppu)); memset(rom, 0, sizeof(rom));
}

static void parallax_register_aliases(void) {
  const SmViewport view = {682, 213, 32.0 / 9, true};
  ResetFixtureRenderer(); memset(ram, 0, sizeof(ram)); memset(&ppu, 0, sizeof(ppu));
  word(0x998, 8); word(0x7a5, 96); word(0x7a7, 32);
  word(0x911, 512); word(0x917, 384); word(0x91b, 192); word(0x923, 768);
  ppu.inidisp = 15; ppu.bgmode = 1; ppu.screenEnabled[0] = 2;
  ppu.bgXsc[1] = 0x49; ppu.bgTileAdr = 0x10; ppu.cgram[1] = 31 << 5;
  for (unsigned y = 0; y < 8; ++y) ppu.vram[4096 + y] = 255;
  /* Actual Norfair $AA0E uses VOFS256 against offset768. Those are the
   * same row of its 256-high map, not a world-Y displacement of -512. */
  for (unsigned alias = 0; alias < 4; ++alias) {
    ppu.hScroll[1] = 384 + (alias & 1 ? 512 : 0);
    ppu.vScroll[1] = alias & 2 ? 768 : 256;
    BeginFixtureFrame(ram, alias + 1);
    for (unsigned y = 1; y <= 224; ++y) SmRendererCaptureLine(&ppu, y);
    assert(SmRendererEndFrame(stock));
    for (unsigned phase = 0; phase <= 2; ++phase) {
      assert(SmRendererDraw(output, view, false, phase * .5));
      for (int x = 0; x < view.width; ++x)
        assert(output[100 * view.width + x] == 0x00ff00);
    }
  }
  ResetFixtureRenderer(); memset(ram, 0, sizeof(ram)); memset(&ppu, 0, sizeof(ppu));
}

static void door_layout_handover(void) {
  const SmViewport view = {342, 43, 16.0 / 9, true};
  for (unsigned layer = 0; layer < 2; ++layer) {
    ResetFixtureRenderer(); memset(ram, 0, sizeof(ram)); memset(&ppu, 0, sizeof(ppu));
    word(0x7a5, 64); word(0x7a7, 32); word(0x911, 256); word(0x917, 256);
    ppu.inidisp = 15; ppu.bgmode = 1; ppu.screenEnabled[0] = 1u << layer;
    ppu.bgXsc[layer] = 0x48; ppu.bgTileAdr = 0x11; ppu.cgram[1] = 31 << 5;
    for (unsigned row = 0; row < 8; ++row) ppu.vram[4096 + 16 + row] = 255;
    for (unsigned q = 0; q < 4; ++q) word(0xa000 + 2 * q, 1);
    /* VRAM retains only the departing door; the decompressed destination
     * would paint the entire left margin if sampled with the old camera. */
    ppu.vram[0x4800 + 12 * 32 + 30] = 1;
    for (unsigned frame = 1; frame <= 3; ++frame) {
      word(0x998, frame == 3 ? 8 : 11);
      word(0x99c, frame == 2 ? 0xe3c0 : 0xe38e);
      BeginFixtureFrame(ram, frame);
      for (unsigned y = 1; y <= 224; ++y) SmRendererCaptureLine(&ppu, y);
      assert(SmRendererEndFrame(stock));
      assert(SmRendererDraw(output, view, false, 1));
      assert(output[100 * view.width + view.extra + 240] == 0x00ff00);
      assert(output[100 * view.width + view.extra - 1] == (frame == 1 ? 0 : 0x00ff00));
    }
  }
  ResetFixtureRenderer(); memset(ram, 0, sizeof(ram)); memset(&ppu, 0, sizeof(ppu));
}

static void room_effect_matte(void) {
  const SmViewport view = {342, 43, 16.0 / 9, true};
  ResetFixtureRenderer(); memset(ram, 0, sizeof(ram)); memset(&ppu, 0, sizeof(ppu));
  word(0x998, 8); word(0x7a5, 16); word(0x7a7, 16);
  ppu.inidisp = 15; ppu.bgmode = 1; ppu.cgadsub = 32; ppu.fixedColor = 31;
  for (unsigned frame = 1; frame <= 2; ++frame) {
    word(0x911, frame == 1 ? 0 : 4); ppu.hScroll[0] = frame == 1 ? 0 : 4;
    BeginFixtureFrame(ram, frame);
    for (unsigned y = 1; y <= 224; ++y) SmRendererCaptureLine(&ppu, y);
    assert(SmRendererEndFrame(stock));
  }
  for (unsigned phase = 0; phase < 3; ++phase) {
    assert(SmRendererDraw(output, view, false, phase * 0.5));
    assert(output[100 * view.width + view.extra] == 0xff0000);
    assert(output[100 * view.width + view.extra - 2] == (phase ? 0xff0000u : 0));
    assert(output[100 * view.width] == 0);
    assert(output[100 * view.width + view.width - 1] == 0);
  }
  /* Door cameras may briefly be negative; unsigned wrap must not hide
   * genuine room content in the wider right side. */
  word(0x911, 0xfff0); ppu.hScroll[0] = 0xfff0;
  BeginFixtureFrame(ram, 3);
  for (unsigned y = 1; y <= 224; ++y) SmRendererCaptureLine(&ppu, y);
  assert(SmRendererEndFrame(stock)); assert(SmRendererDraw(output, view, false, 1));
  assert(output[100 * view.width + view.extra - 1] == 0);
  assert(output[100 * view.width + view.extra + 256] == 0xff0000);
  assert(output[100 * view.width + view.width - 1] == 0);
  ResetFixtureRenderer(); memset(ram, 0, sizeof(ram)); memset(&ppu, 0, sizeof(ppu));
}

static void cannon_motion(void) {
  const SmViewport view = {342, 43, 16.0 / 9, true};
  for (unsigned fault = 0; fault < 7; ++fault) {
    ResetFixtureRenderer(); memset(ram, 0, sizeof(ram)); memset(&ppu, 0, sizeof(ppu));
    memset(rom, 0, sizeof(rom)); SmRendererSetRom(rom, sizeof(rom));
    word(0x998, 8); word(0xaa8, fault == 2 ? 0 : 3); word(0xaac, fault == 1 ? 0 : 2);
    word(0xa1c, fault == 6 ? 253 : 0);
    romword(0x847df, 0xd000); romword(0x84791, 0x281f);
    rom[0x85000] = fault == 5 ? 10 : 0;
    rom[0x85002] = (uint8_t)-8; rom[0x85003] = (uint8_t)-12; rom[0x8b62d] = 8;
    ppu.inidisp = 15; ppu.bgmode = 1; ppu.screenEnabled[0] = 16;
    ppu.cgram[193] = 31 << 10;
    for (unsigned y = 0; y < 8; ++y) ppu.vram[31 * 16 + y] = 255;
    for (unsigned frame = 1; frame <= 2; ++frame) {
      unsigned x = frame == 1 ? 50 : 58;
      word(0xaf6, x + 8); word(0xafa, 80); word(0xb04, x + 8); word(0xb06, 72);
      ppu.oam[0] = (uint16_t)(x | (60 << 8));
      ppu.oam[1] = fault == 3 ? 0x681f : 0x281f;
      ppu.highOam[0] = fault == 4 ? 2 : 0;
      BeginFixtureFrame(ram, frame);
      for (unsigned y = 1; y <= 224; ++y) SmRendererCaptureLine(&ppu, y);
      assert(SmRendererEndFrame(stock));
    }
    assert(SmRendererDraw(output, view, false, 0.5));
    assert((SmRendererGetStats().matched_samus_samples != 0) == (fault == 0));
    assert(output[60 * view.width + view.extra + 54] == (fault == 0 ? 0x0000ffu : 0));
    assert(SmRendererDraw(output, view, false, 1));
    assert(output[60 * view.width + view.extra + 54] == 0);
  }
  ResetFixtureRenderer(); SmRendererSetRom(NULL, 0);
  memset(ram, 0, sizeof(ram)); memset(&ppu, 0, sizeof(ppu)); memset(rom, 0, sizeof(rom));
}

static void nonrectangular_room_padding(void) {
  const SmViewport views[] = {{256,0,4.0/3,false}, {342,43,16.0/9,true},
      {448,96,21.0/9,true}, {682,213,32.0/9,true}};
  for (unsigned real_content = 0; real_content < 4; ++real_content) {
    ResetFixtureRenderer(); memset(ram,0,sizeof(ram)); memset(&ppu,0,sizeof(ppu));
    word(0x998,8); word(0x7a5,32); word(0x7a7,32);
    for (unsigned by=0;by<16;++by)
      for (unsigned bx=0;bx<16;++bx) word(0x10002+2*(by*32+bx),0x83ff);
    /* Even one nonpadding block preserves this screen, including content
     * outside today's view. Do not infer visibility from scroll permission. */
    if (real_content == 1) word(0x10002,0x8000);
    if (real_content >= 2) {
      for (unsigned by=16;by<32;++by)
        for (unsigned bx=0;bx<16;++bx) word(0x10002+2*(by*32+bx),0x8000);
      if (real_content == 3) word(0x10002+2*(16*32),0x8001);
    }
    ppu.inidisp=15; ppu.bgmode=1; ppu.screenEnabled[0]=1;
    ppu.cgadsub=32; ppu.fixedColor=31;
    for (unsigned frame=1;frame<=2;++frame) {
      word(0x911,frame==1?254:258); ppu.hScroll[0]=frame==1?254:258;
      BeginFixtureFrame(ram,frame);
      for(unsigned y=1;y<=224;++y)SmRendererCaptureLine(&ppu,y);
      assert(SmRendererEndFrame(stock));
    }
    for (unsigned v=0;v<sizeof(views)/sizeof(views[0]);++v)
      for (unsigned phase=0;phase<3;++phase) {
        SmViewport view=views[v];
        assert(SmRendererDraw(output,view,false,phase*.5));
        for(int x=0;x<256;++x) assert(output[100*view.width+view.extra+x]==0xff0000);
        for(int x=0;x<view.extra;++x) {
          int world_x=254+(int)phase*2+x-view.extra;
          assert(output[100*view.width+x]==
              (real_content == 1 || real_content == 2 || world_x>=256 ? 0xff0000u : 0));
        }
      }
  }
  ResetFixtureRenderer(); memset(ram,0,sizeof(ram)); memset(&ppu,0,sizeof(ppu));
}

int main(int argc, char **argv) {
  pause_menu_fade();
  hud_colour_math_margins();
  shared_hud_liquid_map();
  eye_beam_margins();
  eye_beam_clamped_tail();
  static_background_room_edges();
  parallax_register_aliases();
  door_layout_handover();
  bg2_object_canvas();
  crocomire_parked_bg2();
  room_effect_matte();
  nonrectangular_room_padding();
  cannon_motion();
  raster_storage();
  /* Reveal substitutions, row-major multi-block writes, linked BTS blocks,
   * item collection and room-state overrides, all without guest execution. */
  static uint16_t revealed[0x3200];
  const unsigned reveal_functions[] = {0xcf36,0xcf3e,0xcf4e,0xcf62,0xcf6f};
  for (unsigned kind = 0; kind < 5; ++kind) {
    memset(ram, 0, sizeof(ram)); memset(rom, 0, sizeof(rom));
    SmRendererSetRom(rom, sizeof(rom));
    word(0x7a5, 4); word(0x7a7, 4); word(0x79f, 1);
    for (unsigned i = 0; i < 16; ++i) word(0x10002 + 2 * i, 0xc01);
    word(0x10002 + 2 * 5, 0xb000);
    romword(0x8d2d6, 0xb000); romword(0x8d2d8, 0xe000); romword(0x8d2da, 0xffff);
    romword(0x8e000, 0xff00); romword(0x8e002, 0xe100);
    romword(0x8e100, reveal_functions[kind]);
    for (unsigned i = 0; i < 4; ++i) romword(0x8e102 + 2 * i, 20 + i);
    assert(SmRendererXrayBlocks(ram, revealed));
    assert(revealed[0] == 0xc01 && revealed[5] == 0x1014);
    assert(revealed[6] == (kind == 2 || kind == 4 ? 0x1015 : 0xc01));
    assert(revealed[9] == (kind == 3 ? 0x1014 : kind == 4 ? 0x1016 : 0xc01));
    assert(revealed[10] == (kind == 4 ? 0x1017 : 0xc01));
    if (kind == 1) {
      word(0x79f, 0); assert(SmRendererXrayBlocks(ram, revealed)); assert(revealed[5] == 0);
    }
  }
  memset(ram, 0, sizeof(ram)); memset(rom, 0, sizeof(rom));
  word(0x7a5, 4); word(0x7a7, 4);
  romword(0x8d2d6, 0x5000); romword(0x8d2d8, 0xe000);
  romword(0x8d2da, 0x3000); romword(0x8d2dc, 0xe010); romword(0x8d2de, 0xffff);
  romword(0x8e000, 0xff00); romword(0x8e002, 0xe100);
  romword(0x8e010, 0xff00); romword(0x8e012, 0xe110);
  romword(0x8e100, 0xcebb); romword(0x8e110, 0xcf36); romword(0x8e112, 77);
  word(0x10002, 0x5000); ram[0x16402] = 1;
  word(0x10004, 0x5000); ram[0x16403] = 1;
  word(0x10006, 0x3000);
  assert(SmRendererXrayBlocks(ram, revealed));
  assert(revealed[0] == 0x104d && revealed[1] == 0x104d && revealed[2] == 0x104d);
  ram[0x16402] = 255;
  assert(SmRendererXrayBlocks(ram, revealed)); assert(revealed[0] == 0x10ff);
  word(0x1c37, 0xdf89); word(0x1c87, 6); word(0x1dc7, 9);
  romword(0x2039d, 0xe200); /* $84:839D item drawing table */
  romword(0x26202, 0x855);
  assert(SmRendererXrayBlocks(ram, revealed)); assert(revealed[3] == 0x1855);
  ram[0xd871] = 2;
  assert(SmRendererXrayBlocks(ram, revealed)); assert(revealed[3] == 0);
  word(0x7bb, 0xe300); romword(0x7e310, 0xe400);
  rom[0x7e400] = 3; rom[0x7e401] = 1; romword(0x7e402, 0x856);
  assert(SmRendererXrayBlocks(ram, revealed)); assert(revealed[7] == 0x1856);
  /* $91:CE79/CEBB changes axes at vertical extensions, and switches back
   * at negative horizontal extensions only. Keep this retail asymmetry. */
  for (unsigned scenario = 0; scenario < 6; ++scenario) {
    memset(ram, 0, sizeof(ram)); memset(rom, 0, sizeof(rom));
    word(0x7a5, 4); word(0x7a7, 4);
    const unsigned types[] = {0xd000, 0x5000, 0x3000};
    const unsigned functions[] = {0xce79, 0xcebb, 0xcf36};
    for (unsigned i = 0; i < 3; ++i) {
      romword(0x8d2d6 + 4 * i, types[i]); romword(0x8d2d8 + 4 * i, 0xe000 + 16 * i);
      romword(0x8e000 + 16 * i, 0xff00); romword(0x8e002 + 16 * i, 0xe100 + 16 * i);
      romword(0x8e100 + 16 * i, functions[i]); romword(0x8e102 + 16 * i, 77);
    }
    romword(0x8d2e2, 0xffff);
    word(0x10002 + 2 * 5, 0xd001); ram[0x16402 + 5] = 1;
    if (scenario == 0) { /* Plain vertical chain. */
      word(0x10002 + 2 * 9, 0xd002); ram[0x16402 + 9] = 1;
      word(0x10002 + 2 * 13, 0x3000);
    } else if (scenario == 1) { /* Switch to negative horizontal. */
      word(0x10002 + 2 * 9, 0x5002); ram[0x16402 + 9] = 255;
      word(0x10002 + 2 * 8, 0x3000);
    } else if (scenario == 2) { /* Positive horizontal keeps the old axis. */
      word(0x10002 + 2 * 9, 0x5002); ram[0x16402 + 9] = 1;
      word(0x10002 + 2 * 13, 0x3000);
    } else if (scenario == 3) { /* A cycle must not hang presentation. */
      word(0x10002 + 2 * 9, 0xd002); ram[0x16402 + 9] = 255;
    } else if (scenario == 4) { /* Out-of-room positive coordinate. */
      ram[0x16402 + 5] = 127;
    } else { /* Zero-step extension is unresolved, not a reveal target. */
      word(0x10002 + 2 * 9, 0xd002);
    }
    assert(SmRendererXrayBlocks(ram, revealed));
    assert(revealed[5] == (scenario < 3 ? 0x104d : scenario == 4 ? 0x10ff : 1));
  }
  memset(ram, 0, sizeof(ram)); memset(rom, 0, sizeof(rom));
  SmRendererSetRom(NULL, 0);
  /* Cardinal cones with symmetric synthetic Q8 slopes. Incremental edge
   * walking is an independent check of the closed-form unclipped bounds. */
  uint16_t tangent[129];
  for (unsigned i = 0; i < 129; ++i) tangent[i] = (uint16_t)(256 * (i <= 64 ? i : 128 - i));
  int32_t beam_left[230], beam_right[230];
  for (unsigned direction = 0; direction < 4; ++direction) {
    for (unsigned spread = 0; spread <= 10; ++spread) {
      assert(SmRendererXrayBounds(tangent, 128, 115, direction * 64, spread, beam_left, beam_right));
      for (int row = 0; row < 230; ++row) {
        int distance = abs(row - 114), l = 1, r = 0;
        if (direction == 1 || direction == 3) {
          if (spread || !distance) {
            int walk = 128;
            for (int step = 0; step < distance; ++step) walk += direction == 1 ? 64 - (int)spread : (int)spread - 64;
            l = direction == 1 ? walk : -32768;
            r = direction == 1 ? 32768 : walk;
          }
        } else if ((direction == 0 && row <= 114) || (direction == 2 && row >= 114)) {
          l = r = 128;
          for (int step = 0; step < distance; ++step) { l -= (int)spread; r += (int)spread; }
        }
        assert(beam_left[row] == l && beam_right[row] == r);
      }
    }
  }
  beam_left[0] = 12345;
  assert(!SmRendererXrayBounds(tangent, -257, 115, 64, 10, beam_left, beam_right));
  assert(!SmRendererXrayBounds(tangent, 128, 0, 64, 10, beam_left, beam_right));
  assert(!SmRendererXrayBounds(tangent, 128, 115, 257, 10, beam_left, beam_right));
  assert(beam_left[0] == 12345);
  for (unsigned side = 0; side < 2; ++side) {
    int source = side ? 272 : -16;
    assert(SmRendererXrayBounds(tangent, source, 115, side ? 192 : 64, 10, beam_left, beam_right));
    for (int row = 0; row < 230; ++row) {
      int steps = row < 115 ? 115 - row : row - 114;
      int edge = source;
      for (int step = 0; step < steps; ++step) edge += side ? -54 : 54;
      assert(beam_left[row] == (side ? -32768 : edge));
      assert(beam_right[row] == (side ? edge : 32768));
    }
    assert(SmRendererXrayBounds(tangent, source, 115, side ? 192 : 64, 0, beam_left, beam_right));
    assert(beam_left[114] == -32768 && beam_right[114] == 32768);
    assert(beam_left[113] > beam_right[113] && beam_left[115] > beam_right[115]);
  }
  /* The power-bomb profile uses fixed-point products and inclusive segments,
   * not an analytic circle. Independently select the last segment covering
   * each row to verify endpoint overwrite, collapsed segments and tail fill. */
  uint8_t profile[160] = {0};
  int16_t widths[192];
  for (unsigned i = 0; i < 32; ++i) {
    profile[96 + i] = (uint8_t)(i * 8);
    profile[128 + i] = (uint8_t)(191 - i * 6);
  }
  const char *power_bomb_rom = getenv("SM_TEST_POWERBOMB_ROM");
  for (unsigned dataset = 0; dataset < (power_bomb_rom ? 2u : 1u); ++dataset) {
    if (dataset) {
      FILE *file = fopen(power_bomb_rom, "rb");
      assert(file && fseek(file, 0x42206, SEEK_SET) == 0);
      assert(fread(profile, sizeof(profile), 1, file) == 1);
      assert(fclose(file) == 0);
    }
    for (unsigned radius = 0; radius < 256; ++radius) {
      assert(SmRendererPowerBombWidths(profile, radius, widths));
      for (unsigned row = 0; row < 192; ++row) {
        int expected = -1;
        for (unsigned i = 0; i < 32; ++i) {
          unsigned high = radius * profile[128 + (i ? i - 1 : 0)] / 256;
          unsigned low = radius * profile[128 + i] / 256;
          if (row >= low && row <= high) expected = (int)(radius * profile[96 + i] / 256);
        }
        if (row < radius * profile[159] / 256) expected = (int)(radius * profile[127] / 256);
        assert(widths[row] == expected);
      }
    }
  }
  int16_t unchanged[192];
  memcpy(unchanged, widths, sizeof(widths));
  assert(!SmRendererPowerBombWidths(profile, 256, widths));
  profile[140] = 255; /* Non-monotonic target rows cannot enter a wrap loop. */
  assert(!SmRendererPowerBombWidths(profile, 255, widths));
  assert(!memcmp(widths, unchanged, sizeof(widths)));
  uint16_t tile = 0;
  word(0x7a5, 64); word(0x7a7, 32);
  word(0x10002, 1);
  for (unsigned i = 0; i < 4; ++i) word(0xa008 + i * 2, 0x100 + i);
  for (unsigned flip = 0; flip < 4; ++flip) {
    word(0x10002, 1 | flip << 10);
    for (unsigned q = 0; q < 4; ++q) {
      assert(SmRendererRoomTile(ram, (q & 1) * 8, (q >> 1) * 8, &tile));
      assert(tile == ((0x100 + (q ^ flip)) ^ (flip << 14)));
    }
  }
  assert(!SmRendererRoomTile(ram, -1, 0, &tile));
  assert(!SmRendererRoomTile(ram, 1024, 0, &tile));
  assert(!SmRendererRoomTile(ram, 0, 512, &tile));
  word(0x19602, 1);
  assert(SmRendererRoomLayerTile(ram, 1, 0, 0, &tile));
  assert(tile == 0x100);
  assert(!SmRendererRoomLayerTile(ram, 2, 0, 0, &tile));
  word(0x7a5, 65535); word(0x7a7, 65535);
  assert(!SmRendererRoomTile(ram, 1024, 1024, &tile));

  SmViewport view = {682, 213, 32.0 / 9, true};
  ResetFixtureRenderer();
  assert(!SmRendererDraw(output, view, true, 1));
  BeginFixtureFrame(ram, 1);
  assert(!SmRendererEndFrame(stock));
  /* Ordered capture is mandatory; a missing line cannot expose stale data. */
  SmRendererCaptureLine(&ppu, 2);
  assert(!SmRendererEndFrame(stock));
  for (unsigned y = 1; y <= 224; ++y) SmRendererCaptureLine(&ppu, y);
  stock[0] = 0x123456;
  assert(SmRendererEndFrame(stock));
  memset(stock, 0, sizeof(stock));
  assert(SmRendererDraw(output, view, true, 1));
  assert(output[view.extra] == 0x123456);
  assert(output[0] == 0);
  assert(SmRendererGetStats().stock_lines == 224);
  assert(SmRendererGetStats().custom_lines == 0);

  /* Palette and VRAM changes must affect only subsequently captured lines. */
  memset(ram, 0, sizeof(ram));
  word(0x998, 8);
  ppu.bgmode = 1; ppu.inidisp = 15; ppu.screenEnabled[0] = 1;
  ppu.bgTileAdr = 1;
  ppu.cgram[1] = 31;
  for (unsigned y = 0; y < 8; ++y) ppu.vram[4096 + y] = 255;
  BeginFixtureFrame(ram, 2);
  for (unsigned y = 1; y <= 224; ++y) {
    if (y == 101) ppu.cgram[1] = 31 << 5;
    if (y == 151) memset(ppu.vram, 0, sizeof(ppu.vram));
    SmRendererCaptureLine(&ppu, y);
  }
  assert(SmRendererEndFrame(stock));
  memset(&ppu, 0, sizeof(ppu));
  memset(ram, 0, sizeof(ram));
  assert(SmRendererDraw(output, view, true, 1));
  assert(output[50 * view.width + view.extra] == 0xff0000);
  assert(output[110 * view.width + view.extra] == 0x00ff00);
  assert(output[160 * view.width + view.extra] == 0);
  assert(SmRendererGetStats().custom_lines == 224);
  assert(SmRendererGetStats().stock_lines == 0);
  uint32_t reference = output[50 * view.width + view.extra];
  for (unsigned n = 0; n < 4; ++n) {
    assert(SmRendererDraw(output, view, true, 1));
    assert(output[50 * view.width + view.extra] == reference);
  }

  /* A ROM spritemap may draw beyond native X=255 without activating its AI.
   * The same snapshot's status band must move all three HUD groups together. */
  memset(ram, 0, sizeof(ram));
  memset(&ppu, 0, sizeof(ppu));
  word(0x998, 8);
  word(0xf78, 0xd07f); word(0xf7a, 300); word(0xf7e, 60);
  word(0xf8e, 0x8000); ram[0xfa6] = 0xa2;
  rom[0x110000] = 1; /* one entry, tile 0 at origin */
  SmRendererSetRom(rom, sizeof(rom));
  ppu.inidisp = 15; ppu.bgmode = 1; ppu.screenEnabled[0] = 16;
  ppu.cgram[129] = 31 << 10;
  for (unsigned y = 0; y < 8; ++y) ppu.vram[y] = 255;
  for (unsigned x = 0; x < 256; ++x) stock[x] = x + 1;
  BeginFixtureFrame(ram, 3);
  for (unsigned y = 1; y <= 224; ++y) {
    ppu.screenEnabled[0] = y <= 30 ? 4 : 16;
    SmRendererCaptureLine(&ppu, y);
  }
  assert(SmRendererEndFrame(stock));
  assert(SmRendererDraw(output, view, true, 1));
  assert(output[60 * view.width + view.extra + 300] == 0x0000ff);
  assert(output[0] == 1 && output[79] == 80);
  assert(output[80 + view.extra] == 81);
  assert(output[208 + 2 * view.extra] == 209);
  assert(output[view.width - 1] == 256);
  assert(SmRendererGetStats().hud_lines == 30);
  assert(SmRendererGetStats().custom_lines == 194);
  assert(output[100] == 0);
  assert(SmRendererDraw(output, view, false, 1));
  assert(output[view.extra] == 1 && output[0] == 0);
  assert(ram[0xf7a] == (300 & 255) && ram[0xf7b] == (300 >> 8));
  /* Deleted/invisible flags are authoritative, regardless of live map data. */
  word(0xf86, 0x100);
  BeginFixtureFrame(ram, 4);
  for (unsigned y = 1; y <= 224; ++y) SmRendererCaptureLine(&ppu, y);
  assert(SmRendererEndFrame(stock));
  assert(SmRendererDraw(output, view, true, 1));
  assert(output[60 * view.width + view.extra + 300] == 0);

  /* Samus interpolation requires both OAM entries to match ROM map data. */
  memset(ram, 0, sizeof(ram));
  memset(&ppu, 0, sizeof(ppu));
  word(0x998, 8);
  rom[0x9008d] = 0; rom[0x9008e] = 0x90; /* map index 0 -> $92:9000 */
  rom[0x91000] = 1;
  ppu.inidisp = 15; ppu.bgmode = 1; ppu.screenEnabled[0] = 16;
  ppu.cgram[129] = 31 << 10;
  for (unsigned y = 0; y < 8; ++y) ppu.vram[y] = 255;
  for (unsigned frame = 5; frame <= 6; ++frame) {
    unsigned x = frame == 5 ? 50 : 58;
    word(0xb04, x); word(0xb06, 60);
    ppu.oam[0] = (uint16_t)(60 * 256 + x);
    LatchFixtureState(ram);
    /* Game logic has already advanced by scanout; OAM still describes the
     * latched pre-NMI owner. Matching against this new position is wrong. */
    word(0xb04, x + 100);
    BeginFixtureFrame(ram, frame);
    for (unsigned y = 1; y <= 224; ++y) SmRendererCaptureLine(&ppu, y);
    assert(SmRendererEndFrame(stock));
  }
  assert(SmRendererDraw(output, view, true, 0.5));
  assert(SmRendererGetStats().matched_object_samples > 0);
  assert(output[60 * view.width + view.extra + 50] == 0);
  assert(output[60 * view.width + view.extra + 54] == 0x0000ff);
  assert(output[60 * view.width + view.extra + 61] == 0x0000ff);
  assert(output[60 * view.width + view.extra + 62] == 0);
  assert(SmRendererDraw(output, view, true, 1));
  assert(output[60 * view.width + view.extra + 54] == 0);
  assert(output[60 * view.width + view.extra + 58] == 0x0000ff);

  /* Background midpoint uses source tile coordinates, never crossfading. */
  memset(&ppu, 0, sizeof(ppu));
  ppu.inidisp = 15; ppu.bgmode = 1; ppu.screenEnabled[0] = 1;
  ppu.bgTileAdr = 1; ppu.cgram[1] = 31; ppu.cgram[2] = 31 << 5;
  for (unsigned i = 0; i < 1024; ++i) ppu.vram[i] = i & 1;
  for (unsigned y = 0; y < 8; ++y) {
    ppu.vram[4096 + y] = 255;
    ppu.vram[4112 + y] = 255 << 8;
  }
  for (unsigned frame = 7; frame <= 8; ++frame) {
    ppu.hScroll[0] = frame == 7 ? 0 : 8;
    BeginFixtureFrame(ram, frame);
    for (unsigned y = 1; y <= 224; ++y) SmRendererCaptureLine(&ppu, y);
    assert(SmRendererEndFrame(stock));
  }
  assert(SmRendererDraw(output, view, true, 0.5));
  assert(output[60 * view.width + view.extra] == 0xff0000);
  assert(output[60 * view.width + view.extra + 4] == 0x00ff00);
  assert(SmRendererDraw(output, view, true, 1));
  assert(output[60 * view.width + view.extra] == 0x00ff00);
  /* Discontinuities must not interpolate into the old room. */
  word(0x79b, 0x1234); ppu.hScroll[0] = 16;
  BeginFixtureFrame(ram, 9);
  for (unsigned y = 1; y <= 224; ++y) SmRendererCaptureLine(&ppu, y);
  assert(SmRendererEndFrame(stock));
  assert(SmRendererDraw(output, view, true, 0));
  assert(output[60 * view.width + view.extra] == 0xff0000);
  ResetFixtureRenderer();
  /* Ceres-style mode-1 HUD -> mode-7 world split uses one captured raster.
   * Both margins must contain world texels, not centered stock fallback. */
  memset(&ppu, 0, sizeof(ppu));
  ppu.inidisp = 15; ppu.bgmode = 7; ppu.screenEnabled[0] = 1;
  ppu.m7matrix[0] = ppu.m7matrix[3] = 256;
  ppu.cgram[2] = 31 << 5;
  for (unsigned i = 0; i < 0x4000; ++i) ppu.vram[i] = 1 | (2 << 8);
  BeginFixtureFrame(ram, 10);
  for (unsigned y = 1; y <= 224; ++y) {
    ppu.bgmode = y <= 32 ? 1 : 7;
    SmRendererCaptureLine(&ppu, y);
  }
  assert(SmRendererEndFrame(stock));
  assert(SmRendererDraw(output, view, true, 1));
  assert(output[60 * view.width] == 0x00ff00);
  assert(output[60 * view.width + view.width - 1] == 0x00ff00);
  ResetFixtureRenderer();

  /* Offscreen projectile visibility follows retail instruction/flicker rules.
   * Enemy projectiles use their own graphics base and phase in the OBJ list. */
  memset(ram, 0, sizeof(ram));
  memset(&ppu, 0, sizeof(ppu));
  word(0x998, 8);
  rom[0x99000] = rom[0x69000] = 1;
  ppu.inidisp = 15; ppu.bgmode = 1; ppu.screenEnabled[0] = 16;
  ppu.cgram[129] = 31 << 10;
  ppu.cgram[145] = 31;
  ppu.cgram[161] = 31 << 5;
  for (unsigned y = 0; y < 8; ++y) ppu.vram[y] = 255;
  word(0xc40, 1); word(0xcb8, 0x9000);
  word(0xb64, 300); word(0xb78, 60);
  word(0x1997, 1); word(0x1b6b, 0x9000);
  word(0x1a4b, 310); word(0x1a93, 70); word(0x19bb, 0x200);
  word(0xf78, 0xd07f); word(0xf7a, 310); word(0xf7e, 70);
  word(0xf8e, 0x8000); ram[0xfa6] = 0xa2;
  word(0xf96, 0x400); word(0xf9a, 5);
  for (unsigned frame = 11; frame <= 14; ++frame) {
    word(0x5b6, frame & 1);
    if (frame == 13) word(0xf9a, 7);
    if (frame == 14) word(0x1997, 0);
    BeginFixtureFrame(ram, frame);
    for (unsigned y = 1; y <= 224; ++y) SmRendererCaptureLine(&ppu, y);
    assert(SmRendererEndFrame(stock));
    assert(SmRendererDraw(output, view, true, 1));
    assert(output[60 * view.width + view.extra + 300] == ((frame & 1) ? 0x0000ff : 0));
    unsigned expected = frame == 13 ? 0xff0000 : 0x00ff00;
    assert(output[70 * view.width + view.extra + 310] == expected);
  }
  word(0xc18, 0x300);
  for (unsigned live = 0; live < 2; ++live) {
    word(0xc7c, live);
    BeginFixtureFrame(ram, 15 + live);
    for (unsigned y = 1; y <= 224; ++y) SmRendererCaptureLine(&ppu, y);
    assert(SmRendererEndFrame(stock));
    assert(SmRendererDraw(output, view, true, 1));
    assert(output[60 * view.width + view.extra + 300] == (live ? 0x0000ff : 0));
  }
  /* Pickup/effect instruction operands are read-only and emit before enemy
   * OAM. Clearing the instruction pointer must remove the visual immediately. */
  rom[0x1a0000] = 8;
  rom[0x1a0002] = 0x10; rom[0x1a0003] = 0x90;
  rom[0x1a1010] = 1;
  word(0xf0f8, 310); word(0xf1f8, 70);
  for (unsigned live = 0; live < 2; ++live) {
    word(0xef78, live ? 0x8000 : 0);
    word(0xeff8, 8);
    BeginFixtureFrame(ram, 17 + live);
    for (unsigned y = 1; y <= 224; ++y) SmRendererCaptureLine(&ppu, y);
    assert(SmRendererEndFrame(stock));
    assert(SmRendererDraw(output, view, true, 1));
    assert(output[70 * view.width + view.extra + 310] == (live ? 0x0000ff : 0x00ff00));
    assert(ram[0xeff8] == 8);
  }
  if (argc > 1) {
    assert(SmRendererSaveCapture(argv[1]));
    ResetFixtureRenderer();
    assert(SmRendererLoadCapture(argv[1]));
    assert(SmRendererDraw(output, view, true, 1));
    assert(output[70 * view.width + view.extra + 310] == 0x0000ff);
    /* A partial load may overwrite the spare store, but must never corrupt
     * the current frame. Change the first record's CHR before truncating. */
    const size_t prefix_bytes=12+PPU_SAVESTATE_REGS_SIZE+1056+65536+128;
    uint8_t *prefix=malloc(prefix_bytes);assert(prefix);
    FILE *partial=fopen(argv[1],"rb");assert(partial);
    assert(fread(prefix,prefix_bytes,1,partial)==1);fclose(partial);
    memset(prefix+12+PPU_SAVESTATE_REGS_SIZE+1024,0,65536);
    partial=fopen(argv[1],"wb");assert(partial);
    assert(fwrite(prefix,prefix_bytes,1,partial)==1);fclose(partial);free(prefix);
    assert(!SmRendererLoadCapture(argv[1]));
    assert(SmRendererDraw(output,view,true,1));
    assert(output[70*view.width+view.extra+310]==0x0000ff);
    FILE *file = fopen(argv[1], "wb");
    assert(file);
    assert(fwrite("bad", 3, 1, file) == 1);
    assert(fclose(file) == 0);
    assert(!SmRendererLoadCapture(argv[1]));
    /* Failed diagnostic loads must not invalidate the last good frame. */
    assert(SmRendererDraw(output, view, true, 1));
    assert(output[70 * view.width + view.extra + 310] == 0x0000ff);
    assert(remove(argv[1]) == 0);
  }
  ResetFixtureRenderer();
  assert(!SmRendererDraw(output, view, true, 1));
  /* B4 effects interpolate verified owners across native/margin boundaries.
   * Normal animation advances may change maps, but arbitrary instruction
   * jumps, deletion and teleports must not invent intermediate positions. */
  static const int effect_origins[] = {-8, 100, 248, 300};
  for (unsigned origin = 0; origin < 4; ++origin) {
    for (unsigned scenario = 0; scenario < 5; ++scenario) {
      ResetFixtureRenderer();
      memset(ram, 0, sizeof(ram));
      memset(&ppu, 0, sizeof(ppu));
      word(0x998, 8); word(0x911, 256);
      word(0xf1f8, 60); word(0xeff8, 1);
      ppu.inidisp = 15; ppu.bgmode = 1; ppu.screenEnabled[0] = 16;
      ppu.cgram[129] = 31 << 10;
      for (unsigned y = 0; y < 8; ++y) ppu.vram[y] = ppu.vram[16 + y] = 255;
      for (unsigned instruction = 0; instruction < 3; ++instruction) {
        unsigned offset = 0x1a0010 + instruction * 4;
        rom[offset] = 8; rom[offset + 1] = 0;
        rom[offset + 2] = instruction ? 0x50 : 0x40; rom[offset + 3] = 0x90;
      }
      memset(rom + 0x1a1040, 0, 0x20);
      rom[0x1a1040] = rom[0x1a1050] = 1;
      rom[0x1a1055] = 1; /* Current animation uses tile 1. */
      int movement = scenario == 4 ? 40 : 8;
      for (unsigned frame = 0; frame < 2; ++frame) {
        int x = effect_origins[origin] + (frame ? movement : 0);
        unsigned instruction = frame && scenario == 1 ? 0x8014 :
                               frame && scenario == 2 ? 0x8018 : 0x8010;
        bool deleted = frame && scenario == 3;
        word(0xef78, deleted ? 0 : instruction);
        word(0xf0f8, (unsigned)(x + 256));
        for (unsigned slot = 0; slot < 128; ++slot) ppu.oam[slot * 2] = 240 << 8;
        memset(ppu.highOam, 0, sizeof(ppu.highOam));
        unsigned oam_slot = frame ? 7 : 0; /* OAM order is not owner identity. */
        ppu.oam[oam_slot * 2] = (uint16_t)((deleted ? 240 : 60) * 256 + (x & 255));
        ppu.oam[oam_slot * 2 + 1] = instruction == 0x8010 ? 0 : 1;
        ppu.highOam[oam_slot / 4] = (uint8_t)((((unsigned)x >> 8) & 1) << ((oam_slot & 3) * 2));
        BeginFixtureFrame(ram, frame + 1);
        for (unsigned line = 1; line <= 224; ++line) SmRendererCaptureLine(&ppu, line);
        assert(SmRendererEndFrame(stock));
      }
      assert(SmRendererDraw(output, view, false, .5));
      int expected_x = effect_origins[origin] + (scenario <= 1 ? movement / 2 : movement);
      assert((SmRendererGetStats().matched_effect_samples > 0) == (scenario <= 1));
      for (int x = -view.extra; x < 256 + view.extra; ++x)
        assert(output[60 * view.width + view.extra + x] ==
               (scenario != 3 && x >= expected_x && x < expected_x + 8 ? 0x0000ff : 0));
      assert(ram[0xeff8] == 1); /* Presentations never tick the timer. */
    }
  }
  /* Enemy motion must match both ROM maps and the same live owner. An OAM
   * slot moving by itself is not sufficient evidence of object continuity. */
  for (unsigned scenario = 0; scenario < 6; ++scenario) {
    ResetFixtureRenderer();
    memset(ram, 0, sizeof(ram));
    memset(&ppu, 0, sizeof(ppu));
    word(0x998, 8);
    word(0xf78, 0xd07f); word(0xf7e, 60);
    word(0xf8e, 0x8000); ram[0xfa6] = 0xa2;
    if (scenario == 4) {
      word(0xf88, 4); word(0xf8e, 0x8100);
      rom[0x110100] = 1;
      rom[0x110106] = 0; rom[0x110107] = 0x80;
    }
    ppu.inidisp = 15; ppu.bgmode = 1; ppu.screenEnabled[0] = 16;
    ppu.cgram[129] = 31 << 10;
    for (unsigned y = 0; y < 8; ++y) ppu.vram[y] = 255;
    for (unsigned frame = 1; frame <= 2; ++frame) {
      unsigned x = frame == 1 ? 50 : 58;
      word(0xf7a, x);
      if (frame == 2 && scenario == 1) word(0xf78, 0xd0bf);
      if (frame == 2 && scenario == 2) word(0xf86, 0x100);
      if (scenario == 3) word(0xfa2, 2);
      ppu.oam[0] = (uint16_t)(60 * 256 + x);
      LatchFixtureState(ram);
      word(0xf7a, x + 100); /* post-frame state must not own this OAM */
      BeginFixtureFrame(ram, frame);
      for (unsigned y = 1; y <= 224; ++y) {
        /* A raster OAM rewrite invalidates an earlier cached owner match. */
        if (scenario == 5 && frame == 2 && y == 65) ppu.oam[0] += 2;
        SmRendererCaptureLine(&ppu, y);
      }
      assert(SmRendererEndFrame(stock));
    }
    assert(SmRendererDraw(output, view, true, 0.5));
    bool interpolate = scenario == 0 || scenario >= 4;
    assert((SmRendererGetStats().matched_object_samples > 0) == interpolate);
    assert(output[60 * view.width + view.extra + 54] == (interpolate ? 0x0000ff : 0));
    if (scenario == 5) {
      assert(output[65 * view.width + view.extra + 54] == 0);
      assert(output[65 * view.width + view.extra + 60] == 0x0000ff);
    }
    assert(SmRendererDraw(output, view, true, 1));
    assert(output[60 * view.width + view.extra + 54] == 0);
    assert(output[60 * view.width + view.extra + 58] == 0x0000ff);
  }
  /* Native and reconstructed halves of a sprite straddling X=256 must use
   * the same intermediate origin. Also exercise an entirely offscreen owner. */
  for (unsigned edge = 0; edge < 4; ++edge) {
    ResetFixtureRenderer();
    memset(ram, 0, sizeof(ram));
    memset(&ppu, 0, sizeof(ppu));
    word(0x998, 8);
    word(0xf78, 0xd07f); word(0xf7e, 60);
    word(0xf8e, 0x8000); ram[0xfa6] = 0xa2;
    ppu.inidisp = 15; ppu.bgmode = 1; ppu.screenEnabled[0] = 16;
    ppu.cgram[129] = 31 << 10;
    for (unsigned y = 0; y < 8; ++y) ppu.vram[y] = 255;
    unsigned origin = (edge & 1) ? 300 : 248;
    bool reverse = edge >= 2;
    for (unsigned frame = 1; frame <= 2; ++frame) {
      unsigned x = origin + (reverse ? 2 - frame : frame - 1) * 4;
      word(0xf7a, x);
      ppu.oam[0] = (uint16_t)(60 * 256 + (x & 255));
      ppu.highOam[0] = (uint8_t)(x >> 8);
      BeginFixtureFrame(ram, frame);
      for (unsigned y = 1; y <= 224; ++y) SmRendererCaptureLine(&ppu, y);
      assert(SmRendererEndFrame(stock));
    }
    assert(SmRendererDraw(output, view, true, 0.5));
    for (unsigned x = origin; x < origin + 12; ++x)
      assert(output[60 * view.width + view.extra + x] ==
             (x >= origin + 2 && x < origin + 10 ? 0x0000ff : 0));
    assert(SmRendererDraw(output, view, true, 1));
    for (unsigned x = origin; x < origin + 12; ++x)
      assert(output[60 * view.width + view.extra + x] ==
             ((reverse ? x < origin + 8 : x >= origin + 4) ? 0x0000ff : 0));
  }
  /* Player/enemy projectile owners use the same motion in native OAM and
   * reconstructed margins. Birth, identity changes and flicker stay discrete. */
  rom[0x99000] = 1; /* $93:9000 */
  rom[0x69000] = 1; /* $8D:9000 */
  for (unsigned kind = 0; kind < 2; ++kind) {
    for (unsigned margin = 0; margin < 2; ++margin) {
      for (unsigned scenario = 0; scenario < 5; ++scenario) {
        ResetFixtureRenderer();
        memset(ram, 0, sizeof(ram));
        memset(&ppu, 0, sizeof(ppu));
        word(0x998, 8);
        ppu.inidisp = 15; ppu.bgmode = 1; ppu.screenEnabled[0] = 16;
        ppu.cgram[129] = 31 << 10;
        for (unsigned y = 0; y < 8; ++y) ppu.vram[y] = 255;
        unsigned origin = margin ? 300 : 50;
        unsigned movement = scenario == 4 ? 40 : 8;
        for (unsigned frame = 1; frame <= 2; ++frame) {
          unsigned x = origin + (frame - 1) * movement;
          if (kind) {
            word(0x1997, scenario == 1 && frame == 1 ? 0 : 1);
            if (scenario == 2 && frame == 2) word(0x1997, 2);
            if (scenario == 3) word(0x1840, 2);
            word(0x1b6b, 0x9000); word(0x1a4b, x); word(0x1a93, 60);
          } else {
            word(0xc40, scenario == 1 && frame == 1 ? 0 : 1);
            word(0xc18, scenario == 3 ? 0 : 0x100);
            if (scenario == 2 && frame == 2) word(0xc18, 0x200);
            word(0x5b6, frame - 1); /* ordinary beam hidden in prior frame */
            word(0xcb8, 0x9000); word(0xb64, x); word(0xb78, 60);
          }
          ppu.oam[0] = (uint16_t)(60 * 256 + (x & 255));
          ppu.highOam[0] = (uint8_t)(x >> 8);
          LatchFixtureState(ram);
          word(kind ? 0x1a4b : 0xb64, x + 100);
          BeginFixtureFrame(ram, frame);
          for (unsigned y = 1; y <= 224; ++y) SmRendererCaptureLine(&ppu, y);
          assert(SmRendererEndFrame(stock));
        }
        assert(SmRendererDraw(output, view, true, 0.5));
        assert(output[60 * view.width + view.extra + origin + 4] ==
               (scenario == 0 ? 0x0000ff : 0));
        if (!margin)
          assert((SmRendererGetStats().matched_object_samples > 0) == (scenario == 0));
        assert(SmRendererDraw(output, view, true, 1));
        assert(output[60 * view.width + view.extra + origin + 4] == 0);
        assert(output[60 * view.width + view.extra + origin + movement] == 0x0000ff);
        /* Clearing the owner and its native OAM removes the current visual;
         * a prior visible projectile must not be resurrected at alpha < 1. */
        word(kind ? 0x1997 : 0xc40, 0);
        ppu.oam[0] = 0xe000;
        ppu.highOam[0] = 0;
        BeginFixtureFrame(ram, 3);
        for (unsigned y = 1; y <= 224; ++y) SmRendererCaptureLine(&ppu, y);
        assert(SmRendererEndFrame(stock));
        assert(SmRendererDraw(output, view, true, 0.5));
        for (unsigned x = origin; x < origin + movement + 8; ++x)
          assert(output[60 * view.width + view.extra + x] == 0);
      }
    }
  }
  /* Every brightness/component value, changing brightness on each raster
   * line: the scanline lookup must retain exact integer truncation. */
  ResetFixtureRenderer(); memset(ram, 0, sizeof(ram)); memset(&ppu, 0, sizeof(ppu));
  word(0x998, 8); ppu.bgmode = 1;
  for (unsigned c = 0; c < 32; ++c) {
    ppu.cgram[0] = (uint16_t)(c | ((31 - c) << 5) | ((c ^ 15) << 10));
    BeginFixtureFrame(ram, c + 1);
    for (unsigned y = 1; y <= 224; ++y) {
      ppu.inidisp = (uint8_t)(y % 16);
      SmRendererCaptureLine(&ppu, y);
    }
    assert(SmRendererEndFrame(stock) && SmRendererDraw(output, view, false, 1));
    for (unsigned y = 0; y < 224; ++y) {
      unsigned expected = 0, channels[] = {c, 31 - c, c ^ 15};
      for (unsigned channel = 0; channel < 3; ++channel) {
        unsigned value = channels[channel];
        expected |= (((value * 8 + value / 4) * ((y + 1) % 16)) / 15) << (16 - channel * 8);
      }
      for (int x = 0; x < view.width; x += 31)
        assert(output[y * view.width + x] == expected);
    }
  }
  /* All Mode 1 BG layers: mosaic samples BEFORE scroll, follows the native
   * grid through both margins, and only affects enabled layers. Scroll varies
   * every raster line: prepared BG state must never leak between lines. */
  for (unsigned layer = 0; layer < 3; ++layer) {
    for (unsigned size = 1; size <= 16; ++size) {
      for (unsigned enabled = 0; enabled < 2; ++enabled) {
        ResetFixtureRenderer();
        memset(ram, 0, sizeof(ram));
        memset(&ppu, 0, sizeof(ppu));
        word(0x998, 8); word(0x7a5, 64); word(0x7a7, 32);
        word(0x911, 256); word(0x917, 256);
        ppu.inidisp = 15; ppu.bgmode = 1;
        ppu.screenEnabled[0] = (uint8_t)(1u << layer);
        ppu.bgTileAdr = 0x111;
        ppu.hScroll[layer] = 259; ppu.vScroll[layer] = 2;
        ppu.mosaic = (uint8_t)(((size - 1) << 4) | (enabled << layer));
        ppu.cgram[1] = 31; ppu.cgram[2] = 31 << 5; ppu.cgram[3] = 31 << 10;
        for (unsigned y = 0; y < 8; ++y) {
          for (unsigned x = 0; x < 8; ++x) {
            unsigned index = 1 + ((x + y) % 3);
            ppu.vram[4096 + y] |= (uint16_t)(((index & 1) << (7 - x)) |
                                             ((index >> 1) << (15 - x)));
          }
        }
        BeginFixtureFrame(ram, 1);
        for (unsigned y = 1; y <= 224; ++y) {
          ppu.hScroll[layer] = (uint16_t)(259 + y % 5);
          ppu.vScroll[layer] = (uint16_t)(2 + y % 3);
          SmRendererCaptureLine(&ppu, y);
        }
        assert(SmRendererEndFrame(stock));
        assert(SmRendererDraw(output, view, false, 1));
        static const uint32_t colors[] = {0xff0000, 0x00ff00, 0x0000ff};
        for (int y = 33; y < 224; y += 17) {
          for (int x = -view.extra; x < 256 + view.extra; ++x) {
            int mx = x, my = y + 1;
            if (enabled) {
              /* Independent floor expression, including negative positions. */
              mx = (x >= 0 ? x / (int)size : -((-x + (int)size - 1) / (int)size)) * (int)size;
              my = my / (int)size * (int)size;
            }
            unsigned color = (unsigned)(((mx + 259 + (y + 1) % 5) & 7) +
                                        ((my + 2 + (y + 1) % 3) & 7)) % 3;
            assert(output[y * view.width + view.extra + x] == colors[color]);
          }
        }
        /* Extra presentations retain the current mosaic grid while moving
         * each raster's scroll halfway between two simulation snapshots. */
        BeginFixtureFrame(ram, 2);
        for (unsigned y = 1; y <= 224; ++y) {
          ppu.hScroll[layer] = (uint16_t)(265 + y % 5);
          ppu.vScroll[layer] = (uint16_t)(4 + y % 3);
          SmRendererCaptureLine(&ppu, y);
        }
        assert(SmRendererEndFrame(stock));
        assert(SmRendererDraw(output, view, false, 0.5));
        unsigned differs_from_current = 0, differs_from_previous = 0;
        for (int y = 33; y < 224; y += 17) {
          for (int x = -view.extra; x < 256 + view.extra; ++x) {
            int mx = x, my = y + 1;
            if (enabled) {
              mx = (x >= 0 ? x / (int)size : -((-x + (int)size - 1) / (int)size)) * (int)size;
              my = my / (int)size * (int)size;
            }
            unsigned color = (unsigned)(((mx + 262 + (y + 1) % 5) & 7) +
                                        ((my + 3 + (y + 1) % 3) & 7)) % 3;
            assert(output[y * view.width + view.extra + x] == colors[color]);
            unsigned now_color = (unsigned)(((mx + 265 + (y + 1) % 5) & 7) +
                                            ((my + 4 + (y + 1) % 3) & 7)) % 3;
            unsigned old_color = (unsigned)(((mx + 259 + (y + 1) % 5) & 7) +
                                            ((my + 2 + (y + 1) % 3) & 7)) % 3;
            differs_from_current += color != now_color;
            differs_from_previous += color != old_color;
          }
        }
        assert(differs_from_current && differs_from_previous);
      }
    }
  }
  /* Mode 7 mosaic precedes both affine mapping and screen flips. A rotating
   * patterned tile exercises both coordinates, all sizes and enable states,
   * and signed screen positions on both sides of the native view. */
  for (unsigned size = 1; size <= 16; ++size) {
    for (unsigned flips = 0; flips < 4; ++flips) {
      for (unsigned enabled = 0; enabled < 2; ++enabled) {
        ResetFixtureRenderer();
        memset(ram, 0, sizeof(ram));
        memset(&ppu, 0, sizeof(ppu));
        word(0x998, 8);
        ppu.inidisp = 15; ppu.bgmode = 7; ppu.screenEnabled[0] = 1;
        ppu.m7matrix[1] = 256; ppu.m7matrix[2] = -256;
        ppu.m7sel = (uint8_t)flips;
        ppu.mosaic = (uint8_t)(((size - 1) << 4) | enabled);
        ppu.cgram[1] = 31; ppu.cgram[2] = 31 << 5; ppu.cgram[3] = 31 << 10;
        for (unsigned py = 0; py < 8; ++py)
          for (unsigned px = 0; px < 8; ++px)
            ppu.vram[py * 8 + px] = (uint16_t)((1 + (px + 2 * py) % 3) << 8);
        BeginFixtureFrame(ram, 1);
        for (unsigned line = 1; line <= 224; ++line) SmRendererCaptureLine(&ppu, line);
        assert(SmRendererEndFrame(stock));
        assert(SmRendererDraw(output, view, false, 1));
        assert(SmRendererGetStats().custom_lines == 224);
        assert(SmRendererGetStats().stock_lines == 0);
        static const uint32_t colors[] = {0xff0000, 0x00ff00, 0x0000ff};
        for (int y = 33; y < 224; y += 17) {
          for (int x = -view.extra; x < 256 + view.extra; ++x) {
            int mx = x, my = y + 1;
            if (enabled) {
              mx = (x >= 0 ? x / (int)size : -((-x + (int)size - 1) / (int)size)) * (int)size;
              my = my / (int)size * (int)size;
            }
            if (flips & 1) mx = 255 - mx;
            if (flips & 2) my = 255 - my;
            unsigned color = ((my & 7) + 2 * ((-mx) & 7)) % 3;
            assert(output[y * view.width + view.extra + x] == colors[color]);
          }
        }
      }
    }
  }
  /* MOSAIC affects BGs only; OBJ keeps its exact eight-pixel footprint. */
  ResetFixtureRenderer();
  memset(ram, 0, sizeof(ram));
  memset(&ppu, 0, sizeof(ppu));
  word(0x998, 8);
  ppu.inidisp = 15; ppu.bgmode = 1; ppu.screenEnabled[0] = 16;
  ppu.mosaic = 0xf7;
  ppu.cgram[129] = 31 << 10;
  ppu.oam[0] = 60 * 256 + 53;
  for (unsigned y = 0; y < 8; ++y) ppu.vram[y] = 255;
  BeginFixtureFrame(ram, 1);
  for (unsigned y = 1; y <= 224; ++y) SmRendererCaptureLine(&ppu, y);
  assert(SmRendererEndFrame(stock));
  assert(SmRendererDraw(output, view, true, 1));
  for (int y = 59; y <= 68; ++y)
    for (int x = 48; x < 64; ++x)
      assert(output[y * view.width + view.extra + x] ==
             (y >= 60 && y < 68 && x >= 53 && x < 61 ? 0x0000ff : 0));
  /* Ceres rotates projectile origins without rotating their sprite tiles.
   * Retail bombs and category-500 explosions deliberately stay unprojected. */
  static const unsigned projected_types[] = {0x100, 0x300, 0x400, 0x500};
  for (unsigned i = 0; i < 4; ++i) {
    ResetFixtureRenderer();
    memset(ram, 0, sizeof(ram));
    memset(&ppu, 0, sizeof(ppu));
    word(0x998, 8); word(0x93f, 0x8000);
    word(0x7a, 256); word(0x7c, 0xff00); word(0x80, 128); word(0x82, 200);
    word(0xc40, 1); word(0xc18, projected_types[i]); word(0xc7c, 1);
    word(0xcb8, 0x9000); word(0xb64, 128);
    /* Steam-style enemy offsets are already projected by the guest. */
    word(0xf78, 0xd07f); word(0xf7a, 280); word(0xf7e, 60);
    word(0xf8e, 0x8000); ram[0xfa6] = 0xa2; word(0x7010, 20);
    ppu.inidisp = 15; ppu.bgmode = 7; ppu.screenEnabled[0] = 16;
    ppu.cgram[129] = 31 << 10;
    for (unsigned y = 0; y < 8; ++y) ppu.vram[y] = 255;
    BeginFixtureFrame(ram, 1);
    for (unsigned y = 1; y <= 224; ++y) SmRendererCaptureLine(&ppu, y);
    assert(SmRendererEndFrame(stock));
    assert(SmRendererDraw(output, view, true, 1));
    assert(output[200 * view.width + view.extra + 328] == ((i == 0 || i == 2) ? 0x0000ff : 0));
    assert(output[60 * view.width + view.extra + 300] == 0x0000ff);
    assert(output[60 * view.width + view.extra + 280] == 0);
  }
  for (unsigned margin = 0; margin < 2; ++margin) {
    ResetFixtureRenderer();
    memset(ram, 0, sizeof(ram));
    memset(&ppu, 0, sizeof(ppu));
    word(0x998, 8); word(0x93f, 0x8000);
    word(0x7a, 256); word(0x7c, 0xff00); word(0x80, 128); word(0x82, 200);
    word(0xc40, 1); word(0xc18, 0x100); word(0xcb8, 0x9000); word(0xb64, 128);
    ppu.inidisp = 15; ppu.bgmode = 7; ppu.screenEnabled[0] = 16;
    ppu.cgram[129] = 31 << 10;
    for (unsigned y = 0; y < 8; ++y) ppu.vram[y] = 255;
    unsigned origin = margin ? 278 : 50;
    for (unsigned frame = 1; frame <= 2; ++frame) {
      unsigned x = origin + (frame - 1) * 8;
      word(0xb78, 328 - x);
      ppu.oam[0] = (uint16_t)(200 * 256 + (x & 255));
      ppu.highOam[0] = (uint8_t)(x >> 8);
      BeginFixtureFrame(ram, frame);
      for (unsigned y = 1; y <= 224; ++y) SmRendererCaptureLine(&ppu, y);
      assert(SmRendererEndFrame(stock));
    }
    assert(SmRendererDraw(output, view, true, 0.5));
    assert(output[200 * view.width + view.extra + origin] == 0);
    assert(output[200 * view.width + view.extra + origin + 4] == 0x0000ff);
    if (!margin) assert(SmRendererGetStats().matched_object_samples > 0);
    assert(SmRendererDraw(output, view, true, 1));
    assert(output[200 * view.width + view.extra + origin + 4] == 0);
    assert(output[200 * view.width + view.extra + origin + 8] == 0x0000ff);
  }
  /* Power-of-two address optimization must also preserve 16x16 BG tiles,
   * including quadrant carry and both whole-tile flips on every layer. */
  for (unsigned layer = 0; layer < 3; ++layer) {
    for (unsigned flip = 0; flip < 4; ++flip) {
      ResetFixtureRenderer();
      memset(ram, 0, sizeof(ram));
      memset(&ppu, 0, sizeof(ppu));
      word(0x998, 8);
      ppu.inidisp = 15; ppu.bgmode = (uint8_t)(1 | (16 << layer));
      ppu.screenEnabled[0] = (uint8_t)(1 << layer); ppu.bgTileAdr = 0x111;
      ppu.cgram[1] = 31; ppu.cgram[2] = 31 << 5; ppu.cgram[3] = 31 << 10;
      for (unsigned i = 0; i < 1024; ++i) ppu.vram[i] = (uint16_t)(flip << 14);
      for (unsigned q = 0; q < 4; ++q) {
        unsigned number = (q & 1) + (q >> 1) * 16;
        unsigned index = q + 1;
        if (index == 4) index = 0;
        for (unsigned y = 0; y < 8; ++y)
          ppu.vram[4096 + number * (layer == 2 ? 8 : 16) + y] =
              (uint16_t)((index & 1 ? 255 : 0) | (index & 2 ? 0xff00 : 0));
      }
      BeginFixtureFrame(ram, 1);
      for (unsigned y = 1; y <= 224; ++y) SmRendererCaptureLine(&ppu, y);
      assert(SmRendererEndFrame(stock));
      assert(SmRendererDraw(output, view, true, 1));
      static const uint32_t colors[] = {0xff0000, 0x00ff00, 0x0000ff, 0};
      for (int y = 33; y < 112; ++y)
        for (int x = -20; x < 276; ++x) {
          unsigned quadrant = (unsigned)(((x & 15) / 8) | (((y + 1) & 15) / 8 * 2));
          assert(output[y * view.width + view.extra + x] == colors[quadrant ^ flip]);
        }
    }
  }
  /* Animation changes map geometry/tile/palette AND OAM reservation. Move
   * the current piece with its owner, not toward a different old piece. */
  rom[0x9008d] = 0; rom[0x9008e] = 0x90;
  rom[0x9008f] = 0x10; rom[0x90090] = 0x90;
  memset(rom + 0x91000, 0, 32);
  rom[0x91000] = 1; rom[0x91010] = 1;
  rom[0x91012] = 4; rom[0x91015] = 1; rom[0x91016] = 2;
  for (unsigned scenario = 0; scenario < 4; ++scenario) {
    ResetFixtureRenderer();
    memset(ram, 0, sizeof(ram));
    memset(&ppu, 0, sizeof(ppu));
    word(0x998, 8);
    ppu.inidisp = 15; ppu.bgmode = 1; ppu.screenEnabled[0] = 16;
    ppu.cgram[129] = 31 << 10; ppu.cgram[145] = 31 << 5;
    for (unsigned y = 0; y < 8; ++y) ppu.vram[y] = ppu.vram[16 + y] = 255;
    unsigned new_origin = scenario == 2 ? 90 : 58;
    unsigned current_x = new_origin + 4 + (scenario == 3 ? 1 : 0);
    for (unsigned frame = 1; frame <= 2; ++frame) {
      for (unsigned slot = 0; slot < 128; ++slot) ppu.oam[slot * 2] = 0xe000;
      word(0xb04, frame == 1 ? 50 : new_origin); word(0xb06, 60);
      word(0xac8, frame - 1);
      if (scenario == 1 && frame == 2) word(0xa1c, 1);
      unsigned slot = frame == 1 ? 0 : 12;
      ppu.oam[slot * 2] = (uint16_t)(60 * 256 + (frame == 1 ? 50 : current_x));
      ppu.oam[slot * 2 + 1] = frame == 1 ? 0 : 0x201;
      LatchFixtureState(ram);
      word(0xb04, 150);
      BeginFixtureFrame(ram, frame);
      for (unsigned y = 1; y <= 224; ++y) SmRendererCaptureLine(&ppu, y);
      assert(SmRendererEndFrame(stock));
    }
    assert(SmRendererDraw(output, view, true, 0.5));
    assert((SmRendererGetStats().matched_object_samples > 0) == (scenario == 0));
    unsigned expected_x = scenario == 0 ? 58 : current_x;
    assert((SmRendererGetStats().matched_samus_samples > 0) == (scenario == 0));
    assert(output[60 * view.width + view.extra + expected_x] == 0x00ff00);
    assert(output[60 * view.width + view.extra + expected_x - 1] == 0);
    assert(SmRendererDraw(output, view, true, 1));
    assert(output[60 * view.width + view.extra + current_x] == 0x00ff00);
    assert(output[60 * view.width + view.extra + current_x - 1] == 0);
  }
  /* Enemy owner motion survives reservation and animation-map changes in
   * native view, across X=256, and wholly in the reconstructed margin. */
  memset(rom + 0x110000, 0, 0x130);
  rom[0x110000] = rom[0x110010] = 1;
  rom[0x110012] = 2; rom[0x110015] = 1; rom[0x110016] = 2;
  rom[0x110100] = rom[0x110110] = 1;
  rom[0x110107] = 0x80;
  rom[0x110112] = 3; rom[0x110116] = 0x10; rom[0x110117] = 0x80;
  for (unsigned extended = 0; extended < 2; ++extended) {
    for (unsigned region = 0; region < 3; ++region) {
      ResetFixtureRenderer();
      memset(ram, 0, sizeof(ram));
      memset(&ppu, 0, sizeof(ppu));
      word(0x998, 8);
      word(0xf78, 0xd07f); word(0xf7e, 60); ram[0xfa6] = 0xa2;
      word(0xf88, extended ? 4 : 0);
      ppu.inidisp = 15; ppu.bgmode = 1; ppu.screenEnabled[0] = 16;
      ppu.cgram[129] = 31 << 10; ppu.cgram[145] = 31 << 5;
      for (unsigned y = 0; y < 8; ++y) ppu.vram[y] = ppu.vram[16 + y] = 255;
      unsigned origin = region == 0 ? 50 : region == 1 ? 246 : 300;
      unsigned new_offset = 2 + (extended ? 3 : 0);
      for (unsigned frame = 1; frame <= 2; ++frame) {
        for (unsigned slot = 0; slot < 128; ++slot) ppu.oam[slot * 2] = 0xe000;
        unsigned root = origin + (frame - 1) * 4;
        unsigned x = root + (frame == 2 ? new_offset : 0);
        word(0xf7a, root);
        word(0xf8e, (extended ? 0x8100 : 0x8000) + (frame - 1) * 16);
        unsigned slot = frame == 1 ? 0 : 12;
        ppu.oam[slot * 2] = (uint16_t)(60 * 256 + (x & 255));
        ppu.oam[slot * 2 + 1] = frame == 1 ? 0 : 0x201;
        ppu.highOam[slot / 4] = (uint8_t)(x >> 8);
        BeginFixtureFrame(ram, frame);
        for (unsigned y = 1; y <= 224; ++y) SmRendererCaptureLine(&ppu, y);
        assert(SmRendererEndFrame(stock));
      }
      assert(SmRendererDraw(output, view, true, 0.5));
      unsigned midpoint = origin + 2 + new_offset;
      for (unsigned x = midpoint - 1; x <= midpoint + 8; ++x)
        assert(output[60 * view.width + view.extra + x] ==
               (x >= midpoint && x < midpoint + 8 ? 0x00ff00 : 0));
      assert(SmRendererDraw(output, view, true, 1));
      assert(output[60 * view.width + view.extra + midpoint] == 0);
      assert(output[60 * view.width + view.extra + midpoint + 2] == 0x00ff00);
    }
  }
  ResetFixtureRenderer();
  memset(ram, 0, sizeof(ram));
  memset(&ppu, 0, sizeof(ppu));
  word(0x998, 8);
  ppu.inidisp = 15; ppu.bgmode = 1; ppu.screenEnabled[0] = 16;
  ppu.cgram[129] = 31 << 10;
  for (unsigned y = 0; y < 8; ++y) ppu.vram[y] = 255;
  for (unsigned frame = 1; frame <= 2; ++frame) {
    unsigned x = frame == 1 ? 50 : 58;
    for (unsigned owner = 0; owner < 2; ++owner) {
      unsigned offset = owner * 64;
      word(0xf78 + offset, 0xd07f + owner * 64);
      word(0xf7a + offset, owner ? 58 : x); word(0xf7e + offset, 60);
      word(0xf8e + offset, 0x8000); ram[0xfa6 + offset] = 0xa2;
    }
    ppu.oam[0] = (uint16_t)(60 * 256 + x);
    BeginFixtureFrame(ram, frame);
    for (unsigned y = 1; y <= 224; ++y) SmRendererCaptureLine(&ppu, y);
    assert(SmRendererEndFrame(stock));
  }
  assert(SmRendererDraw(output, view, true, 0.5));
  assert(SmRendererGetStats().matched_object_samples == 0);
  assert(output[60 * view.width + view.extra + 54] == 0);
  assert(output[60 * view.width + view.extra + 58] == 0x0000ff);
  for (unsigned kind = 0; kind < 2; ++kind) {
    unsigned rom_base = kind ? 0x69000 : 0x99000;
    memset(rom + rom_base, 0, 32);
    rom[rom_base] = rom[rom_base + 16] = 1;
    rom[rom_base + 18] = 2;
    rom[rom_base + 21] = 1; rom[rom_base + 22] = 2;
    for (unsigned region = 0; region < 3; ++region) {
      ResetFixtureRenderer();
      memset(ram, 0, sizeof(ram));
      memset(&ppu, 0, sizeof(ppu));
      word(0x998, 8);
      if (kind) word(0x1997, 1);
      else { word(0xc40, 1); word(0xc18, 0x100); }
      ppu.inidisp = 15; ppu.bgmode = 1; ppu.screenEnabled[0] = 16;
      ppu.cgram[129] = 31 << 10; ppu.cgram[145] = 31 << 5;
      for (unsigned y = 0; y < 8; ++y) ppu.vram[y] = ppu.vram[16 + y] = 255;
      unsigned origin = region == 0 ? 50 : region == 1 ? 244 : 300;
      for (unsigned frame = 1; frame <= 2; ++frame) {
        for (unsigned slot = 0; slot < 128; ++slot) ppu.oam[slot * 2] = 0xe000;
        unsigned root = origin + (frame - 1) * 8;
        unsigned x = root + (frame == 2 ? 2 : 0);
        word(kind ? 0x1a4b : 0xb64, root); word(kind ? 0x1a93 : 0xb78, 60);
        word(kind ? 0x1b6b : 0xcb8, 0x9000 + (frame - 1) * 16);
        unsigned slot = frame == 1 ? 0 : 12;
        ppu.oam[slot * 2] = (uint16_t)(60 * 256 + (x & 255));
        ppu.oam[slot * 2 + 1] = frame == 1 ? 0 : 0x201;
        ppu.highOam[slot / 4] = (uint8_t)(x >> 8);
        LatchFixtureState(ram);
        word(kind ? 0x1a4b : 0xb64, root + 100);
        BeginFixtureFrame(ram, frame);
        for (unsigned y = 1; y <= 224; ++y) SmRendererCaptureLine(&ppu, y);
        assert(SmRendererEndFrame(stock));
      }
      assert(SmRendererDraw(output, view, true, 0.5));
      unsigned midpoint = origin + 6;
      for (unsigned x = midpoint - 1; x <= midpoint + 8; ++x)
        assert(output[60 * view.width + view.extra + x] ==
               (x >= midpoint && x < midpoint + 8 ? 0x00ff00 : 0));
      assert(SmRendererDraw(output, view, true, 1));
      assert(output[60 * view.width + view.extra + midpoint] == 0);
      assert(output[60 * view.width + view.extra + midpoint + 4] == 0x00ff00);
    }
  }
  /* Ambiguous player/enemy projectile overlap must not borrow motion from
   * whichever family is searched first. */
  ResetFixtureRenderer();
  memset(ram, 0, sizeof(ram));
  memset(&ppu, 0, sizeof(ppu));
  word(0x998, 8); word(0xc40, 1); word(0xc18, 0x100); word(0x1997, 1);
  word(0xcb8, 0x9000); word(0x1b6b, 0x9000);
  word(0xb78, 60); word(0x1a93, 60); word(0x1a4b, 58);
  ppu.inidisp = 15; ppu.bgmode = 1; ppu.screenEnabled[0] = 16;
  ppu.cgram[129] = 31 << 10;
  for (unsigned y = 0; y < 8; ++y) ppu.vram[y] = 255;
  for (unsigned frame = 1; frame <= 2; ++frame) {
    unsigned x = frame == 1 ? 50 : 58;
    word(0xb64, x); ppu.oam[0] = (uint16_t)(60 * 256 + x);
    BeginFixtureFrame(ram, frame);
    for (unsigned y = 1; y <= 224; ++y) SmRendererCaptureLine(&ppu, y);
    assert(SmRendererEndFrame(stock));
  }
  assert(SmRendererDraw(output, view, true, 0.5));
  assert(SmRendererGetStats().matched_object_samples == 0);
  assert(output[60 * view.width + view.extra + 54] == 0);
  assert(output[60 * view.width + view.extra + 58] == 0x0000ff);
  /* A current OAM origin beyond the native footprint can interpolate back
   * into it. Resolve 9-bit X from its owner, never guess X+512 from OAM. */
  memset(rom + 0x110000, 0, 7); rom[0x110000] = 1;
  memset(rom + 0x99000, 0, 7); rom[0x99000] = 1;
  memset(rom + 0x69000, 0, 7); rom[0x69000] = 1;
  for (unsigned family = 0; family < 4; ++family) {
    for (unsigned side = 0; side < 3; ++side) {
      ResetFixtureRenderer();
      memset(ram, 0, sizeof(ram));
      memset(&ppu, 0, sizeof(ppu));
      word(0x998, 8);
      int camera = side == 2 ? 512 : 256;
      word(0x911, (unsigned)camera);
      if (family == 0) { word(0xf78, 0xd07f); word(0xf8e, 0x8000); ram[0xfa6] = 0xa2; }
      if (family == 1) { word(0xc40, 1); word(0xc18, 0x100); word(0xcb8, 0x9000); }
      if (family == 2) { word(0x1997, 1); word(0x1b6b, 0x9000); }
      ppu.inidisp = 15; ppu.bgmode = 1; ppu.screenEnabled[0] = 16;
      ppu.cgram[129] = 31 << 10;
      for (unsigned y = 0; y < 8; ++y) ppu.vram[y] = 255;
      int old_x = side == 0 ? 248 : side == 1 ? -2 : -264;
      int now_x = side == 0 ? 260 : side == 1 ? -10 : -252;
      for (unsigned frame = 1; frame <= 2; ++frame) {
        int x = frame == 1 ? old_x : now_x;
        word(family == 0 ? 0xf7a : family == 1 ? 0xb64 : 0x1a4b, (unsigned)(camera + x));
        word(family == 0 ? 0xf7e : family == 1 ? 0xb78 : 0x1a93, 60);
        ppu.oam[0] = (uint16_t)(60 * 256 + (x & 255));
        ppu.highOam[0] = (uint8_t)((x & 511) >> 8);
        BeginFixtureFrame(ram, frame);
        for (unsigned y = 1; y <= 224; ++y) SmRendererCaptureLine(&ppu, y);
        assert(SmRendererEndFrame(stock));
      }
      assert(SmRendererDraw(output, view, true, 0.5));
      int midpoint = (old_x + now_x) / 2;
      assert(SmRendererGetStats().recovered_edge_pixels == (family < 3 && side < 2 ? 16u : 0u));
      for (int x = -view.extra; x < 256 + view.extra; ++x)
        assert(output[60 * view.width + view.extra + x] ==
               (family < 3 && x >= midpoint && x < midpoint + 8 ? 0x0000ff : 0));
      assert(SmRendererDraw(output, view, true, 1));
      for (int x = 0; x < 256; ++x)
        assert(output[60 * view.width + view.extra + x] == 0);
    }
  }
  /* Exercise the custom compositor itself, not just the engine's window
   * helper: both enables/inversions, OR/AND/XOR/XNOR, and edge extension. */
  static const unsigned bounds[][4] = {{40,120,80,180}, {0,255,80,180}, {255,0,0,255}};
  for (unsigned bound = 0; bound < 3; ++bound) {
    for (unsigned flags = 0; flags < 16; ++flags) {
      for (unsigned operation = 0; operation < 4; ++operation) {
        ResetFixtureRenderer();
        memset(ram, 0, sizeof(ram)); memset(&ppu, 0, sizeof(ppu));
        word(0x998, 8); word(0x7a5, 64); word(0x7a7, 32); word(0x911, 256);
        ppu.inidisp = 15; ppu.bgmode = 1; ppu.screenEnabled[0] = 1;
        ppu.screenWindowed[0] = 1; ppu.bgTileAdr = 1; ppu.hScroll[0] = 256;
        ppu.cgram[0] = 31 << 10; ppu.cgram[1] = 31;
        ppu.windowsel = flags; ppu.wbgobjlog = operation;
        ppu.window1left = bounds[bound][0]; ppu.window1right = bounds[bound][1];
        ppu.window2left = bounds[bound][2]; ppu.window2right = bounds[bound][3];
        for (unsigned y = 0; y < 8; ++y) ppu.vram[4096 + y] = 255;
        BeginFixtureFrame(ram, 1);
        for (unsigned line = 1; line <= 224; ++line) SmRendererCaptureLine(&ppu, line);
        assert(SmRendererEndFrame(stock));
        assert(SmRendererDraw(output, view, false, 1));
        for (int x = -view.extra; x < 256 + view.extra; ++x) {
          int left1 = bounds[bound][0] ? (int)bounds[bound][0] : -view.extra;
          int right1 = bounds[bound][1] == 255 ? 255 + view.extra : (int)bounds[bound][1];
          int left2 = bounds[bound][2] ? (int)bounds[bound][2] : -view.extra;
          int right2 = bounds[bound][3] == 255 ? 255 + view.extra : (int)bounds[bound][3];
          unsigned a = (unsigned)(x >= left1 && x <= right1) ^ (flags & 1);
          unsigned b = (unsigned)(x >= left2 && x <= right2) ^ ((flags >> 2) & 1);
          /* Truth tables indexed by the two window predicates. */
          static const unsigned truth[] = {0xe, 0x8, 0x6, 0x9};
          bool hidden = (flags & 2) ? ((flags & 8) ? ((truth[operation] >> (a * 2 + b)) & 1) : a)
                                    : ((flags & 8) ? b : false);
          assert(output[60 * view.width + view.extra + x] == (hidden ? 0x0000ff : 0xff0000));
        }
      }
    }
  }
  /* Color-window clipping and math prevention are independent. Cover all
   * policies with fixed/subscreen color, addition/subtraction and halving. */
  for (unsigned policy = 0; policy < 16; ++policy) {
    for (unsigned arithmetic = 0; arithmetic < 8; ++arithmetic) {
      ResetFixtureRenderer();
      memset(ram, 0, sizeof(ram)); memset(&ppu, 0, sizeof(ppu));
      word(0x998, 8); word(0x7a5, 64); word(0x7a7, 32);
      word(0x911, 256); word(0x917, 256);
      ppu.inidisp = 15; ppu.bgmode = 1; ppu.screenEnabled[0] = 1; ppu.screenEnabled[1] = 2;
      ppu.bgTileAdr = 0x21; ppu.hScroll[0] = ppu.hScroll[1] = 256;
      ppu.cgram[1] = 10 | (8 << 5) | (6 << 10);
      ppu.cgram[2] = 4 | (6 << 5) | (10 << 10);
      ppu.fixedColor = 1 | (2 << 5) | (3 << 10);
      ppu.windowsel = 2u << 20; ppu.window1left = 64; ppu.window1right = 191;
      ppu.cgwsel = (uint8_t)((policy << 4) | ((arithmetic & 1) ? 2 : 0));
      ppu.cgadsub = (uint8_t)(1 | ((arithmetic & 2) ? 64 : 0) | ((arithmetic & 4) ? 128 : 0));
      for (unsigned y = 0; y < 8; ++y) { ppu.vram[4096 + y] = 255; ppu.vram[8192 + y] = 0xff00; }
      BeginFixtureFrame(ram, 1);
      for (unsigned line = 1; line <= 224; ++line) SmRendererCaptureLine(&ppu, line);
      assert(SmRendererEndFrame(stock));
      assert(SmRendererDraw(output, view, false, 1));
      for (int x = -view.extra; x < 256 + view.extra; ++x) {
        bool inside = x >= 64 && x <= 191;
        bool conditions[] = {false, !inside, inside, true};
        bool clip = conditions[policy >> 2], math = !conditions[policy & 3];
        static const int main_rgb[] = {10,8,6}, sub_rgb[] = {4,6,10}, fixed_rgb[] = {1,2,3};
        uint32_t expected = 0;
        for (unsigned component = 0; component < 3; ++component) {
          int value = clip ? 0 : main_rgb[component];
          if (math) {
            int other = (arithmetic & 1) ? sub_rgb[component] : fixed_rgb[component];
            value += (arithmetic & 4) ? -other : other;
            if (value < 0) value = 0;
            if ((arithmetic & 2) && !clip) value /= 2;
            if (value > 31) value = 31;
          }
          expected |= (uint32_t)(value * 8 + value / 4) << (16 - component * 8);
        }
        assert(output[60 * view.width + view.extra + x] == expected);
      }
    }
  }
  /* Preset mask replay: pointers/preset advance after NMI. Distinct row
   * widths catch using post-frame pointers or omitting raster line +1.
   * Offscreen centers exercise shapes hidden by native clipping. */
  for (unsigned location = 0; location < 3; ++location) {
    for (unsigned fault = 0; fault < 4; ++fault) {
      ResetFixtureRenderer();
      memset(ram, 0, sizeof(ram)); memset(&ppu, 0, sizeof(ppu));
      memset(rom, 0, sizeof(rom));
      SmRendererSetRom(rom, sizeof(rom));
      int center = location == 0 ? -100 : location == 1 ? 128 : 350;
      word(0x998, 8); word(0x592, fault == 1 ? 0 : 0x8000);
      word(0xce6, center + 256); word(0x18f0, 0x8eb2);
      word(0xcf2, 0x9246); word(0x18d8, fault == 3 ? 0 : 0x9800);
      word(0x18da, 0xa101);
      for (unsigned i = 0; i < 192; ++i) {
        int width = 180 - (int)i / 2, left = center - width, right = center + width;
        rom[0x41246 + i] = (uint8_t)width;
        bool empty = right < 0 || left > 255;
        ram[0xc406 + i] = empty ? 255 : left < 0 ? 0 : (uint8_t)left;
        ram[0xc506 + i] = empty ? 0 : right > 255 ? 255 : (uint8_t)right;
      }
      for (unsigned i = 0; i < 225; ++i) {
        unsigned target = 0xc406 + i % 192;
        for (unsigned side = 0; side < 2; ++side) {
          unsigned offset = 0x49800 + 3 * i + side * 0x901;
          rom[offset] = 0x81;
          rom[offset + 1] = (uint8_t)target;
          rom[offset + 2] = (uint8_t)((target + side * 256) >> 8);
        }
      }
      LatchFixtureState(ram);
      word(0xcf2, 0x9306); word(0x18d8, 0x9806); word(0x18da, 0xa107);
      if (fault == 2) ram[0xc406 + 100] ^= 1;
      ppu.inidisp = 15; ppu.bgmode = 1; ppu.cgram[0] = 31;
      ppu.windowsel = 8u << 20; ppu.cgwsel = 0x80;
      BeginFixtureFrame(ram, 1);
      for (unsigned line = 1; line <= 224; ++line) {
        ppu.window2left = ram[0xc406 + line % 192];
        ppu.window2right = ram[0xc506 + line % 192];
        SmRendererCaptureLine(&ppu, line);
      }
      assert(SmRendererEndFrame(stock));
      assert(SmRendererDraw(output, view, false, 1));
      assert(SmRendererGetStats().power_bomb_lines == (fault ? 0u : 224u));
      for (unsigned y = 0; y < 224; ++y) {
        unsigned row = (y + 1) % 192;
        for (int x = -view.extra; x < 256 + view.extra; ++x) {
          int left = ram[0xc406 + row], right = ram[0xc506 + row];
          if (!fault && (x < 0 || x >= 256)) {
            left = center - (180 - (int)row / 2);
            right = center + (180 - (int)row / 2);
          } else {
            if (left == 0) left = -view.extra;
            if (right == 255) right = 255 + view.extra;
          }
          assert(output[y * view.width + view.extra + x] ==
                 (x >= left && x <= right ? 0u : 0xff0000u));
        }
      }
    }
  }
  /* Grapple crosses X=256: native clipping stops its OAM loop, while the
   * host samples remaining segment art without modifying guest timers. */
  ResetFixtureRenderer(); memset(ram, 0, sizeof(ram)); memset(rom, 0, sizeof(rom)); memset(&ppu, 0, sizeof(ppu));
  SmRendererSetRom(rom, sizeof(rom));
  word(0x998, 8); word(0xa5c, 0xeb86); word(0xd32, 0xc703); word(0xcfe, 32);
  word(0xd1a, 256); word(0xd1c, 104); word(0xd08, 288); word(0xd0c, 104); word(0xcfa, 0x4000);
  romword(0x1034c3, 256); /* cos(angle64), sine remains zero */
  for (unsigned i = 0; i < 16; ++i) { word(0xd62 + 2 * i, 0xe104); word(0xd42 + 2 * i, 2); }
  romword(0xa6102, 0x3a00);
  word(0xd62 + 2 * 13, 0xe200); word(0xd42 + 2 * 13, 1);
  romword(0xa6200, 0xb0f4); romword(0xa6202, 0xe300);
  romword(0xa6300, 2); romword(0xa6302, 0x3a01);
  SmGrapplePiece grapple[17];
  assert(SmRendererGrapplePieces(ram, grapple) == 5);
  for (unsigned i = 0; i < 4; ++i) {
    assert(grapple[i].x == 252 + (int)i * 8 && grapple[i].y == 100);
    assert(grapple[i].attr == (i == 2 ? 0x3a01 : 0x3a00));
  }
  assert(grapple[4].x == 284 && grapple[4].attr == 0x3a20);
  ppu.inidisp = 15; ppu.bgmode = 1; ppu.screenEnabled[0] = 16; ppu.cgram[209] = 0x3e0;
  ppu.oam[0] = 252 | (100 << 8); ppu.oam[1] = 0x3a00;
  for (unsigned y = 0; y < 8; ++y) ppu.vram[y] = ppu.vram[16 + y] = ppu.vram[512 + y] = 255;
  static uint8_t grapple_ram_before[0x20000]; memcpy(grapple_ram_before, ram, sizeof(ram));
  LatchFixtureState(ram); BeginFixtureFrame(ram, 1);
  for (unsigned y = 1; y <= 224; ++y) SmRendererCaptureLine(&ppu, y);
  assert(SmRendererEndFrame(stock) && SmRendererDraw(output, view, false, 1));
  assert(SmRendererGetStats().grapple_native_matches == 1);
  assert(SmRendererGetStats().grapple_margin_pixels == 36 * 8);
  for (int x = 256; x < 320; ++x)
    assert(output[100 * view.width + view.extra + x] == (x < 292 ? 0x00ff00u : 0));
  assert(!memcmp(ram, grapple_ram_before, sizeof(ram)));
  romword(0xa6202, 0xe200); /* An animation jump cycle cannot hang rendering. */
  assert(SmRendererGrapplePieces(ram, grapple) == 0);
  word(0xcfe, 0); assert(SmRendererGrapplePieces(ram, grapple) == 0);
  /* Flare animation may change map/palette while its owner moves. Native
   * OAM and margin reconstruction must both use current art at the midpoint. */
  const int flare_origins[] = {100, 252, 280};
  for (unsigned origin = 0; origin < 3; ++origin) {
    for (unsigned scenario = 0; scenario < 3; ++scenario) {
      ResetFixtureRenderer(); memset(ram, 0, sizeof(ram)); memset(rom, 0, sizeof(rom)); memset(&ppu, 0, sizeof(ppu));
      SmRendererSetRom(rom, sizeof(rom));
      word(0x998, 8); word(0xa5c, 0xeb86); word(0xd32, 0xc703); word(0xcd0, 4);
      ram[0xa1e] = 4;
      romword(0x9a22b, 4);
      romword(0x9a1c9, 0x9000); romword(0x9a1cb, 0x9010);
      romword(0x99000, 1); romword(0x99010, 1);
      romword(0x99005, 0x3a00); romword(0x99015, 0x3c01);
      ppu.inidisp = 15; ppu.bgmode = 1; ppu.screenEnabled[0] = 16;
      ppu.cgram[209] = 31; ppu.cgram[225] = 0x3e0;
      for (unsigned row = 0; row < 8; ++row) ppu.vram[row] = ppu.vram[16 + row] = 255;
      int before_x = flare_origins[origin], now_x = before_x + (scenario == 2 ? 64 : 8);
      for (unsigned frame = 0; frame < 2; ++frame) {
        int x = frame ? now_x : before_x;
        word(0xd1a, (unsigned)x); word(0xd1c, 100); word(0xcd6, 16 + frame);
        if (frame && scenario == 1) word(0xd32, 0xc77e);
        ppu.oam[0] = (uint16_t)((x & 255) | (100 << 8)); ppu.oam[1] = frame ? 0x3c01 : 0x3a00;
        ppu.highOam[0] = (uint8_t)((x >> 8) & 1);
        LatchFixtureState(ram); BeginFixtureFrame(ram, frame + 1);
        for (unsigned row = 1; row <= 224; ++row) SmRendererCaptureLine(&ppu, row);
        assert(SmRendererEndFrame(stock));
      }
      assert(SmRendererDraw(output, view, false, 0.5));
      int expected_x = scenario ? now_x : before_x + 4;
      for (int x = before_x - 8; x < now_x + 16; ++x)
        assert(output[100 * view.width + view.extra + x] ==
               (x >= expected_x && x < expected_x + 8 ? 0x00ff00u : 0));
      assert(ram[0xcd6] == 17 && ram[0xcd0] == 4);
    }
  }
  /* A currently clipped segment can interpolate back into native pixels.
   * This must also work in FPS-only mode, with no widescreen margin. */
  ResetFixtureRenderer(); memset(ram, 0, sizeof(ram)); memset(rom, 0, sizeof(rom)); memset(&ppu, 0, sizeof(ppu));
  SmRendererSetRom(rom, sizeof(rom));
  word(0x998, 8); word(0xa5c, 0xeb86); word(0xd32, 0xc703); word(0xcfe, 8); word(0xcfa, 0x4000);
  word(0xd1c, 104); word(0xd0c, 104); word(0xd80, 0xe104); word(0xd60, 2);
  romword(0x1034c3, 256); romword(0xa6102, 0x3a00);
  ppu.inidisp = 15; ppu.bgmode = 1; ppu.screenEnabled[0] = 16; ppu.cgram[209] = 0x3e0;
  for (unsigned row = 0; row < 8; ++row) ppu.vram[row] = ppu.vram[512 + row] = 255;
  for (unsigned frame = 0; frame < 2; ++frame) {
    unsigned source = 248 + frame * 16, tip = source + 4;
    word(0xd1a, source); word(0xd08, source + 8);
    ppu.oam[0] = frame ? 0 : (244 | (100 << 8)); ppu.oam[1] = 0x3a00;
    ppu.oam[2] = (uint16_t)((tip & 255) | (100 << 8)); ppu.oam[3] = 0x3a20;
    ppu.highOam[0] = (uint8_t)(((tip >> 8) & 1) << 2);
    LatchFixtureState(ram); BeginFixtureFrame(ram, frame + 1);
    for (unsigned row = 1; row <= 224; ++row) SmRendererCaptureLine(&ppu, row);
    assert(SmRendererEndFrame(stock));
  }
  for (unsigned wide = 0; wide < 2; ++wide) {
    SmViewport viewport = wide ? view : (SmViewport){256, 0, 4.0 / 3, true};
    assert(SmRendererDraw(output, viewport, false, 0.5));
    assert(SmRendererGetStats().grapple_interpolated_pieces == 2);
    assert(SmRendererGetStats().recovered_edge_pixels == 32);
    for (int x = 240; x < (wide ? 280 : 256); ++x)
      assert(output[100 * viewport.width + viewport.extra + x] == (x >= 252 && x < 268 ? 0x00ff00u : 0));
    assert(SmRendererDraw(output, viewport, false, 1));
    assert(SmRendererGetStats().recovered_edge_pixels == 0);
  }
  /* Growing while translating adds a segment, but must not leave a gap or
   * confuse the old endpoint with that newly born segment. */
  word(0xd1a, 280); word(0xd08, 296); word(0xcfe, 16);
  word(0xd7e, 0xe104); word(0xd5e, 2);
  ppu.oam[2] = (292 & 255) | (100 << 8); ppu.highOam[0] = 4;
  LatchFixtureState(ram); BeginFixtureFrame(ram, 3);
  for (unsigned row = 1; row <= 224; ++row) SmRendererCaptureLine(&ppu, row);
  assert(SmRendererEndFrame(stock) && SmRendererDraw(output, view, false, 0.5));
  assert(SmRendererGetStats().grapple_interpolated_pieces == 3);
  for (int x = 264; x < 296; ++x)
    assert(output[100 * view.width + view.extra + x] == (x >= 268 && x < 288 ? 0x00ff00u : 0));
  /* Deliberately bypass the fixture wrapper: post-frame RAM cannot stand
   * in for a missing pre-NMI latch. One latch is consumed by one frame. */
  ResetFixtureRenderer(); memset(ram, 0, sizeof(ram)); memset(rom, 0, sizeof(rom)); memset(&ppu, 0, sizeof(ppu));
  SmRendererSetRom(rom, sizeof(rom)); word(0x998, 8); word(0xa1c, 1);
  romword(0x9008d, 0x9000); romword(0x91000, 1);
  ppu.inidisp = 15; ppu.bgmode = 1; ppu.screenEnabled[0] = 16; ppu.cgram[129] = 0x3e0;
  for (unsigned row = 0; row < 8; ++row) ppu.vram[row] = 255;
  for (unsigned frame = 1; frame <= 5; ++frame) {
    unsigned x = 92 + frame * 8;
    word(0xb04, x); word(0xb06, 100);
    ppu.oam[0] = (uint16_t)(x | (100 << 8));
    if (frame == 3 || frame == 4) SmRendererLatchObjectState(ram);
    SmRendererBeginFrame(ram, frame);
    for (unsigned row = 1; row <= 224; ++row) SmRendererCaptureLine(&ppu, row);
    assert(SmRendererEndFrame(stock) && SmRendererDraw(output, view, false, 0.5));
    unsigned expected_x = frame == 4 ? x - 4 : x;
    assert((SmRendererGetStats().matched_samus_samples != 0) == (frame == 4));
    for (unsigned pixel = x - 8; pixel < x + 12; ++pixel)
      assert(output[100 * view.width + view.extra + pixel] ==
             (pixel >= expected_x && pixel < expected_x + 8 ? 0x00ff00u : 0));
  }
  puts("Super Metroid room tiles and immutable raster capture checks passed.");
  return 0;
}
