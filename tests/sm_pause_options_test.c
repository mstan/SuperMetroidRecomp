/* The pause menu's Options tab (src/sm_pause_options.c): what one paused
 * frame does to the guest's pause state, pad and fade words, and the
 * panel's pixels.
 *
 * The panel's top band is the guest HUD, read from its BG3 tilemap and the
 * cartridge's HUD tiles/palettes; the rest is the ROM pause-window frame, the
 * small-font title, the pause screen's own bottom tab strip, and the
 * framework's settings rows.
 *
 * Opening rides the ROM's tab-change effects: the 0x38 SFX through the
 * guest's own SFX-1 queue, and a host-driven screen fade (15 fast steps to
 * force-blank, one no-step frame, 15 slow steps back) stepped on the
 * guest's own INIDISP/fade words, so the press frame keeps its brightness
 * exactly as a native tab change does. Leaving to a different tab is the
 * guest's own tab change; a same-tab leave is a plain close. A held Start
 * hands the in-flight fade to the guest's unpause on the frame its own
 * timed-held input filter takes the edge.
 *
 * The render hashes are a golden picture of the open panel at its eleven-row
 * layout (four SM video settings, five framework display settings, a
 * graceful Exit, and Back) with the navigation the test has driven. A
 * change to them is a visible change to the panel.
 *
 * The framework's live-settings API (host_api.h) is faked here: the fakes
 * apply the same clamps the host's setters do and mark g_config_dirty, as
 * the host does before its shutdown config.ini write. No SDL runs.
 */
#include "sm_pause_options.h"
#include "sm_video.h"
#include "types.h"
#include "host_api.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

uint8 g_ram[0x20000];
#include "variables.h"

#define game_state_byte g_ram[0x0998]
#define inidisp_byte g_ram[0x0051]

/* host_api.h fakes. The host applies the same clamps (runner/src/desktop/
 * host_main.c); the defaults match the shipped config.ini: vsync on, window
 * scale 3, everything else off. */
bool g_config_dirty;
bool f_host_live = true;
bool snesrecomp_host_frame_is_live(void) { return f_host_live; }
bool f_exit_requested;
int f_fullscreen;        /* 0 = windowed, 1 = borderless */
int f_vsync = 1;         /* 0 = off, 1 = on, 2 = adaptive */
int f_frame_blend;
int f_run_ahead;
int f_window_scale = 3;

void snesrecomp_set_fullscreen(int mode) {
  if (mode < 0) mode = 0;
  if (mode > 1) mode = 1;
  f_fullscreen = mode;
  g_config_dirty = true;
}
int snesrecomp_get_fullscreen(void) { return f_fullscreen; }

void snesrecomp_set_vsync(int mode) {
  if (mode < 0) mode = 0;
  if (mode > 2) mode = 2;
  f_vsync = mode;
  g_config_dirty = true;
}
int snesrecomp_get_vsync(void) { return f_vsync; }

void snesrecomp_set_frame_blend(int enabled) {
  f_frame_blend = enabled != 0;
  g_config_dirty = true;
}
int snesrecomp_get_frame_blend(void) { return f_frame_blend; }

void snesrecomp_set_run_ahead(int frames) {
  if (frames < 0) frames = 0;
  f_run_ahead = frames;
  g_config_dirty = true;
}
int snesrecomp_get_run_ahead(void) { return f_run_ahead; }

void snesrecomp_set_window_scale(int scale) {
  if (scale < 1) scale = 1;
  if (scale > 10) scale = 10;
  f_window_scale = scale;
  g_config_dirty = true;
}
int snesrecomp_get_window_scale(void) { return f_window_scale; }

/* The host ends the run after the requesting frame; the fake only latches,
 * so the test can keep driving the model afterwards. */
void snesrecomp_request_exit(void) { if (f_host_live) f_exit_requested = true; }

static uint32_t g_frame[684 * 224];

/* Test stubs for the ROM glyph renderer. The glyph bitmaps live in the
 * cartridge; the test supplies a synthetic font region so the render is
 * deterministic without the real ROM. No ROM asset is copied here: the
 * pattern below is generated, not extracted. */
static uint8 g_fake_rom[0x10000];   /* stands in for $B6:8000-$B6:FFFF */
static uint8 g_fake_hud[0x10000];   /* stands in for $9A:8000-$9A:FFFF */

static void init_fake_rom(void) {
  memset(g_fake_rom, 0, sizeof(g_fake_rom));
  memset(g_fake_hud, 0, sizeof(g_fake_hud));
  /* Every font tile gets a distinct 4bpp pattern at $B6:C000 + tile*32
   * (offset 0x4000 in this window), so a wrong char->tile index or a
   * plane/row mix-up changes the pixels and the golden hash catches it.
   * Clearing the top bit of each plane byte leaves the leftmost pixel
   * transparent in every row, exercising the decode's zero handling. */
  for (unsigned tile = 0x60; tile <= 0x8f; ++tile) {
    unsigned base = 0x4000u + tile * 32u;
    for (unsigned y = 0; y < 8; ++y) {
      uint32_t s = tile * 2654435761u + y * 97u;
      uint16_t w0 = (uint16_t)((s >> 3) & 0x7f7fu);
      uint16_t w1 = (uint16_t)(((s >> 19) ^ (s >> 7)) & 0x7f7fu);
      g_fake_rom[base + y * 2] = (uint8_t)(w0 & 0xFF);
      g_fake_rom[base + y * 2 + 1] = (uint8_t)(w0 >> 8);
      g_fake_rom[base + 16 + y * 2] = (uint8_t)(w1 & 0xFF);
      g_fake_rom[base + 16 + y * 2 + 1] = (uint8_t)(w1 >> 8);
    }
  }

  /* The window frame reads tiles 0xbd/0xbe/0xbf/0xcf from $B6:8000 (offset
   * 0 here) and palette 2 from $B6:F000 (offset 0x7000). Give both synthetic
   * contents so the frame's tile decode, flips and palette lookup are covered
   * by the golden hash. Generated, not extracted. */
  {
    static const unsigned frame_tiles[] = {0xbd, 0xbe, 0xbf, 0xcf};
    for (size_t t = 0; t < sizeof(frame_tiles) / sizeof(frame_tiles[0]); ++t) {
      const unsigned tile = frame_tiles[t];
      const unsigned base = tile * 32u;
      for (unsigned y = 0; y < 8; ++y) {
        uint32_t s = tile * 2246822519u + y * 59u;
        uint16_t w0 = (uint16_t)((s >> 5) & 0x7f7fu);
        uint16_t w1 = (uint16_t)(((s >> 21) ^ (s >> 3)) & 0x7f7fu);
        g_fake_rom[base + y * 2] = (uint8_t)(w0 & 0xFF);
        g_fake_rom[base + y * 2 + 1] = (uint8_t)(w0 >> 8);
        g_fake_rom[base + 16 + y * 2] = (uint8_t)(w1 & 0xFF);
        g_fake_rom[base + 16 + y * 2 + 1] = (uint8_t)(w1 >> 8);
      }
    }
  }

  /* The pause screen's bottom tab strip reads the pause BG2 tilemap rows
   * 25-26 (offset 0x6640 in this window: $B6:E000 + 25 rows * 32 cols * 2
   * bytes) and their tiles from the $B6:8000 tileset. One synthetic 16-bit
   * entry per cell: the hash spreads tile, palette and flips across the 64
   * cells, so the strip's per-entry decode is covered by the golden hash.
   * Referenced tiles stay in 0x00-0x5F, which leaves the font, frame,
   * palette and tilemap-word regions untouched; each gets its own 4bpp
   * pattern. Generated, not extracted. */
  {
    static uint8_t used[96];
    for (unsigned i = 0; i < 64; ++i) {
      const uint32_t s = i * 2654435761u + 13u;
      const uint16_t w = (uint16_t)((((s >> 9) & 0x5Fu) |
                                     (((s >> 15) & 7u) << 10) |
                                     (((s >> 21) & 1u) << 13) |
                                     (((s >> 22) & 1u) << 14)));
      used[w & 0x5Fu] = 1;
      g_fake_rom[0x6640u + i * 2u] = (uint8_t)w;
      g_fake_rom[0x6640u + i * 2u + 1u] = (uint8_t)(w >> 8);
    }
    for (unsigned t = 0; t < 96; ++t) {
      if (!used[t]) continue;
      const unsigned base = t * 32u;
      for (unsigned y = 0; y < 8; ++y) {
        const uint32_t s = t * 2654435761u + y * 59u;
        const uint16_t w0 = (uint16_t)((s >> 5) & 0x7F7Fu);
        const uint16_t w1 = (uint16_t)((s >> 21) & 0x7F7Fu);
        g_fake_rom[base + y * 2] = (uint8_t)w0;
        g_fake_rom[base + y * 2 + 1] = (uint8_t)(w0 >> 8);
        g_fake_rom[base + 16 + y * 2] = (uint8_t)w1;
        g_fake_rom[base + 16 + y * 2 + 1] = (uint8_t)(w1 >> 8);
      }
    }
  }

  /* HUD tiles at $9A:B200 (offset 0x3200 in the $9A window). BG3 is 2bpp, so
   * each tile is 16 bytes: one word per row. The panel draws the guest BG3
   * tilemap, so the full 10-bit tile range (512 tiles) gets a distinct
   * generated pattern with transparent pixels. */
  for (unsigned tile = 0; tile < 512; ++tile) {
    unsigned base = 0x3200u + tile * 16u;
    for (unsigned y = 0; y < 8; ++y) {
      uint32_t s = tile * 40503u + y * 131u;
      uint16_t w0 = (uint16_t)((s >> 2) & 0x7f7fu);
      g_fake_hud[base + y * 2] = (uint8_t)(w0 & 0xFF);
      g_fake_hud[base + y * 2 + 1] = (uint8_t)(w0 >> 8);
    }
  }

  /* The full pause palette set at $B6:F000: 16 palettes of 16 BGR555 entries,
   * one distinct value per (palette, index). The frame uses palette 2 at its
   * 16-colour stride; the 2bpp HUD uses 4-entry palettes 3 and 5. */
  for (unsigned p = 0; p < 16; ++p) {
    const unsigned pal = 0x7000u + p * 16u * 2u;
    for (unsigned i = 0; i < 16; ++i) {
      const uint16_t c =
          (uint16_t)((0x0421u * (1u + p * 16u + i)) & 0x7FFFu);
      g_fake_rom[pal + i * 2] = (uint8_t)(c & 0xFF);
      g_fake_rom[pal + i * 2 + 1] = (uint8_t)(c >> 8);
    }
  }
}

const uint8_t *SmRomPtr(unsigned bank, unsigned addr, size_t size) {
  const uint8_t *window = NULL;
  if (bank == 0xB6) window = g_fake_rom;
  else if (bank == 0x9A) window = g_fake_hud;
  if (!window || addr < 0x8000) return NULL;
  size_t off = addr - 0x8000;
  if (off + size > 0x10000u) return NULL;
  return &window[off];
}

uint8_t SmRomByte(unsigned bank, unsigned addr) {
  const uint8_t *p = SmRomPtr(bank, addr, 1);
  return p ? *p : 0;
}

static uint32_t RenderHash(int width) {
  for (size_t i = 0; i < sizeof(g_frame) / sizeof(g_frame[0]); ++i)
    g_frame[i] = 0xFF123456u;
  /* As the host does (SmDrawFrame): the overlay is composited only while
   * Visible(). */
  if (SmPauseOptions_Visible())
    SmPauseOptions_Render(g_frame, width, width, 224);
  uint32_t h = 2166136261u;
  for (size_t i = 0; i < (size_t)width * 224; ++i) { h ^= g_frame[i]; h *= 16777619u; }
  return h;
}

/* A golden-pixel check. In a calibration build (PAUSE_OPTIONS_TEST_CALIBRATE,
 * compiled by hand after a layout or row change) it prints the actual hash
 * instead of asserting, so the constants below can be regenerated. */
#ifndef PAUSE_OPTIONS_TEST_CALIBRATE
#define PAUSE_OPTIONS_TEST_CALIBRATE 0
#endif

static size_t g_hash_seq;

static void HashCheck(int width, uint32_t expected) {
  const uint32_t actual = RenderHash(width);
#if PAUSE_OPTIONS_TEST_CALIBRATE
  printf("hash %zu width=%d actual=0x%08X expected=0x%08X\n",
         ++g_hash_seq, width, actual, expected);
#else
  assert(actual == expected);
#endif
}

/* ReleaseButtonsFilter(3) ($80:8146): the guest's timed-held input. A button
 * counts as newly held only once it has been stably held through the decay
 * window, so the unpause Start edge lands five frames after the press and a
 * quick tap produces none. The stable branch writes the decrement back, so a
 * hold decays the timer 3 -> 2 -> 1 -> 0 -> wrap across the frames. Runs on
 * the sample the host left on the pad. */
static void RunButtonsFilter(void) {
  const bool v1 = ((uint16)(~joypad1_newkeys & joypad1_lastkeys) == joypad_released_keys);
  joypad_released_keys = (uint16)(~joypad1_newkeys & joypad1_lastkeys);
  if (!v1) {
    timed_held_input_timer = 3;
    timed_held_input = 0;
  } else if (((uint16)(--timed_held_input_timer) & 0x8000) == 0) {
    timed_held_input = 0;
  } else {
    timed_held_input_timer = 0;
    previous_timed_held_input = timed_held_input;
    timed_held_input = joypad_released_keys;
  }
  newly_held_down_timed_held_input =
      timed_held_input & (uint16)(previous_timed_held_input ^ timed_held_input);
}

/* The guest pumps its SFX-1 queue once per frame (SfxHandlers_0_SendToApu,
 * $82:8A2C), so queued sounds do not pile up; the read pointer wraps the
 * 16-slot ring as the ROM's does. */
static void PumpSfx1(void) {
  if (sfx_readpos[0] != sfx_writepos[0])
    sfx_readpos[0] = (uint8)((sfx_readpos[0] + 1) & 0xF);
}

/* One paused frame with `pressed` newly down (and `held` also down): the
 * host tick, then the guest routine's own acts on the sample the host left. */
static void Frame(uint16 pressed, uint16 held) {
  joypad1_newkeys = pressed;
  joypad1_lastkeys = (uint16)(pressed | held);
  SmPauseOptions_GuestTick();
  RunButtonsFilter();
  PumpSfx1();
}

static void OnMap(void) { menu_index = 0; pausemenu_button_label_mode = 0; }

/* The guest's BG3 HUD tilemap (g_ram+0xC608), as the paused game leaves it:
 * backdrop, energy tanks, and the health/weapon digits. This is generated
 * setup rather than ROM data; it exercises the HUD renderer's per-entry
 * palette, tile and flip paths. */
static void SeedHud(void) {
  uint16_t *hud = (uint16_t *)(g_ram + 0xC608);
  for (int i = 0; i < 96; ++i) hud[i] = 0x2c0f;  /* tile 0x0f, palette 3 */
  for (int i = 1; i <= 7; ++i) {
    hud[i] = 0x3430;        /* energy tank top, palette 5 */
    hud[32 + i] = 0x3431;   /* energy tank bottom, palette 5 */
  }
  hud[70] = 0x2c00; hud[71] = 0x2c07;                     /* health digits */
  hud[74] = 0x2c09; hud[75] = 0x2c05; hud[76] = 0x2c02;   /* missiles */
  hud[78] = 0x2c01; hud[79] = 0x2c04;                     /* super missiles */
  hud[81] = 0x2c08; hud[82] = 0x2c06;                     /* power bombs */
  hud[94] = 0x5430;  /* hflip */
  hud[95] = 0xF431;  /* hflip + vflip */
}

/* The guest's fade/input/SFX words as a paused screen at rest leaves them:
 * every pause entry path ends the fades at 0/0, the SFX-1 queue is pumped
 * to empty, and the button filter decays to idle. */
static void ResetFadeInputs(void) {
  screen_fade_delay = 0;
  screen_fade_counter = 0;
  sfx_max_queued[0] = 0;
  sfx_readpos[0] = 0;
  sfx_writepos[0] = 0;
  memset(sfx1_queue, 0, 16);
  joypad_released_keys = 0;
  timed_held_input_timer = 0;
  previous_timed_held_input = 0;
  timed_held_input = 0;
  newly_held_down_timed_held_input = 0;
}

/* The guest finishing a fade-out: it moves on to the Load state. */
static void FadeOutDone(void) { menu_index = (uint16)(menu_index + 1); }

/* The host enter fade's INIDISP, f frames after the press frame: 15 fast
 * steps to force-blank (delay 0), one no-step frame (the counter's first
 * reload), then 15 slow steps back to full (delay 1, one step per two
 * frames) -- the native tab change's fade minus its Load frame. */
static uint8 EnterInidisp(unsigned f) {
  if (f == 0) return 0x0F;
  if (f <= 14) return (uint8)(0x0F - f);
  if (f <= 16) return 0x80;
  unsigned level = 1 + (f - 17) / 2;
  return (uint8)(level > 15 ? 15 : level);
}

int main(void) {
  init_fake_rom();
  SmVideoDefaults(&g_sm_video);
  g_sm_video.hud_anchored = false;
  assert(!g_sm_video.enhanced && !g_sm_video.fps_enabled);
  game_state_byte = 0x0F;
  inidisp_byte = 0x0F;
  SeedHud();

  /* ── opening: the press frame ────────────────────────────────────────── */

  /* L on Map opens the panel. While it is up it owns the pad: the pressed
   * and held buttons are swallowed, so the hidden Map screen sees none of
   * them. The enter rides the ROM's tab-change effects: the 0x38 SFX and a
   * host-driven fade on the guest's own words. */
  OnMap();
  ResetFadeInputs();
  Frame(kButton_L, kButton_Y | kButton_Down);
  assert(SmPauseOptions_IsActive() && menu_index == 0);
  assert(joypad1_newkeys == 0 && joypad1_lastkeys == 0);
  assert(joypad_released_keys == 0 && newly_held_down_timed_held_input == 0);
  /* The press frame is not a step frame, exactly as a native tab change's:
   * full brightness, and the fade marker armed for the frames ahead. */
  assert(inidisp_byte == 0x0F && screen_fade_counter == 0);
  assert(screen_fade_delay == 1);
  assert(!SmPauseOptions_Visible());  /* hidden through the down phase */
  /* The tab-change SFX went through the guest's own queue and sounded. */
  assert(sfx1_queue[0] == 0x38 && sfx_readpos[0] == 1 && sfx_writepos[0] == 1);

  /* ── the enter fade timeline ─────────────────────────────────────────── */

  /* 15 fast steps to force-blank, one no-step frame, 15 slow steps back:
   * full brightness again 45 frames after the press (the native tab change
   * takes 46 -- the host fade has no Load frame). The guest never leaves
   * its own tab while any of it runs. */
  for (unsigned f = 1; f <= 46; ++f) {
    Frame(0, 0);
    assert(inidisp_byte == EnterInidisp(f));
    assert(menu_index == 0 && pausemenu_button_label_mode == 0);
    if (f == 14)
      assert(screen_fade_delay == 1 && !SmPauseOptions_Visible());
    if (f == 15)
      assert(screen_fade_delay == 2 && screen_fade_counter == 1 &&
             inidisp_byte == 0x80 && SmPauseOptions_Visible());
    if (f == 16)
      assert(screen_fade_delay == 2 && screen_fade_counter == 0 &&
             inidisp_byte == 0x80);
    if (f == 17)
      assert(inidisp_byte == 1 && screen_fade_counter == 1);
    if (f == 45)
      assert(screen_fade_delay == 0 && screen_fade_counter == 0 &&
             inidisp_byte == 0x0F && SmPauseOptions_Visible());
  }
  Frame(0, 0);  /* settled: marker clear, INIDISP holds at full */
  assert(screen_fade_delay == 0 && screen_fade_counter == 0 && inidisp_byte == 0x0F);

  /* Opened on Widescreen at the defaults: sm-video defaults, and the faked
   * framework defaults (windowed, vsync on, no blend, no run-ahead, scale 3). */
  HashCheck(256, 0xA9AE91DDu);
  HashCheck(342, 0xD9E6EC09u);
  HashCheck(456, 0x3532ADFDu);
  HashCheck(684, 0x8E01165Du);
  {
    /* The window frame comes from the synthetic ROM tiles, not a host
     * rectangle: the top-left corner (4,42) and the top bar (12,42) are
     * opaque frame pixels rather than the panel background. */
    (void)RenderHash(256);
    assert(g_frame[42 * 256 + 4] != 0xFF0A0E18u);
    assert(g_frame[42 * 256 + 12] != 0xFF0A0E18u);
    /* The top band is the guest HUD, not the panel background: it paints
     * pixels there starting at y=8, stopping at y=32. */
    int hud_pixels = 0;
    for (int y = 8; y < 32 && !hud_pixels; ++y)
      for (int x = 0; x < 256; ++x)
        if (g_frame[y * 256 + x] != 0xFF0A0E18u) { hud_pixels = 1; break; }
    assert(hud_pixels);
    assert(g_frame[6 * 256 + 100] == 0xFF0A0E18u);
    assert(g_frame[34 * 256 + 100] == 0xFF0A0E18u);
    /* The footer tab strip (y=200-215) renders cartridge tiles from the
     * guest tilemap copy, not the panel background. */
    int footer_pixels = 0;
    for (int y = 200; y < 216 && !footer_pixels; ++y)
      for (int x = 0; x < 256; ++x)
        if (g_frame[y * 256 + x] != 0xFF0A0E18u) { footer_pixels = 1; break; }
    assert(footer_pixels);
  }

  /* ── navigation ──────────────────────────────────────────────────────── */

  /* HUD anchoring is disabled without widescreen, so Down skips it. */
  Frame(kButton_Down, 0);   /* Presentation FPS */
  HashCheck(256, 0x6970792Du);
  HashCheck(342, 0xBA0C2EF9u);
  Frame(kButton_Down, 0);   /* Aspect ratio */
  Frame(kButton_Right, 0);  /* Fit wraps to 4:3 */
  assert(g_sm_video.aspect == SM_ASPECT_STOCK);
  HashCheck(256, 0x51D775A5u);
  HashCheck(684, 0x38793CB5u);
  Frame(kButton_Right, 0);
  Frame(kButton_Left, 0);
  assert(g_sm_video.aspect == SM_ASPECT_STOCK);
  Frame(kButton_A, 0);      /* accept steps a choice forward */
  assert(g_sm_video.aspect == SM_ASPECT_16_9);
  HashCheck(256, 0x78234F2Bu);
  HashCheck(342, 0xA3FE4C63u);

  /* Turning widescreen on brings HUD anchoring up, from either direction. */
  Frame(kButton_Up, 0);     /* Presentation FPS */
  Frame(kButton_Up, 0);     /* skips the disabled HUD row to Widescreen */
  Frame(kButton_Left, 0);
  assert(g_sm_video.enhanced && g_sm_video.hud_anchored);
  Frame(kButton_Down, 0);   /* HUD anchoring, now reachable */
  HashCheck(256, 0x8380818Fu);
  HashCheck(342, 0xA106A75Fu);
  Frame(kButton_A, 0);
  assert(!g_sm_video.hud_anchored && g_sm_video.enhanced);

  /* Down leaves the SM rows for the framework's: Fullscreen, Windowed. */
  Frame(kButton_Down, 0);   /* Presentation FPS */
  Frame(kButton_Down, 0);   /* Aspect ratio */
  Frame(kButton_Down, 0);   /* Fullscreen */
  HashCheck(256, 0x32A8987Du);
  HashCheck(684, 0x077F52ADu);
  Frame(kButton_Right, 0);  /* Borderless */
  assert(f_fullscreen == 1 && g_config_dirty);
  HashCheck(256, 0xE4AA8F32u);
  /* VSync is a three-way choice that wraps at both ends. */
  Frame(kButton_Down, 0);   /* VSync */
  Frame(kButton_Right, 0);
  assert(f_vsync == 2 && g_config_dirty);   /* Adaptive */
  Frame(kButton_Right, 0);
  assert(f_vsync == 0);                     /* Off */
  Frame(kButton_Right, 0);
  assert(f_vsync == 1);                     /* back to On */
  HashCheck(256, 0xE54357DCu);

  /* Frame blend toggles on a fresh press, either direction. */
  Frame(kButton_Down, 0);   /* Frame blend */
  Frame(kButton_A, 0);
  assert(f_frame_blend == 1 && g_config_dirty);
  Frame(kButton_A, 0);
  assert(f_frame_blend == 0);
  HashCheck(256, 0x0E245F82u);

  /* Run ahead counts 0..4 and stops at the ends. */
  Frame(kButton_Down, 0);   /* Run ahead */
  Frame(kButton_Right, 0);
  Frame(kButton_Right, 0);
  Frame(kButton_Right, 0);
  Frame(kButton_Right, 0);
  assert(f_run_ahead == 4);
  Frame(kButton_Right, 0);
  assert(f_run_ahead == 4);
  HashCheck(256, 0x57309D90u);
  Frame(kButton_Left, 0);
  assert(f_run_ahead == 3);
  Frame(kButton_Left, 0);
  Frame(kButton_Left, 0);
  Frame(kButton_Left, 0);
  Frame(kButton_Left, 0);
  assert(f_run_ahead == 0);
  Frame(kButton_Left, 0);
  assert(f_run_ahead == 0);

  /* Still borderless, so Window scale is disabled: navigation skips it and
   * lands on Exit. */
  Frame(kButton_Down, 0);   /* Run ahead -> Exit */
  HashCheck(256, 0xEAD89554u);
  HashCheck(684, 0xB4C95A5Cu);
  Frame(kButton_A, 0);      /* graceful exit */
  assert(f_exit_requested);
  HashCheck(256, 0xEAD89554u);       /* the action changes nothing visible */

  /* Down to Back, up again to Fullscreen (over the disabled scale row). */
  Frame(kButton_Down, 0);   /* Back to map */
  HashCheck(256, 0x34D2D170u);
  Frame(kButton_Up, 0);
  Frame(kButton_Up, 0);
  Frame(kButton_Up, 0);
  Frame(kButton_Up, 0);
  Frame(kButton_Up, 0);     /* Fullscreen */
  Frame(kButton_Left, 0);   /* Windowed */
  assert(f_fullscreen == 0 && g_config_dirty);
  HashCheck(256, 0x9A16FE59u);

  /* Window scale, enabled again, counts 1..10. */
  Frame(kButton_Down, 0);   /* VSync */
  Frame(kButton_Down, 0);   /* Frame blend */
  Frame(kButton_Down, 0);   /* Run ahead */
  Frame(kButton_Down, 0);   /* Window scale */
  Frame(kButton_Right, 0);
  Frame(kButton_Right, 0);
  assert(f_window_scale == 5);
  Frame(kButton_Left, 0);
  assert(f_window_scale == 4);
  HashCheck(256, 0xD567B138u);
  HashCheck(684, 0x98853F70u);

  /* ── the panel dims with the screen ──────────────────────────────────── */

  /* The dim comes from INIDISP itself, so the panel tracks whichever fade is
   * running. Seven of fifteen levels scales every channel; force-blank is
   * full black. */
  inidisp_byte = 0x07;
  (void)RenderHash(256);
#if !PAUSE_OPTIONS_TEST_CALIBRATE
  assert(g_frame[35 * 256 + 100] == 0xFF04060Bu);     /* COL_BG 0A0E18 at 7/15 */
#endif
  inidisp_byte = 0x80;
  (void)RenderHash(256);
#if !PAUSE_OPTIONS_TEST_CALIBRATE
  assert(g_frame[0] == 0xFF000000u && g_frame[100 * 256 + 100] == 0xFF000000u);
#endif
  inidisp_byte = 0x0F;

  /* ── same-tab leave: a plain close ───────────────────────────────────── */

  /* Back to map while the guest is already on Map: the ROM's own L handler
   * is a total no-op there, so the panel just closes -- no SFX, no fade,
   * no tab hop. The pad press is swallowed and the guest stays on Map. */
  Frame(kButton_Down, 0);   /* Exit */
  Frame(kButton_Down, 0);   /* Back to map */
  const uint8 wp_before_back = sfx_writepos[0];
  const uint8 rp_before_back = sfx_readpos[0];
  assert(rp_before_back == wp_before_back);  /* pumped to empty by the frames above */
  Frame(kButton_A, 0);
  assert(!SmPauseOptions_IsActive() && !SmPauseOptions_Visible());
  assert(menu_index == 0 && pausemenu_button_label_mode == 0);
  /* A on the action row still plays the row-nav blip through the guest's own
   * queue (the nav loop fires it on any row); the close itself is quiet. */
  assert(sfx_readpos[0] == wp_before_back + 1 &&
         sfx_writepos[0] == wp_before_back + 1 &&
         sfx1_queue[wp_before_back] == 0x37);
  assert(screen_fade_delay == 0 && screen_fade_counter == 0);
  assert(inidisp_byte == 0x0F);

  /* B from Map is the same plain close. */
  Frame(kButton_L, 0);      /* re-open on Map */
  assert(SmPauseOptions_IsActive());
  Frame(kButton_B, 0);
  /* The close cancels the panel, not the fade: the enter fade it armed takes
   * its first step before the close runs, and keeps running afterwards. */
  assert(!SmPauseOptions_IsActive() && menu_index == 0 && inidisp_byte == 0x0E);

  /* ── a same-tab close mid enter-fade lets the fade finish ────────────── */

  /* Open again and close same-tab one frame into the down phase: the panel
   * goes, but the fade marker lives on and the screen's brightness
   * recovers when the fade completes. */
  OnMap();
  ResetFadeInputs();
  inidisp_byte = 0x0F;
  Frame(kButton_L, 0);
  Frame(0, 0);                       /* one step down: 14 */
  assert(inidisp_byte == 0x0E && screen_fade_delay == 1);
  Frame(kButton_L, 0);               /* L on Map: same-tab plain close */
  assert(!SmPauseOptions_IsActive());
  assert(screen_fade_delay == 1 && inidisp_byte == 0x0D);
  Frame(0, 0);
  assert(screen_fade_delay == 1 && inidisp_byte == 0x0C);
  for (unsigned f = 0; f < 44; ++f) {
    Frame(0, 0);
    if (inidisp_byte == 0x0F) break;
  }
  assert(inidisp_byte == 0x0F && screen_fade_delay == 0);

  /* ── different-tab leave: the guest's own tab change ─────────────────── */

  /* Opened over Equipment (R is the free shoulder there); L leaves for the
   * Map, the ROM's own Map->Equipment... reverse: PauseMenu_5's fade-out,
   * the guest's Load and FadeIn do the rest. */
  menu_index = 1;
  pausemenu_button_label_mode = 2;
  ResetFadeInputs();
  inidisp_byte = 0x0F;
  Frame(kButton_R, 0);
  assert(SmPauseOptions_IsActive() && menu_index == 1);
  assert(screen_fade_delay == 1 && screen_fade_counter == 0);
  /* Start passes through the panel: the pause screen beneath unpauses on
   * it, but only through the timed-held filter -- the press frame itself
   * produces no edge. */
  Frame(kButton_Start, kButton_A);
  assert(joypad1_newkeys == kButton_Start && joypad1_lastkeys == kButton_Start);
  assert(newly_held_down_timed_held_input == 0);
  /* The Start+A frame ran one enter-fade step: dimmed one level, the down
   * marker still set. (A on row 0 also toggled Widescreen on; it shows in
   * the golden below.) */
  assert(inidisp_byte == 0x0E && screen_fade_delay == 1);
  Frame(kButton_L, 0);
  /* The host's driver takes one last step on the leave press frame, then
   * LeaveTo hands the fade words to the guest's own tab change. */
  assert(inidisp_byte == 0x0D);
  /* The host's writes are the tab change: PauseMenu_5's fade-out, the Map
   * label lit, the highlight and its timer; the guest owns the fade words
   * from here (counter 1 keeps the press frame bright, delay 0 takes the
   * host's own driver off the words). */
  assert(SmPauseOptions_IsActive() && menu_index == 5);
  assert(pausemenu_button_label_mode == 0);
  assert(pausemenu_shoulder_button_highlight == 1 &&
         pausemenu_start_lr_pressed_highlight_timer == 5);
  assert(screen_fade_counter == 1 && screen_fade_delay == 0);
  /* The panel is still up (the guest is mid tab change, menu 5) and stays
   * up for the rest of it, riding the fade: at full brightness the frame is
   * a golden of the panel over the tab change, the footer's label highlight
   * now on the Map label. During the leave's down phase the panel is hidden
   * (the dim gate), so the golden is the after-leave picture, not a diff. */
  inidisp_byte = 0x0F;
  HashCheck(256, 0xD844BC75u);
  /* The Load after the fade-out closes the panel; the guest is on its way. */
  FadeOutDone();
  Frame(0, 0);
  SmPauseOptions_SyncToGuest();
  assert(!SmPauseOptions_IsActive() && !SmPauseOptions_Visible());
  assert(menu_index == 6);
  inidisp_byte = 0x0F;

  /* The reverse of the last phase: the map's free shoulder is R, so the
   * open is L, and the leave (R, toward Equipment) is the guest's own
   * map->equipment tab change -- PauseMenu_2's fade-out; the Load state
   * that follows closes the panel. */
  OnMap();
  ResetFadeInputs();
  inidisp_byte = 0x0F;
  Frame(kButton_L, 0);
  assert(SmPauseOptions_IsActive() && menu_index == 0);
  Frame(kButton_R, 0);   /* R on Map: the guest's own map->equipment change */
  assert(SmPauseOptions_IsActive() && menu_index == 2);
  assert(pausemenu_button_label_mode == 2 && pausemenu_shoulder_button_highlight == 2);
  assert(pausemenu_start_lr_pressed_highlight_timer == 5);
  assert(screen_fade_counter == 1 && screen_fade_delay == 0);
  /* The host's driver stepped the enter fade one level before the leave. */
  assert(inidisp_byte == 0x0E);
  /* The Load after the fade-out closes the panel; the guest is on its way. */
  FadeOutDone();
  Frame(0, 0);
  SmPauseOptions_SyncToGuest();
  assert(!SmPauseOptions_IsActive() && !SmPauseOptions_Visible());
  assert(menu_index == 3);
  inidisp_byte = 0x0F;

  /* ── the unpause fade keeps the panel up, dimmed ─────────────────────── */

  OnMap();
  ResetFadeInputs();
  inidisp_byte = 0x0F;
  Frame(kButton_L, 0);
  assert(SmPauseOptions_IsActive());
  game_state_byte = 0x10;
  SmPauseOptions_SyncToGuest();
  assert(SmPauseOptions_IsActive() && SmPauseOptions_Visible());
  game_state_byte = 0x11;
  SmPauseOptions_SyncToGuest();
  assert(!SmPauseOptions_IsActive() && !SmPauseOptions_Visible());
  game_state_byte = 0x0F;

  /* ── snapshots restore the flag; a restored panel opens on its row ───── */

  OnMap();
  ResetFadeInputs();
  inidisp_byte = 0x0F;
  Frame(kButton_L, 0);
  Frame(kButton_Down, 0);
  SmPauseOptions_SetActive(false);
  SmPauseOptions_SetActive(true);
  g_sm_video.enhanced = false; g_sm_video.hud_anchored = false;
  g_sm_video.fps_enabled = false; g_sm_video.aspect = SM_ASPECT_FIT;
  f_fullscreen = 0; f_vsync = 1; f_frame_blend = 0; f_run_ahead = 0;
  f_window_scale = 3; f_exit_requested = false; g_config_dirty = false;
  /* The two frames above armed and stepped the enter fade, which the dim
   * gate would hide and dim the golden with; a settled open leaves the
   * fade words at rest, so put them back. */
  inidisp_byte = 0x0F;
  screen_fade_delay = 0;
  screen_fade_counter = 0;
  /* The opening picture again. */
  HashCheck(256, 0x75F179BDu);

  /* ── the Start handoff ───────────────────────────────────────────────── */

  /* The pause screen acts only on its timed-held input, so a held Start
   * unpauses five frames after the press -- on a hold frame, several
   * frames into the in-flight enter fade. The host mirrors the filter's
   * next result and hands the fade off on exactly that frame. */
  OnMap();
  ResetFadeInputs();
  inidisp_byte = 0x0F;
  SmPauseOptions_SetActive(false);  /* the re-open phase left the panel up */
  Frame(kButton_L, 0);              /* open */
  Frame(0, 0);                      /* f1: 15 -> 14 */
  assert(inidisp_byte == 0x0E && screen_fade_delay == 1);
  Frame(kButton_Start, 0);          /* the press frame: no edge yet */
  assert(inidisp_byte == 0x0D && screen_fade_delay == 1);
  Frame(0, kButton_Start);          /* holds 2-4: the decay window */
  Frame(0, kButton_Start);
  Frame(0, kButton_Start);
  assert(inidisp_byte == 0x0A && screen_fade_delay == 1);
  Frame(0, kButton_Start);          /* hold 5: still decaying */
  assert(inidisp_byte == 0x09 && screen_fade_delay == 1);
  Frame(0, kButton_Start);          /* hold 6: the guest's edge */
  /* The host takes its driver off the words; the guest then unpauses on
   * this same frame with its own 1/1 slow fade and its own 0x38. */
  assert(screen_fade_delay == 0 && screen_fade_counter == 0);
  assert(inidisp_byte == 0x08);
  screen_fade_delay = 1;
  screen_fade_counter = 1;
  game_state_byte = 0x10;
  SmPauseOptions_SyncToGuest();
  assert(SmPauseOptions_IsActive() && SmPauseOptions_Visible());
  game_state_byte = 0x0F;

  /* A quick Start tap the filter will never turn into an edge leaves the
   * enter fade running to completion. */
  OnMap();
  ResetFadeInputs();
  inidisp_byte = 0x0F;
  SmPauseOptions_SetActive(false);  /* the re-open phase left the panel up */
  Frame(kButton_L, 0);
  Frame(0, 0);
  Frame(kButton_Start, 0);          /* press */
  Frame(0, kButton_Start);          /* one hold */
  Frame(0, 0);                      /* release: the filter resets, no edge */
  assert(screen_fade_delay == 1 && inidisp_byte == 0x0B);
  for (unsigned f = 0; f < 44 && inidisp_byte != 0x0F; ++f)
    Frame(0, 0);
  assert(inidisp_byte == 0x0F && screen_fade_delay == 0);

  /* At rest (fade already complete) the handoff is a no-op: the words were
   * already 0/0 and the guest's unpause just proceeds. */
  OnMap();
  ResetFadeInputs();
  inidisp_byte = 0x0F;
  Frame(kButton_Start, 0);
  Frame(0, kButton_Start);
  Frame(0, kButton_Start);
  Frame(0, kButton_Start);
  Frame(0, kButton_Start);
  Frame(0, kButton_Start);          /* hold 6: the edge */
  assert(screen_fade_delay == 0 && screen_fade_counter == 0 && inidisp_byte == 0x0F);
  game_state_byte = 0x10;
  SmPauseOptions_SyncToGuest();
  assert(SmPauseOptions_IsActive() && SmPauseOptions_Visible());
  game_state_byte = 0x0F;

  puts("sm_pause_options: ok");
  return 0;
}
