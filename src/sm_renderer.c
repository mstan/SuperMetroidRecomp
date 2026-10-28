#include "sm_renderer.h"
#ifndef SM_NATIVE_RASTER
#define SM_NATIVE_RASTER 0
#endif
#ifndef SM_NATIVE_ROW_PLAN
#define SM_NATIVE_ROW_PLAN 0
#endif
#ifndef SM_NATIVE_CENTER_REUSE
#define SM_NATIVE_CENTER_REUSE 0
#endif
#if SM_NATIVE_RASTER
#include "snes/raster_register_jobs.h"
#include "snes/raster_image.h"
#endif
#include "raster_snapshot.h"
#include "sm_mode7.h"
#include <string.h>
#include <math.h>
#include <stddef.h>

/* Immutable host snapshots. Presenting never invokes guest code or writes PPU,
 * WRAM, VRAM or OAM. Each visible line owns its memory image so raster DMA
 * cannot retroactively change an earlier line. */
typedef struct SmRasterLine {
  uint8_t registers[PPU_SAVESTATE_REGS_SIZE];
  uint16_t palette[256], oam[256];
  uint8_t high_oam[32];
  const uint16_t *vram;
} SmRasterLine;
typedef struct SmSourceFrame {
  SmRasterLine lines[224];
  uint8_t ram[0x20000];
  uint32_t stock[256 * 224];
  unsigned number, captured;
  bool valid;
  uint8_t object_ram[0x20000];
  bool object_state_valid;
} SmSourceFrame;
/* Diagnostic disk records contain full images, independently of the in-memory
 * representation. Stream them without allocating a 15 MB temporary frame. */
typedef struct SmDiskRasterLine {
  uint8_t registers[PPU_SAVESTATE_REGS_SIZE];
  uint16_t palette[256], oam[256], vram[0x8000];
  uint8_t high_oam[32];
} SmDiskRasterLine;
typedef struct SmDiskFrame {
  SmDiskRasterLine lines[224];
  uint8_t ram[0x20000];
  uint32_t stock[256 * 224];
  unsigned number, captured;
  bool valid;
  uint8_t object_ram[0x20000];
  bool object_state_valid;
} SmDiskFrame;
#define SM_CAPTURE_TAIL_BYTES (sizeof(SmDiskFrame) - offsetof(SmDiskFrame, ram))
_Static_assert(sizeof(SmDiskRasterLine) == PPU_SAVESTATE_REGS_SIZE + 0x10000 + 1056,
    "capture line records have no implicit padding");
_Static_assert(offsetof(SmSourceFrame, object_state_valid) - offsetof(SmSourceFrame, ram) ==
    offsetof(SmDiskFrame, object_state_valid) - offsetof(SmDiskFrame, ram),
    "capture metadata has the disk field layout");
_Static_assert(SM_CAPTURE_TAIL_BYTES <= sizeof(SmSourceFrame) - offsetof(SmSourceFrame, ram),
    "capture metadata fits the source frame");
static SmSourceFrame frames[2];
/* Current and previous frames have independent allocation lifetimes. The old
 * frame remains readable by interpolation while the next frame is captured. */
static SnesRasterSnapshotStore vram_snapshots[2];
static bool capture_failed[2];
static struct {
  const Ppu *ppu;
  uint32_t writes;
  bool tracking, uncertain;
} hardware_raster;
#ifndef SM_RASTER_FULL_COPY
#define SM_RASTER_FULL_COPY 0
#endif
static unsigned current;
#if SM_NATIVE_RASTER
static SnesRasterImage stock_images[2];
static bool native_capture;
#if SM_NATIVE_CENTER_REUSE
/* Live-provider provenance, excluded from disk captures and guest state.
 * Loading arbitrary captured stock never grants permission to reuse it. */
static const Ppu *native_center_source;
static bool native_centers[2][224];
#endif
static bool read_stock_line(const void *context, unsigned y, SnesRasterImageLine *out) {
  const SmSourceFrame *f = context;
  if (y >= f->captured) return false;
  const SmRasterLine *l = &f->lines[y];
  *out = (SnesRasterImageLine){l->registers, l->high_oam, l->palette, l->oam, l->vram};
  return true;
}
static bool materialize_stock(const SmSourceFrame *f) {
  if (!f->valid) return false;
  unsigned index = f == &frames[0] ? 0 : 1;
  return SnesRasterImageMaterialize(&stock_images[index], frames[index].stock,
      read_stock_line, f);
}
#endif
static SnesRenderRegisters scanout;
static const uint8_t *renderer_rom;
static size_t renderer_rom_size;
static bool captured_mode7;
static bool captured_mosaic;
static bool captured_motion;
static bool captured_edge;
static bool captured_effect;
static bool captured_grapple;
static bool captured_rotation;
static struct { unsigned position, attr, high; int dx, dy; bool valid; } grapple_motion[128];
static unsigned captured_power_bomb_phases;
static unsigned power_bomb_phase_observations[5];
static unsigned captured_rooms[128], captured_room_count, stable_room_key, stable_room_frames;
static bool has_edge_motion(const SmSourceFrame *f, const SmSourceFrame *old);
static int moving_effect_slot(const SmSourceFrame *f, const SmSourceFrame *old);
static SmRendererStats render_stats;
static uint8_t latched_object_ram[0x20000];
static bool object_state_latched;
void SmRendererLatchObjectState(const uint8_t ram[0x20000]) {
  memcpy(latched_object_ram, ram, sizeof(latched_object_ram));
  object_state_latched = true;
}
SmRendererStats SmRendererGetStats(void) { return render_stats; }
SmRasterStats SmRendererGetRasterStats(void) {
  const SnesRasterSnapshotStore *s = &vram_snapshots[current];
  return (SmRasterStats){s->copied_bytes,
      vram_snapshots[0].allocated * vram_snapshots[0].image_bytes +
      vram_snapshots[1].allocated * vram_snapshots[1].image_bytes,
      sizeof(frames), s->compared_bytes, s->count};
}
const uint32_t *SmRendererStockFrame(void) {
#if SM_NATIVE_RASTER
  if (!materialize_stock(&frames[current])) return NULL;
#endif
  return frames[current].valid ? frames[current].stock : NULL;
}
void SmRendererSetRom(const uint8_t *rom, size_t size) {
  renderer_rom = rom;
  renderer_rom_size = size;
}
static const uint8_t *rom_bytes(unsigned bank, unsigned address, size_t size) {
  if (!renderer_rom || address < 0x8000 || address > 0xffff ||
      size > 0x10000 - address) return NULL;
  size_t offset = ((bank & 0x7f) << 15) | (address & 0x7fff);
  if (offset > renderer_rom_size || size > renderer_rom_size - offset) return NULL;
  return renderer_rom + offset;
}

static bool save_capture(const SmSourceFrame *frame, const char *path) {
  if (!path || !frame->valid) return false;
#if SM_NATIVE_RASTER
  if (!materialize_stock(frame)) return false;
#endif
  FILE *file = fopen(path, "wb");
  if (!file) return false;
  const uint32_t header[3] = {0x534d5243, 2, sizeof(SmDiskFrame)};
  bool ok = fwrite(header, sizeof(header), 1, file) == 1;
  for (unsigned y = 0; ok && y < 224; ++y) {
    const SmRasterLine *line = &frame->lines[y];
    ok = fwrite(line->registers, sizeof(line->registers), 1, file) == 1 &&
         fwrite(line->palette, sizeof(line->palette), 1, file) == 1 &&
         fwrite(line->oam, sizeof(line->oam), 1, file) == 1 &&
         fwrite(line->vram, 0x10000, 1, file) == 1 &&
         fwrite(line->high_oam, sizeof(line->high_oam), 1, file) == 1;
  }
  if (ok) ok = fwrite((const unsigned char *)frame + offsetof(SmSourceFrame, ram),
      SM_CAPTURE_TAIL_BYTES, 1, file) == 1;
  return fclose(file) == 0 && ok;
}
bool SmRendererSaveCapture(const char *path) {
  return save_capture(&frames[current], path);
}
bool SmRendererLoadCapture(const char *path) {
#if SM_NATIVE_RASTER && SM_NATIVE_CENTER_REUSE
  memset(native_centers[current ^ 1], 0, sizeof(native_centers[0]));
#endif
#if SM_NATIVE_RASTER
  SnesRasterImageReset(&stock_images[current ^ 1]);
#endif
  if (!path) return false;
  FILE *file = fopen(path, "rb");
  if (!file) return false;
  uint32_t header[3];
  SmSourceFrame *next = &frames[current ^ 1];
  bool ok = fread(header, sizeof(header), 1, file) == 1 &&
            header[0] == 0x534d5243 && header[1] == 2 &&
            header[2] == sizeof(SmDiskFrame);
  next->valid = false;
  uint16_t *source = ok ? malloc(0x10000) : NULL;
  ok = ok && source && SnesRasterSnapshotBegin(&vram_snapshots[current ^ 1], 0x10000, 224);
  for (unsigned y = 0; ok && y < 224; ++y) {
    SmRasterLine *line = &next->lines[y];
    ok = fread(line->registers, sizeof(line->registers), 1, file) == 1 &&
         fread(line->palette, sizeof(line->palette), 1, file) == 1 &&
         fread(line->oam, sizeof(line->oam), 1, file) == 1 &&
         fread(source, 0x10000, 1, file) == 1 &&
         fread(line->high_oam, sizeof(line->high_oam), 1, file) == 1;
    if (ok) {
      line->vram = SnesRasterSnapshotCapture(&vram_snapshots[current ^ 1], source, !SM_RASTER_FULL_COPY);
      ok = line->vram != NULL;
    }
  }
  free(source);
  if (ok) ok = fread((unsigned char *)next + offsetof(SmSourceFrame, ram),
      SM_CAPTURE_TAIL_BYTES, 1, file) == 1 &&
      *((const unsigned char *)next + offsetof(SmSourceFrame, valid)) == 1 &&
      *((const unsigned char *)next + offsetof(SmSourceFrame, object_state_valid)) <= 1 &&
      next->captured == 224 && fgetc(file) == EOF;
  fclose(file);
  capture_failed[current ^ 1] = !ok;
  if (ok) current ^= 1;
  else next->valid = false;
  return ok;
}

static unsigned read16(const uint8_t *ram, unsigned address) {
  return ram[address] | (ram[address + 1] << 8);
}
static bool room_state(unsigned state) {
  /* Unpausing begins with a menu fade (16), then restores room graphics
   * under forced blank (17). The room is ready for scanout in state 18. */
  return (state >= 7 && state <= 13) || (state >= 18 && state <= 25) ||
         state == 27 || (state >= 32 && state <= 38) || state == 42;
}
bool SmRendererPowerBombWidths(const uint8_t profile[160], unsigned radius,
                              int16_t widths[192]) {
  if (!profile || !widths || radius > 255) return false;
  int16_t result[192];
  for (unsigned row = 0; row < 192; ++row) result[row] = -1;
  int row = (int)(radius * profile[128] >> 8);
  if (row >= 192) return false;
  int width = 0;
  for (unsigned point = 96; point < 128; ++point) {
    int end = (int)(radius * profile[point + 32] >> 8);
    if (end > row) return false;
    width = (int)(radius * profile[point] >> 8);
    /* Retail includes both endpoints; the next segment overwrites their
     * shared row. Preserve this detail and the final centerward fill. */
    for (int y = row; y >= end; --y) result[y] = (int16_t)width;
    row = end;
  }
  for (; row >= 0; --row) result[row] = (int16_t)width;
  memcpy(widths, result, sizeof(result));
  return true;
}
static int lerp_pixel(int old, int now, double alpha) {
  return (int)lround(old + (now - old) * alpha);
}
static bool continuous_frames(const SmSourceFrame *old, const SmSourceFrame *now) {
  return old->valid && old->number + 1 == now->number &&
      read16(old->ram, 0x79b) == read16(now->ram, 0x79b) &&
      read16(old->ram, 0x998) == read16(now->ram, 0x998) &&
      abs((int)read16(old->ram, 0x911) - (int)read16(now->ram, 0x911)) <= 32 &&
      abs((int)read16(old->ram, 0x915) - (int)read16(now->ram, 0x915)) <= 32;
}

/* Match an actual Samus ROM map entry, not a screen proximity or OAM-slot
 * heuristic. Echoes, projectiles and reused reservations stay discrete unless
 * their coordinates and tile identity genuinely match the player map. */
static bool samus_cannon_piece(const SmSourceFrame *f, unsigned position,
                               unsigned attr, unsigned high) {
  const uint8_t *ram = f->object_ram;
  unsigned pose = read16(ram, 0xa1c), frame = read16(ram, 0xa96);
  unsigned arm = read16(ram, 0xaa8), mode = read16(ram, 0xaac) & 15;
  if ((attr & 0x3fff) != 0x281f || (high & 3) || !f->object_state_valid ||
      pose >= 253 || frame >= 64 || !arm || arm > 4 || (mode != 1 && mode != 2)) return false;
  /* $90:C663 emits this separate 8x8 piece; it is not in either $92 Samus
   * body map. Verify its pose/animation offset and exact character flags. */
  const uint8_t *table = rom_bytes(0x90, 0xc7df + pose * 2, 2);
  const uint8_t *params = rom_bytes(0x91, 0xb629 + pose * 8, 8);
  if (!table || !params) return false;
  unsigned address = read16(table, 0);
  const uint8_t *data = rom_bytes(0x90, address, 3);
  if (!data) return false;
  unsigned direction = (data[0] & 128) && frame ? data[2] & 127 : data[0] & 127;
  const uint8_t *character = rom_bytes(0x90, 0xc791 + direction * 2, 2);
  const uint8_t *offset = rom_bytes(0x90, address + (data[0] & 128 ? 4 : 2) + frame * 2, 2);
  if (direction >= 10 || !character || !offset || read16(character, 0) != attr) return false;
  int x = (int16_t)(uint16_t)(read16(ram, 0xaf6) - read16(ram, 0x911) + (int8_t)offset[0]);
  int y = (int16_t)(uint16_t)(read16(ram, 0xafa) - read16(ram, 0x915) - params[4] + (int8_t)offset[1]);
  return x >= 0 && x < 256 && y >= 0 && y < 256 && position == (unsigned)(x | (y << 8));
}
static bool samus_piece(const SmSourceFrame *f, unsigned position,
                        unsigned attr, unsigned high) {
  if (samus_cannon_piece(f, position, attr, high)) return true;
  for (unsigned half = 0; half < 2; ++half) {
    unsigned index = read16(f->object_ram, 0xac8 + half * 2);
    if (index > 0x7fff || (half && !index)) continue;
    const uint8_t *table = rom_bytes(0x92, 0x808d + index * 2, 2);
    if (!table) continue;
    unsigned map = read16(table, 0);
    const uint8_t *data = rom_bytes(0x92, map, 2);
    if (!data) continue;
    unsigned count = read16(data, 0);
    if (!count || count > 128) continue;
    data = rom_bytes(0x92, map + 2, count * 5);
    if (!data) continue;
    for (unsigned i = 0; i < count; ++i) {
      const uint8_t *entry = data + i * 5;
      unsigned offset = read16(entry, 0);
      unsigned x = (read16(f->object_ram, 0xb04) + offset) & 511;
      unsigned y = (read16(f->object_ram, 0xb06) + entry[2]) & 255;
      if (attr == read16(entry, 3) &&
          x == ((position & 255) | ((high & 1) << 8)) &&
          y == (position >> 8) && (offset >> 15) == ((high >> 1) & 1))
        return true;
    }
  }
  return false;
}
static bool map_piece(unsigned bank, unsigned map, int x, int y,
                      unsigned palette, unsigned base, unsigned position,
                      unsigned attr, unsigned high, int *actual_x) {
  const uint8_t *data = rom_bytes(bank, map, 2);
  if (!data) return false;
  unsigned count = read16(data, 0);
  if (!count || count > 128) return false;
  data = rom_bytes(bank, map + 2, count * 5);
  if (!data) return false;
  for (unsigned i = 0; i < count; ++i) {
    const uint8_t *entry = data + i * 5;
    unsigned offset = read16(entry, 0);
    if (((x + offset) & 511) == ((position & 255) | ((high & 1) << 8)) &&
        ((y + entry[2]) & 255) == (position >> 8) &&
        (offset >> 15) == ((high >> 1) & 1) &&
        (uint16_t)(palette | (base + read16(entry, 3))) == attr) {
      int dx = (int)(offset & 511);
      if (dx & 256) dx -= 512;
      *actual_x = x + dx;
      return true;
    }
  }
  return false;
}
static bool enemy_piece(const SmSourceFrame *f, unsigned slot,
                        unsigned position, unsigned attr, unsigned high, int *actual_x) {
  const uint8_t *ram = f->object_ram, *e = ram + 0xf78 + slot * 64;
  if (!read16(e, 0) || read16(e, 0) == 0xdaff || (read16(e, 14) & 0x300))
    return false;
  /* Shaking is intentionally discrete: its timer is changed during OAM
   * construction, so the owner snapshot alone cannot prove its phase. */
  if (read16(e, 42)) return false;
  int x = (int)read16(e, 2) - (int)read16(ram, 0x911) +
          (int16_t)read16(ram, 0x7010 + slot * 64);
  int y = (int)read16(e, 6) - (int)read16(ram, 0x915) +
          (int16_t)read16(ram, 0x7012 + slot * 64);
  unsigned palette = read16(e, 30), frozen = read16(e, 38);
  if (read16(e, 36) && (read16(ram, 0xe44) & 2)) palette = 0;
  else if (frozen && (frozen >= 0x5a || (frozen & 2))) palette = 3072;
  unsigned bank = e[46], map = read16(e, 22), base = read16(e, 32);
  if (!(read16(e, 16) & 4))
    return map_piece(bank, map, x, y, palette, base, position, attr, high, actual_x);
  const uint8_t *ext = rom_bytes(bank, map, 2);
  if (!ext) return false;
  unsigned count = read16(ext, 0);
  if (!count || count > 64) return false;
  ext = rom_bytes(bank, map + 2, count * 8);
  if (!ext) return false;
  for (unsigned i = 0; i < count; ++i) {
    const uint8_t *child = ext + i * 8;
    if (map_piece(bank, read16(child, 4), x + (int16_t)read16(child, 0),
                  y + (int16_t)read16(child, 2), palette, base, position, attr, high, actual_x))
      return true;
  }
  return false;
}
static bool enemy_motion(const SmSourceFrame *f, const SmSourceFrame *old,
                         unsigned slot, int *dx, int *dy) {
  if (!old || !f->object_state_valid || !old->object_state_valid) return false;
  const uint8_t *e = f->object_ram + 0xf78 + slot * 64;
  const uint8_t *before = old->object_ram + 0xf78 + slot * 64;
  if (!read16(e, 0) || read16(e, 0) != read16(before, 0) || e[46] != before[46] ||
      read16(e, 22) < 0x8000 || read16(before, 22) < 0x8000 ||
      ((read16(e, 16) ^ read16(before, 16)) & 4) ||
      ((read16(e, 14) | read16(before, 14)) & 0x300) ||
      read16(e, 42) || read16(before, 42)) return false;
  int world_dx = (int)read16(before, 2) - (int)read16(e, 2);
  int world_dy = (int)read16(before, 6) - (int)read16(e, 6);
  *dx = world_dx + (int)read16(f->object_ram, 0x911) - (int)read16(old->object_ram, 0x911) +
      (int16_t)read16(old->object_ram, 0x7010 + slot * 64) -
      (int16_t)read16(f->object_ram, 0x7010 + slot * 64);
  *dy = world_dy + (int)read16(f->object_ram, 0x915) - (int)read16(old->object_ram, 0x915) +
      (int16_t)read16(old->object_ram, 0x7012 + slot * 64) -
      (int16_t)read16(f->object_ram, 0x7012 + slot * 64);
  return abs(world_dx) <= 32 && abs(world_dy) <= 32 && abs(*dx) <= 32 && abs(*dy) <= 32;
}
static bool has_samus_motion(const SmSourceFrame *f, const SmSourceFrame *old) {
  const SmRasterLine *line = &f->lines[100], *previous = &old->lines[100];
  for (unsigned slot = 0; slot < 128; ++slot) {
    unsigned a = previous->oam[slot * 2], b = line->oam[slot * 2];
    unsigned attr = line->oam[slot * 2 + 1];
    unsigned ah = previous->high_oam[slot / 4] >> ((slot % 4) * 2);
    unsigned bh = line->high_oam[slot / 4] >> ((slot % 4) * 2);
    if (a == b || attr != previous->oam[slot * 2 + 1] || ((ah ^ bh) & 2)) continue;
    if (abs((int)(a & 255) - (int)(b & 255)) > 32 ||
        abs((int)(a >> 8) - (int)(b >> 8)) > 32) continue;
    if (samus_piece(old, a, attr, ah) && samus_piece(f, b, attr, bh)) return true;
  }
  return false;
}
void SmRendererReset(void) {
#if SM_NATIVE_RASTER && SM_NATIVE_CENTER_REUSE
  native_center_source = NULL;
  memset(native_centers, 0, sizeof(native_centers));
#endif
#if SM_NATIVE_RASTER
  SnesRasterImageReset(&stock_images[0]);
  SnesRasterImageReset(&stock_images[1]);
  native_capture = false;
#endif
  memset(frames, 0, sizeof(frames));
  hardware_raster.tracking=false;
  capture_failed[0] = capture_failed[1] = false;
  current = 0;
  captured_mode7 = false;
  captured_mosaic = false;
  captured_motion = false;
  captured_edge = false;
  captured_effect = false;
  captured_grapple = false;
  captured_rotation = false;
  captured_power_bomb_phases = 0;
  memset(power_bomb_phase_observations, 0, sizeof(power_bomb_phase_observations));
  captured_room_count = stable_room_key = stable_room_frames = 0;
  object_state_latched = false;
}
void SmRendererBeginFrame(const uint8_t ram[0x20000], unsigned number) {
  hardware_raster.tracking=false;
  current ^= 1;
#if SM_NATIVE_RASTER && SM_NATIVE_CENTER_REUSE
  native_center_source = NULL;
  memset(native_centers[current], 0, sizeof(native_centers[0]));
#endif
#if SM_NATIVE_RASTER
  SnesRasterImageReset(&stock_images[current]);
#endif
  SmSourceFrame *f = &frames[current];
  f->valid = false;
  f->captured = 0;
  capture_failed[current] = !SnesRasterSnapshotBegin(&vram_snapshots[current], 0x10000, 224);
  f->number = number;
  memcpy(f->ram, ram, sizeof(f->ram));
  memcpy(f->object_ram, object_state_latched ? latched_object_ram : ram, sizeof(f->object_ram));
  /* The fallback copy is useful for discrete inspection, but is not the
   * pre-NMI owner state required to prove sprite interpolation. */
  f->object_state_valid = object_state_latched;
  object_state_latched = false;
}
void SmRendererBeginHardwareRaster(const Ppu *p) {
#if SM_NATIVE_RASTER && SM_NATIVE_CENTER_REUSE
  native_center_source = p;
#endif
#if SM_NATIVE_RASTER
  native_capture = p && !frames[current].valid && !frames[current].captured;
  if (native_capture) SnesRasterImageBegin(&stock_images[current], p);
#endif
  hardware_raster.tracking=false;
#if SM_RASTER_TRACK_WRITES && !SNESRECOMP_TRACE
  if(p) {
    hardware_raster.ppu=p;hardware_raster.writes=p->vramWriteCount;
    hardware_raster.uncertain=true;
    hardware_raster.tracking=!p->widescreenLineEnhancer;
  }
#else
  (void)p;
#endif
}
void SmRendererEndHardwareRaster(void) {
#if SM_NATIVE_RASTER && SM_NATIVE_CENTER_REUSE
  native_center_source = NULL;
#endif
  hardware_raster.tracking=false;
#if SM_NATIVE_RASTER
  native_capture = false;
#endif
}
unsigned SmRendererNativeLines(void) {
#if SM_NATIVE_RASTER
  return stock_images[current].count;
#else
  return 0;
#endif
}
bool SmRendererClaimNativeLine(const Ppu *p, unsigned line) {
#if SM_NATIVE_RASTER
  const SmSourceFrame *f = &frames[current];
#if SM_NATIVE_CENTER_REUSE
  if (line >= 1 && line <= 224 && native_centers[current][line - 1]) return false;
#endif
  if (!native_capture || capture_failed[current] || line < 1 || line > 224 ||
      f->captured != line || !SnesRasterImageCanDefer(p) ||
      !room_state(read16(f->ram, 0x998)) || (p->bgmode & 7) != 1 || (p->mosaic >> 4)) return false;
  /* The HUD copies original OBJ/BG pixels as an opaque screen-space band. */
  if (line <= 32 && p->screenEnabled[0] == 4 && !p->cgadsub && !p->cgwsel) return false;
  return SnesRasterImageClaim(&stock_images[current], line);
#else
  (void)p; (void)line; return false;
#endif
}
void SmRendererHardwareVramChanged(void) { hardware_raster.uncertain=true; }
void SmRendererCaptureLine(const Ppu *p, unsigned line) {
  if (!p || capture_failed[current] || line < 1 || line > 224) return;
  SmSourceFrame *f = &frames[current];
  if (line != f->captured + 1) return;
#if SM_NATIVE_RASTER && SM_NATIVE_CENTER_REUSE
  /* The current compositor draws all OAM entries. A hardware-limited source
   * is therefore not a compatible provider. Do not change guest/status flags
   * to obtain one; retain the existing deferred/full-custom path instead. */
  native_centers[current][line - 1] = native_capture && native_center_source == p &&
      SnesRasterImageCanDefer(p) && room_state(read16(f->ram, 0x998)) &&
      (p->renderFlags & (kPpuRenderFlags_NewRenderer | kPpuRenderFlags_NoSpriteLimits)) ==
          (kPpuRenderFlags_NewRenderer | kPpuRenderFlags_NoSpriteLimits) &&
      (g_snes_ppu_dbg_layer_mask & 31) == 31 && !(p->oamaddh & 0x80) &&
      (p->bgmode & 7) == 1 && !(p->mosaic >> 4);
#endif
  SmRasterLine *l = &f->lines[line - 1];
  memcpy(l->registers, p, sizeof(l->registers));
  memcpy(l->palette, p->cgram, sizeof(l->palette));
  memcpy(l->oam, p->oam, sizeof(l->oam));
  memcpy(l->high_oam, p->highOam, sizeof(l->high_oam));
#if SM_RASTER_TRACK_WRITES
  if(p!=hardware_raster.ppu || p->widescreenLineEnhancer)hardware_raster.tracking=false;
  if(!SM_RASTER_FULL_COPY && hardware_raster.tracking && !hardware_raster.uncertain &&
      f->captured && hardware_raster.writes==p->vramWriteCount)
    l->vram=vram_snapshots[current].images[vram_snapshots[current].count-1];
  else
#endif
    l->vram = SnesRasterSnapshotCapture(&vram_snapshots[current], p->vram, !SM_RASTER_FULL_COPY);
#if SM_RASTER_TRACK_WRITES
  hardware_raster.writes=p->vramWriteCount;hardware_raster.uncertain=false;
#endif
  if (!l->vram) {
#if SM_NATIVE_RASTER
    if (stock_images[current].count && !SnesRasterImageRecoverPrefix(
        &stock_images[current], f->stock, p, read_stock_line, f, f->captured)) abort();
#endif
    capture_failed[current] = true; return;
  }
  ++f->captured;
}
bool SmRendererEndFrame(const uint32_t stock[256 * 224]) {
  SmSourceFrame *f = &frames[current];
  if (!stock || capture_failed[current] || f->captured != 224) return false;
  memcpy(f->stock, stock, sizeof(f->stock));
  f->valid = true;
  const char *motion_prefix = getenv("SM_CAPTURE_MOTION_PREFIX");
  const char *motion_after = getenv("SM_CAPTURE_MOTION_AFTER");
  const SmSourceFrame *previous = &frames[current ^ 1];
  const char *rotation_prefix = getenv("SM_CAPTURE_ROTATION_PREFIX");
  if (rotation_prefix && !captured_rotation && continuous_frames(previous, f) &&
      room_state(read16(f->ram, 0x998)) && (read16(f->ram, 0x93f) & 0x8000)) {
    SnesRenderRegisters now, before;
    memcpy(&now, f->lines[100].registers, PPU_SAVESTATE_REGS_SIZE);
    memcpy(&before, previous->lines[100].registers, PPU_SAVESTATE_REGS_SIZE);
    if ((now.bgmode & 7) == 7 && (before.bgmode & 7) == 7 && now.inidisp == 15 && before.inidisp == 15 &&
        abs((int16_t)now.m7matrix[1]) >= 16 &&
        memcmp(now.m7matrix, before.m7matrix, 4 * sizeof(now.m7matrix[0]))) {
      char path[1024];
      int n = snprintf(path, sizeof(path), "%s.previous.capture", rotation_prefix);
      bool saved = n > 0 && n < (int)sizeof(path) && save_capture(previous, path);
      n = snprintf(path, sizeof(path), "%s.current.capture", rotation_prefix);
      if (saved && n > 0 && n < (int)sizeof(path) && save_capture(f, path)) {
        captured_rotation = true;
        fprintf(stderr, "SM rotation capture: frame=%u room=%04x A=%d B=%d\n", f->number,
                read16(f->ram, 0x79b), (int16_t)now.m7matrix[0], (int16_t)now.m7matrix[1]);
      }
    }
  }
  const char *grapple_prefix = getenv("SM_CAPTURE_GRAPPLE_PREFIX");
  const char *grapple_min_env = getenv("SM_CAPTURE_GRAPPLE_MIN_LENGTH");
  unsigned grapple_min = grapple_min_env ? (unsigned)atoi(grapple_min_env) : 32;
  if (!grapple_min || grapple_min > 127) grapple_min = 32;
  if (grapple_prefix && !captured_grapple && continuous_frames(previous, f) &&
      room_state(read16(f->ram, 0x998)) &&
      read16(f->object_ram, 0xd32) != 0xc4f0 &&
      read16(f->object_ram, 0xcfe) >= grapple_min && read16(f->object_ram, 0xcfe) < 128) {
    SnesRenderRegisters raster;
    memcpy(&raster, f->lines[64].registers, PPU_SAVESTATE_REGS_SIZE);
    if (raster.inidisp == 15 && (raster.screenEnabled[0] & 16)) {
      char path[1024];
      int n = snprintf(path, sizeof(path), "%s.previous.capture", grapple_prefix);
      bool saved = n > 0 && n < (int)sizeof(path) && save_capture(previous, path);
      n = snprintf(path, sizeof(path), "%s.current.capture", grapple_prefix);
      if (saved && n > 0 && n < (int)sizeof(path) && save_capture(f, path)) {
        captured_grapple = true;
        fprintf(stderr, "SM grapple capture: frame=%u length=%u function=%04x\n", f->number,
                read16(f->object_ram, 0xcfe), read16(f->object_ram, 0xd32));
      }
    }
  }
  const char *rooms_prefix = getenv("SM_CAPTURE_ROOMS_PREFIX");
  if (rooms_prefix && captured_room_count < 128) {
    SnesRenderRegisters raster;
    memcpy(&raster, f->lines[64].registers, PPU_SAVESTATE_REGS_SIZE);
    unsigned mode = raster.bgmode & 7;
    unsigned key = read16(f->ram, 0x79b) | (mode << 16);
    bool ready = room_state(read16(f->ram, 0x998)) && (mode == 1 || mode == 7);
    for (unsigned y = 32; ready && y < 224; ++y) {
      memcpy(&raster, f->lines[y].registers, PPU_SAVESTATE_REGS_SIZE);
      ready = raster.inidisp == 15 && (raster.bgmode & 7) == mode;
    }
    if (!ready || key != stable_room_key) stable_room_frames = 0;
    stable_room_key = key;
    if (ready && ++stable_room_frames == 5) {
      bool seen = false;
      for (unsigned i = 0; i < captured_room_count; ++i) seen |= captured_rooms[i] == key;
      if (!seen) {
        char path[1024];
        int n = snprintf(path, sizeof(path), "%s.%04x.m%u.capture", rooms_prefix, key & 0xffff, mode);
        if (n > 0 && n < (int)sizeof(path) && save_capture(f, path)) {
          captured_rooms[captured_room_count++] = key;
          fprintf(stderr, "SM room capture: frame=%u room=%04x mode=%u\n", f->number, key & 0xffff, mode);
        }
      }
    }
  }
  const char *power_bomb_prefix = getenv("SM_CAPTURE_POWERBOMB_PREFIX");
  if (power_bomb_prefix && (read16(f->ram, 0x592) & 0x8000)) {
    static const unsigned phases[] = {0x90df, 0x91a8, 0x8de9, 0x8eb2, 0x8b98};
    for (unsigned slot = 0; slot < 6; ++slot) {
      unsigned instruction = read16(f->ram, 0x18f0 + slot * 2);
      for (unsigned phase = 0; phase < 5; ++phase) {
        if (instruction != phases[phase] || (captured_power_bomb_phases & (1u << phase))) continue;
        /* Setup and the first generated table can precede the next NMI's
         * pointer latch. Retain a later observation for raster comparison. */
        if (++power_bomb_phase_observations[phase] < 3) continue;
        char path[1024];
        int n = snprintf(path, sizeof(path), "%s.%04x.capture", power_bomb_prefix, instruction);
        if (n > 0 && n < (int)sizeof(path) && save_capture(f, path)) {
          captured_power_bomb_phases |= 1u << phase;
          fprintf(stderr, "SM power-bomb capture: frame=%u phase=%04x slot=%u\n", f->number, instruction, slot);
        }
      }
    }
  }
  const char *effect_prefix = getenv("SM_CAPTURE_EFFECT_PREFIX");
  if (effect_prefix && !captured_effect && continuous_frames(previous, f) &&
      room_state(read16(f->ram, 0x998))) {
    int slot = moving_effect_slot(f, previous);
    if (slot >= 0) {
      char path[1024];
      int n = snprintf(path, sizeof(path), "%s.previous.capture", effect_prefix);
      bool saved = n > 0 && n < (int)sizeof(path) && save_capture(previous, path);
      n = snprintf(path, sizeof(path), "%s.current.capture", effect_prefix);
      captured_effect = saved && n > 0 && n < (int)sizeof(path) && save_capture(f, path);
      if (captured_effect)
        fprintf(stderr, "SM effect capture: frames=%u/%u B4_slot=%d\n", previous->number, f->number, slot);
    }
  }
  const char *edge_prefix = getenv("SM_CAPTURE_EDGE_PREFIX");
  if (edge_prefix && !captured_edge && continuous_frames(previous, f) &&
      room_state(read16(f->ram, 0x998)) && has_edge_motion(f, previous)) {
    char path[1024];
    int n = snprintf(path, sizeof(path), "%s.previous.capture", edge_prefix);
    bool saved = n > 0 && n < (int)sizeof(path) && save_capture(previous, path);
    n = snprintf(path, sizeof(path), "%s.current.capture", edge_prefix);
    captured_edge = saved && n > 0 && n < (int)sizeof(path) && save_capture(f, path);
  }
  if (motion_prefix && !captured_motion &&
      (!motion_after || f->number >= strtoul(motion_after, NULL, 10)) &&
      continuous_frames(previous, f) &&
      room_state(read16(f->ram, 0x998)) &&
      read16(f->object_ram, 0xa1c) == read16(previous->object_ram, 0xa1c) &&
      has_samus_motion(f, previous) &&
      (read16(f->object_ram, 0xb04) != read16(previous->object_ram, 0xb04) ||
       read16(f->object_ram, 0xb06) != read16(previous->object_ram, 0xb06))) {
    char path[1024];
    int n = snprintf(path, sizeof(path), "%s.previous.capture", motion_prefix);
    bool saved = n > 0 && n < (int)sizeof(path) && save_capture(previous, path);
    n = snprintf(path, sizeof(path), "%s.current.capture", motion_prefix);
    captured_motion = saved && n > 0 && n < (int)sizeof(path) && save_capture(f, path);
  }
  const char *mode7_path = getenv("SM_CAPTURE_MODE7");
  if (mode7_path && !captured_mode7 && room_state(read16(f->ram, 0x998))) {
    for (unsigned y = 32; y < 224; ++y) {
      const uint8_t *regs = f->lines[y].registers;
      if ((regs[4] & 7) == 7 && regs[0] == 15) {
        captured_mode7 = SmRendererSaveCapture(mode7_path);
        break;
      }
    }
  }
  const char *mosaic_path = getenv("SM_CAPTURE_MOSAIC");
  if (mosaic_path && !captured_mosaic && room_state(read16(f->ram, 0x998))) {
    for (unsigned y = 32; y < 224; ++y) {
      SnesRenderRegisters raster;
      memcpy(&raster, f->lines[y].registers, PPU_SAVESTATE_REGS_SIZE);
      if ((raster.bgmode & 7) == 1 && !(raster.inidisp & 128) &&
          (raster.inidisp & 15) && raster.mosaic > 15 &&
          (raster.mosaic & (raster.screenEnabled[0] | raster.screenEnabled[1]) & 7)) {
        captured_mosaic = SmRendererSaveCapture(mosaic_path);
        break;
      }
    }
  }
  return true;
}

/* $7F:0002 block map and $7E:A000 block definitions, matching the
 * decomp's row/column streamer. Invalid coordinates are transparent, never
 * wrapped to unrelated room data. No scroll permissions/AI are modified. */
bool SmRendererRoomTile(const uint8_t ram[0x20000], int x, int y,
                        uint16_t *entry) {
  return SmRendererRoomLayerTile(ram, 0, x, y, entry);
}
bool SmRendererRoomLayerTile(const uint8_t ram[0x20000], unsigned layer,
                            int x, int y, uint16_t *entry) {
  if (!ram || !entry || layer > 1 || x < 0 || y < 0) return false;
  unsigned w = read16(ram, 0x7a5), h = read16(ram, 0x7a7);
  unsigned bx = (unsigned)x / 16, by = (unsigned)y / 16;
  /* LoadLevelDataAndOtherThings reserves 0x6400 bytes per layer. */
  if (!w || !h || (uint64_t)w * h > 0x3200 || bx >= w || by >= h) return false;
  unsigned address = (layer ? 0x19602 : 0x10002) + 2 * (by * w + bx);
  if (address >= 0x20000 - 1) return false;
  unsigned block = read16(ram, address);
  unsigned qx = ((unsigned)x / 8 & 1) ^ ((block >> 10) & 1);
  unsigned qy = ((unsigned)y / 8 & 1) ^ ((block >> 11) & 1);
  unsigned table = 0xa000 + (block & 1023) * 8 + (qy * 2 + qx) * 2;
  *entry = (uint16_t)(read16(ram, table) ^ ((block & 0xc00) << 4));
  return true;
}
static unsigned tile_pixel(const uint16_t *vram, unsigned address, int x, int y, int bpp) {
  unsigned a = (address + y) & 0x7fff;
  unsigned shift = 7 - x;
  unsigned bits = vram[a] >> shift;
  unsigned pixel = (bits & 1) | ((bits >> 7) & 2);
  if (bpp == 4) {
    bits = vram[(a + 8) & 0x7fff] >> shift;
    pixel |= ((bits & 1) << 2) | ((bits >> 5) & 8);
  }
  return pixel;
}

/* Host-only reconstruction of $91:CB8E's revealed block map. The high bit
 * marks replacement blocks, whose quadrant swaps do not flip tile pixels. */
static uint16_t xray_blocks[0x3200];
static const uint8_t *xray_rule(const uint8_t *ram, unsigned position) {
  unsigned type = read16(ram, 0x10002 + 2 * position) & 0xf000;
  unsigned bts = ram[0x16402 + position];
  for (unsigned i = 0; i < 17; ++i) {
    const uint8_t *entry = rom_bytes(0x91, 0xd2d6 + 4 * i, 4);
    if (!entry || read16(entry, 0) == 0xffff) return NULL;
    if (read16(entry, 0) != type) continue;
    unsigned list = read16(entry, 2);
    for (unsigned j = 0; j < 257; ++j) {
      const uint8_t *match = rom_bytes(0x91, list + 4 * j, 4);
      if (!match || read16(match, 0) == 0xffff) return NULL;
      if (read16(match, 0) == 0xff00 || read16(match, 0) == bts)
        return rom_bytes(0x91, read16(match, 2), 10);
    }
    return NULL;
  }
  return NULL;
}
static uint16_t xray_entry(const uint8_t *ram, unsigned block, unsigned quadrant) {
  unsigned q = quadrant ^ ((block >> 10) & 3);
  unsigned value = read16(ram, 0xa000 + (block & 1023) * 8 + q * 2);
  return (uint16_t)(value ^ ((block & 0x1000) ? 0 : (block & 0xc00) << 4));
}
bool SmRendererXrayBlocks(const uint8_t ram[0x20000], uint16_t blocks[0x3200]) {
  if (!ram || !blocks) return false;
  unsigned w = read16(ram, 0x7a5), h = read16(ram, 0x7a7);
  if (!w || !h || (uint64_t)w * h > 0x3200) return false;
  unsigned count = w * h;
  for (unsigned i = 0; i < count; ++i) blocks[i] = (uint16_t)(read16(ram, 0x10002 + 2 * i) & 0xfff);
  for (unsigned i = 0; i < count; ++i) {
    const uint8_t *rule = xray_rule(ram, i);
    if (!rule) continue;
    unsigned function = read16(rule, 0);
    if (function == 0xce79 || function == 0xcebb) {
      int x = (int)(i % w), y = (int)(i / w);
      bool vertical = function == 0xce79;
      int step = (int8_t)ram[0x16402 + i];
      unsigned target = i;
      bool resolved = !step;
      for (unsigned n = 0; step && n < count; ++n) {
        if (vertical) y += step; else x += step;
        if (x < 0 || y < 0 || x >= (int)w || y >= (int)h) {
          blocks[i] = 0x10ff; break;
        }
        target = (unsigned)y * w + (unsigned)x;
        step = (int8_t)ram[0x16402 + target];
        unsigned type = read16(ram, 0x10002 + 2 * target) & 0xf000;
        if (type == 0xd000) vertical = true;
        else if (type == 0x5000) { if (step < 0) vertical = false; }
        else { resolved = true; break; }
      }
      if (resolved && (read16(ram, 0x10002 + 2 * target) & 0xf000) == 0x3000) {
        const uint8_t *target_rule = xray_rule(ram, target);
        if (target_rule) blocks[i] = (uint16_t)(0x1000 | (read16(target_rule, 2) & 1023));
      }
      continue;
    }
    if (function == 0xcf3e && read16(ram, 0x79f) != 1) continue;
    if (function != 0xcf36 && function != 0xcf3e && function != 0xcf4e && function != 0xcf62 && function != 0xcf6f) return false;
    blocks[i] = (uint16_t)(0x1000 | (read16(rule, 2) & 1023));
    if ((function == 0xcf4e || function == 0xcf6f) && i % w + 1 < w)
      blocks[i + 1] = (uint16_t)(0x1000 | (read16(rule, 4) & 1023));
    if ((function == 0xcf62 || function == 0xcf6f) && i + w < count)
      blocks[i + w] = (uint16_t)(0x1000 | (read16(rule, function == 0xcf62 ? 2 : 6) & 1023));
    if (function == 0xcf6f && i % w + 1 < w && i + w + 1 < count)
      blocks[i + w + 1] = (uint16_t)(0x1000 | (read16(rule, 8) & 1023));
  }
  /* Uncollected item PLMs override generic block reveals, in guest order. */
  for (int slot = 39; slot >= 0; --slot) {
    if (read16(ram, 0x1c37 + 2 * slot) < 0xdf89) continue;
    unsigned item = read16(ram, 0x1dc7 + 2 * slot);
    if (item >= 512 || (ram[0xd870 + item / 8] & (1u << (item & 7)))) continue;
    unsigned variable = read16(ram, 0xdf0c + 2 * slot);
    const uint8_t *pointer = rom_bytes(0x84, 0x839d + (variable & ~1u), 2);
    const uint8_t *drawing = pointer ? rom_bytes(0x84, read16(pointer, 0), 4) : NULL;
    unsigned block = read16(ram, 0x1c87 + 2 * slot) / 2;
    if (drawing && block < count) blocks[block] = (uint16_t)(0x1000 | (read16(drawing, 2) & 0xbff));
  }
  const uint8_t *state = rom_bytes(0x8f, read16(ram, 0x7bb), 26);
  unsigned specials = state ? read16(state, 16) : 0;
  for (unsigned i = 0; specials && i < count; ++i) {
    const uint8_t *entry = rom_bytes(0x8f, specials + 4 * i, 4);
    if (!entry) return false;
    if (!entry[0] && !entry[1]) break;
    if (entry[0] < w && entry[1] < h)
      blocks[entry[1] * w + entry[0]] = (uint16_t)(0x1000 | (read16(entry, 2) & 0xbff));
  }
  return true;
}

typedef struct SmBg2Object { bool valid; int x, y; bool crocomire, parked; } SmBg2Object;
/* Extended spritemaps explicitly tag BG2 tilemap children with $FFFE
 * ($A0:944A/$96CA). Their tilemap is one moving object, not repeating room
 * scenery. Derive ownership from that ROM format rather than boss IDs. */
static SmBg2Object bg2_object(const SmSourceFrame *f, const SmSourceFrame *old, double alpha) {
  SmBg2Object result = {0};
  if (!f->object_state_valid) return result;
  for (unsigned slot = 0; slot < 32; ++slot) {
    const uint8_t *e = f->object_ram + 0xf78 + slot * 64;
    if (!read16(e, 0) || (read16(e, 14) & 0x300) || !(read16(e, 16) & 4)) continue;
    unsigned bank = e[46], map = read16(e, 22);
    const uint8_t *data = rom_bytes(bank, map, 2);
    if (!data) continue;
    unsigned n = read16(data, 0);
    if (!n || n > 64 || !(data = rom_bytes(bank, map + 2, n * 8))) continue;
    bool tilemap = false;
    for (unsigned i = 0; i < n; ++i) {
      const uint8_t *child = rom_bytes(bank, read16(data + i * 8, 4), 2);
      tilemap |= child && read16(child, 0) == 0xfffe;
    }
    if (!tilemap) continue;
    int x = (int)read16(e, 2) - (int)read16(f->object_ram, 0x911);
    int y = (int)read16(e, 6) - (int)read16(f->object_ram, 0x915);
    int dx, dy;
    if (alpha < 1 && enemy_motion(f, old, slot, &dx, &dy)) {
      x = lerp_pixel(x + dx, x, alpha); y = lerp_pixel(y + dy, y, alpha);
    }
    if (result.valid && (result.x != x || result.y != y)) return (SmBg2Object){0};
    const uint8_t *definition = rom_bytes(0xa0, read16(e, 0), 26);
    bool crocomire = bank == 0xa4 && definition && definition[12] == 0xa4 &&
        read16(definition, 18) == 0x8a5a && read16(definition, 24) == 0x8c04;
    result = (SmBg2Object){true, x, y, crocomire, false};
  }
  return result;
}

typedef struct SmBackgroundLine {
  int shift, size, scroll_x, scroll_y, world_x, world_y, bpp;
  int room_width, room_height, camera_x, camera_y;
  unsigned layer, sc, base, low, high;
  const uint8_t *room;
  const uint16_t *revealed;
  bool screen_space;
  bool object_canvas;
  int canvas_x, canvas_y, canvas_width, canvas_height, canvas_scroll_x;
} SmBackgroundLine;
/* Bit 11 is unused by the priority/layer/palette pixel encoding. A cleared
 * HUD plane is opaque black, unlike a transparent subscreen backdrop which
 * asks color math to use COLDATA. Keep that distinction without inventing a
 * palette index or exposing repeated HUD texels in the wider view. */
enum { SM_PIXEL_OPAQUE_BLACK = 0x0800 };

/* Scroll registers are ten bits, but a 32-tile axis wraps sooner. HDMA can
 * leave an equivalent register alias (e.g. BG2 VOFS256 vs768). Recovering a
 * world offset modulo1024 turns that alias into a false half-room jump. */
static int background_scroll_delta(int difference, unsigned sc, unsigned size, bool vertical) {
  int period = (int)size * (sc & (vertical ? 2u : 1u) ? 64 : 32);
  return ((difference + period / 2) & (period - 1)) - period / 2;
}

static SmBackgroundLine background_line(const SnesRenderRegisters *p, const uint8_t *room, unsigned layer,
                                        SmBg2Object object) {
  static const unsigned low[] = {8, 7, 1}, high[] = {12, 11, 3};
  SmBackgroundLine b = {0};
  b.layer = layer;
  b.shift = PPU_bigTiles(p, layer) ? 4 : 3;
  b.size = 1 << b.shift;
  b.scroll_x = p->hScroll[layer]; b.scroll_y = p->vScroll[layer];
  b.sc = p->bgXsc[layer];
  /* $58/$5A include the HUD/message map at VRAM $5800..$5BFF. The taller
   * $5A map also includes liquid FX at $5C00..$5FFF: classify the addressed
   * page below, not the entire BG3 map as screen-space. */
  b.screen_space = layer == 2 && (b.sc & 0xfc) == 0x58;
  if (layer == 1 && object.valid && (room[0x91b] & 1)) {
    b.object_canvas = true;
    b.canvas_width = b.size * (b.sc & 1 ? 64 : 32);
    b.canvas_height = b.size * (b.sc & 2 ? 64 : 32);
    b.canvas_scroll_x = b.scroll_x;
    /* $A4:8BA4 parks Crocomire's BG2 at HOFS256 when outside the native
     * viewport. Sampling that parking position past X255 wraps his face
     * back onto the edge. Recover its unclamped owner-relative placement
     * for margins only; native pixels retain the original raster register. */
    if (object.crocomire && (object.parked || b.scroll_x == 256))
      b.canvas_scroll_x = 51 - object.x;
    b.canvas_x = object.x - ((object.x + b.canvas_scroll_x) & (b.canvas_width - 1));
    b.canvas_y = object.y - ((object.y + b.scroll_y) & (b.canvas_height - 1));
  }
  b.bpp = layer == 2 ? 2 : 4;
  b.base = ((p->bgTileAdr >> (layer * 4)) & 15) * 4096;
  b.low = low[layer];
  b.high = layer == 2 && (p->bgmode & 8) ? 15 : high[layer];
  unsigned width = read16(room, 0x7a5), height = read16(room, 0x7a7);
  if (layer < 2 && width && height && (uint64_t)width * height <= 0x3200) {
    b.room_width = (int)width * 16; b.room_height = (int)height * 16;
    b.camera_x = (int16_t)read16(room, 0x911); b.camera_y = (int16_t)read16(room, 0x915);
    b.world_x = b.camera_x; b.world_y = b.camera_y;
  }
  /* $82:E36E installs the destination level before $82:E38E initializes
   * its camera and ring-buffer offsets. That one frame still scans out the
   * departing door. Combining the new level with the old camera invents a
   * second door in the opposite margin; retain hardware tilemap addressing
   * until the destination scroll setup has run. */
  bool layout_ready = read16(room, 0x998) != 11 || read16(room, 0x99c) != 0xe38e;
  if (layout_ready && b.size == 8 && (layer == 0 || (layer == 1 && !(room[0x91b] & 1)))) {
    /* Native scroll includes ring-buffer offsets. Recover the raster delta
     * relative to the live camera, then sample decompressed world data. */
    int camera_x = (int16_t)read16(room, layer ? 0x917 : 0x911);
    int camera_y = (int16_t)read16(room, layer ? 0x919 : 0x915);
    int dx = background_scroll_delta((int)p->hScroll[layer] - camera_x -
              (int)read16(room, layer ? 0x921 : 0x91d), b.sc, b.size, false);
    int dy = background_scroll_delta((int)p->vScroll[layer] - camera_y -
              (int)read16(room, layer ? 0x923 : 0x91f), b.sc, b.size, true);
    b.world_x = camera_x + dx;
    b.world_y = camera_y + dy;
    b.room = room;
  }
  return b;
}

static uint16_t background_pixel(const SmBackgroundLine *b, const uint16_t *vram,
                                 int x, int y, bool margin) {
  if (margin && b->object_canvas &&
      (x < b->canvas_x || x >= b->canvas_x + b->canvas_width ||
       y < b->canvas_y || y >= b->canvas_y + b->canvas_height)) return 0;
  int size = b->size;
  int px, py;
  unsigned tile;
  if (margin && b->room) {
    int wx = b->world_x + x, wy = b->world_y + y;
    uint16_t entry;
    if (b->revealed) {
      unsigned w = read16(b->room, 0x7a5), h = read16(b->room, 0x7a7);
      if (wx < 0 || wy < 0 || (unsigned)wx / 16 >= w || (unsigned)wy / 16 >= h) return 0;
      entry = xray_entry(b->room, b->revealed[(unsigned)wy / 16 * w + (unsigned)wx / 16],
                         ((unsigned)wx / 8 & 1) + ((unsigned)wy / 8 & 1) * 2);
    } else if (!SmRendererRoomLayerTile(b->room, b->layer, wx, wy, &entry)) return 0;
    tile = entry;
    px = wx & 1023;
    py = wy & 1023;
  } else {
    int scroll_x = margin && b->object_canvas ? b->canvas_scroll_x : b->scroll_x;
    px = (x + scroll_x) & 1023; py = (y + b->scroll_y) & 1023;
    int tx = px >> b->shift, ty = py >> b->shift;
    unsigned sc = b->sc;
    unsigned address = (sc & 0xfc) * 256 + (tx & 31) + (ty & 31) * 32;
    if ((sc & 1) && (tx & 32)) address += 1024;
    if ((sc & 2) && (ty & 32)) address += (sc & 1) ? 2048 : 1024;
    /* Message HDMA and boss scrolling can expose any UI row. Preserve its
     * opaque-black subscreen without suppressing the adjacent liquid page
     * selected by the same $5A register on lower gameplay scanlines. */
    if (margin && b->screen_space && (address & 0x7c00) == 0x5800)
      return b->sc & 2 ? (uint16_t)((b->low << 12) | (2 << 8) | SM_PIXEL_OPAQUE_BLACK) : 0;
    tile = vram[address & 0x7fff];
  }
  int cx = px & (size - 1), cy = py & (size - 1);
  if (tile & 0x4000) cx = size - 1 - cx;
  if (tile & 0x8000) cy = size - 1 - cy;
  unsigned number = ((tile & 1023) + cx / 8 + (cy / 8) * 16) & 1023;
  unsigned pixel = tile_pixel(vram, b->base + number * (b->bpp * 4), cx & 7, cy & 7, b->bpp);
  if (!pixel) return 0;
  unsigned priority = tile & 0x2000 ? b->high : b->low;
  unsigned palette = ((tile >> 10) & 7) * (1u << b->bpp);
  return (priority << 12) | (b->layer << 8) | palette | pixel;
}

typedef struct SmWideWindow { bool valid; int left, right; bool first; } SmWideWindow;

static int q8_floor(int value) {
  return value >= 0 ? value / 256 : -((-value + 255) / 256);
}

bool SmRendererXrayBounds(const uint16_t tangent[129], int x, int y,
                          unsigned angle, unsigned half_width,
                          int32_t left[230], int32_t right[230]) {
  if (!tangent || !left || !right || x < -256 || x > 511 || y < 1 || y > 230 ||
      angle > 256 || half_width > 10) return false;
  int a = (int)angle - (int)half_width, b = (int)angle + (int)half_width;
  if (a < 0) a += 256;
  if (b > 256) b -= 256;
  int sa = tangent[a < 128 ? a : a - 128];
  int sb = tangent[b < 128 ? b : b - 128];
  int kind = a < 128 ? (a >= 64 ? 2 : b >= 64 ? 0 : 1) :
                      (a >= 192 ? 1 : b >= 192 ? 3 : 2);
  bool horizontal = !half_width && (angle == 64 || angle == 192);
  bool offscreen = x < 0 || x >= 256;
  for (int row = 0; row < 230; ++row) {
    int delta = row - (y - 1), distance = abs(delta);
    int l = 1, r = 0;
    if (offscreen) {
      /* $91:BE11 selects distinct offscreen routines. There is no apex
       * row: both ramps advance before their first write. Retain the
       * original 16-bit source and each routine's signed bank correction. */
      int base = (x * 256) & 0xffff;
      distance = delta <= 0 ? 1 - delta : delta;
      if (horizontal) {
        if (!delta) { l = -32768; r = 32768; }
      } else if (kind == 0) {
        l = q8_floor(base - 65536 + distance * (delta <= 0 ? sa : sb)); r = 32768;
      } else if (kind == 3) {
        l = -32768; r = q8_floor(base + 65536 - distance * (delta <= 0 ? sb : sa));
      } else if ((kind == 1 && delta <= 0) || (kind == 2 && delta > 0)) {
        int ls, rs, lb, rb;
        if (kind == 1) {
          ls = a < 192 ? sa : -sa; lb = a < 192 ? -65536 : 65536;
          rs = b < 192 ? sb : -sb; rb = b < 192 ? -65536 : 65536;
        } else {
          ls = b < 128 ? sb : -sb; lb = b < 128 ? -65536 : 65536;
          rs = a < 128 ? sa : -sa; rb = a < 128 ? -65536 : 65536;
        }
        l = q8_floor(base + lb + distance * ls);
        r = q8_floor(base + rb + distance * rs);
      }
      left[row] = l; right[row] = r;
      continue;
    }
    if (horizontal) {
      if (!delta) { l = angle == 64 ? x : -32768; r = angle == 64 ? 32768 : x; }
    } else if (kind == 0) {
      l = q8_floor(x * 256 + distance * (delta < 0 ? sa : sb)); r = 32768;
    } else if (kind == 3) {
      l = -32768; r = q8_floor(x * 256 - distance * (delta < 0 ? sb : sa));
    } else if ((kind == 1 && delta <= 0) || (kind == 2 && delta >= 0)) {
      int ls, rs;
      if (kind == 1) {
        ls = a < 192 ? sa : -sa;
        rs = b < 192 ? sb : -sb;
      } else {
        ls = b < 128 ? sb : -sb;
        rs = a < 128 ? sa : -sa;
      }
      l = q8_floor(x * 256 + distance * ls);
      r = q8_floor(x * 256 + distance * rs);
    }
    left[row] = l; right[row] = r;
  }
  return true;
}

static bool xray_windows(const SmSourceFrame *f, SmWideWindow rows[224]) {
  if (!read16(f->ram, 0xa78) || read16(f->ram, 0xa7a) > 2 ||
      read16(f->ram, 0x9d2) != 5) return false;
  const uint8_t *table = rom_bytes(0x91, 0xc9d4, 258);
  if (!table) return false;
  uint16_t tangent[129];
  for (unsigned i = 0; i < 129; ++i) tangent[i] = (uint16_t)read16(table, 2 * i);
  int x = (int)read16(f->ram, 0xaf6) - (int)read16(f->ram, 0x911) +
          (f->ram[0xa1e] == 4 ? -3 : 3);
  int y = (int)read16(f->ram, 0xafa) - (int)read16(f->ram, 0x915) -
          (f->ram[0xa1f] == 5 ? 12 : 16);
  int32_t left[230], right[230];
  if (!SmRendererXrayBounds(tangent, x, y, read16(f->ram, 0xa82),
                            read16(f->ram, 0xa84), left, right)) return false;
  /* Match the generated native table before trusting its extrapolation.
   * Do not infer an angle from clipped/empty rows. */
  for (unsigned row = 0; row < 230; ++row) {
    int l = left[row], r = right[row];
    unsigned native = read16(f->ram, 0x9800 + row * 2);
    bool empty = l > r || l > 255 || r < 0;
    if (empty ? (native & 255) <= (native >> 8) :
        (native & 255) != (unsigned)(l < 0 ? 0 : l) ||
        (native >> 8) != (unsigned)(r > 255 ? 255 : r)) return false;
  }
  for (unsigned row = 0; row < 224; ++row) {
    SnesRenderRegisters raster;
    memcpy(&raster, f->lines[row].registers, PPU_SAVESTATE_REGS_SIZE);
    unsigned native = read16(f->ram, 0x9800 + (row + 1) * 2);
    if (raster.window2left != (native & 255) || raster.window2right != (native >> 8)) continue;
    rows[row] = (SmWideWindow){true, left[row + 1], right[row + 1]};
  }
  return true;
}

static bool eye_beam_windows(const SmSourceFrame *f, SmWideWindow rows[224]) {
  /* $88:E987 builds the Morph Ball Eye cone with the same tangent walker
   * as X-ray, but uses enemy slot 1 and the eye HDMA object's half-width.
   * A clipped 0/255 edge is not a horizontal continuation of that cone. */
  if (!f->object_state_valid) return false;
  const uint8_t *ram = f->ram;
  /* HdmaObjectHandler precedes camera/enemy updates. The owner latch has
   * its input angle and position; current RAM has the updated HDMA width
   * and generated table. Using the new enemy angle would flicker in motion. */
  const uint8_t *owner = f->object_ram;
  const uint8_t *enemy = owner + 0xfb8;
  const uint8_t *definition = rom_bytes(0xa0, read16(enemy, 0), 26);
  const uint8_t *table = rom_bytes(0x91, 0xc9d4, 258);
  if (!definition || definition[12] != 0xa8 || read16(definition, 18) != 0x9058 ||
      (read16(enemy, 14) & 0x300) || read16(ram, 0xfb8) != read16(enemy, 0) ||
      !table) return false;
  unsigned half_width = 5;
  for (unsigned slot = 0; slot < 6; ++slot) {
    unsigned phase = read16(ram, 0x18f0 + slot * 2);
    if ((read16(ram, 0x18c0 + slot * 2) & 0xff00) == 0x8800 &&
        (phase == 0xe9e6 || phase == 0xea3c || phase == 0xeacb)) {
      half_width = read16(ram, 0x192c + slot * 2);
      break;
    }
  }
  if (half_width > 4) return false;
  uint16_t tangent[129];
  for (unsigned i = 0; i < 129; ++i) tangent[i] = (uint16_t)read16(table, i * 2);
  int x = (int)read16(enemy, 2) - (int)read16(owner, 0x911);
  int y = (int)read16(enemy, 6) - (int)read16(owner, 0x915);
  int32_t left[230], right[230];
  if (!SmRendererXrayBounds(tangent, x, y, read16(enemy, 54), half_width,
                            left, right)) return false;
  /* Require the guest's native table, including the last clamped edge pixel
   * before its ramp becomes empty. Geometry only supplies the extra columns;
   * every original pixel continues to use the captured hardware windows. */
  for (unsigned row = 0; row < 230; ++row) {
    unsigned native = read16(ram, 0x9100 + row * 2);
    int l = left[row], r = right[row];
    bool empty = l > r || l > 255 || r < 0;
    if (!empty) {
      if (native != (unsigned)(l < 0 ? 0 : l) + ((unsigned)(r > 255 ? 255 : r) << 8))
        return false;
    } else if ((native & 255) <= (native >> 8)) {
      int toward = (int)row + ((int)row < y - 1 ? 1 : -1);
      if (toward < 0 || toward >= 230 ||
          !((native == 0xffff && l > 255 && left[toward] <= 255) ||
            (native == 0 && r < 0 && right[toward] >= 0))) return false;
    }
  }
  bool extended = false;
  for (unsigned row = 0; row < 224; ++row) {
    SnesRenderRegisters raster;
    memcpy(&raster, f->lines[row].registers, PPU_SAVESTATE_REGS_SIZE);
    unsigned native = read16(ram, 0x9100 + (row + 1) * 2);
    if (raster.window1left != (native & 255) || raster.window1right != (native >> 8)) continue;
    rows[row] = (SmWideWindow){true, left[row + 1], right[row + 1], true};
    extended = true;
  }
  return extended;
}

/* NMI latches the indirect pointers before the game builds this frame's
 * row bytes. Use that same ordering, and verify both the generated table
 * and each raster register before extrapolating beyond native clipping. */
static void power_bomb_windows(const SmSourceFrame *f, SmWideWindow rows[224]) {
  memset(rows, 0, 224 * sizeof(*rows));
  if (!f->object_state_valid || !(read16(f->ram, 0x592) & 0x8000)) return;
  const uint8_t *old = f->object_ram;
  int center = (int)read16(f->ram, 0xce6) - 256;
  if (center < -256 || center > 511) return;
  for (unsigned slot = 0; slot < 5; ++slot) {
    unsigned phase = read16(f->ram, 0x18f0 + slot * 2);
    int16_t widths[192];
    if (phase == 0x90df || phase == 0x8de9) {
      const uint8_t *profile = rom_bytes(0x88, 0xa206, 160);
      if (!SmRendererPowerBombWidths(profile,
          read16(old, phase == 0x90df ? 0xcec : 0xcea) >> 8, widths)) continue;
    } else if (phase == 0x91a8 || phase == 0x8eb2 || phase == 0x8b98) {
      unsigned address = read16(old, 0xcf2);
      if (phase == 0x8b98) address -= 192;
      if (address < 0x9246 || address > 0xa146 || (address - 0x9246) % 192) continue;
      const uint8_t *profile = rom_bytes(0x88, address, 192);
      if (!profile) continue;
      bool ended = false;
      for (unsigned i = 0; i < 192; ++i) {
        ended |= profile[i] == 0;
        widths[i] = ended ? -1 : profile[i];
      }
    } else continue;
    bool valid = true;
    for (unsigned i = 0; i < 192; ++i) {
      int left = center - widths[i], right = center + widths[i];
      bool empty = widths[i] < 0 || right < 0 || left > 255;
      unsigned actual_left = f->ram[0xc406 + i], actual_right = f->ram[0xc506 + i];
      if (empty ? actual_left <= actual_right :
          actual_left != (unsigned)(left < 0 ? 0 : left) ||
          actual_right != (unsigned)(right > 255 ? 255 : right)) valid = false;
    }
    if (!valid) continue;
    unsigned pointer = read16(old, 0x18d8 + slot * 2);
    if (pointer < 0x9800 || pointer > 0xa100 || (pointer - 0x9800) % 3 ||
        read16(old, 0x18da + slot * 2) != pointer + 0x901) continue;
    for (unsigned y = 0; y < 224; ++y) {
      unsigned address = pointer + 3 * (y + 1);
      if (address > 0xa0fd) continue;
      const uint8_t *left = rom_bytes(0x89, address, 3);
      const uint8_t *right = rom_bytes(0x89, address + 0x901, 3);
      if (!left || !right || left[0] != 0x81 || right[0] != 0x81) continue;
      unsigned target = read16(left, 1), target_right = read16(right, 1);
      int width;
      if (target == 0xc606 && target_right == 0xc607) width = -1;
      else if (target >= 0xc406 && target < 0xc4c6 && target_right == target + 256)
        width = widths[target - 0xc406];
      else continue;
      SnesRenderRegisters raster;
      memcpy(&raster, f->lines[y].registers, PPU_SAVESTATE_REGS_SIZE);
      if (raster.window2left != f->ram[target] || raster.window2right != f->ram[target_right]) continue;
      rows[y] = (SmWideWindow){true, width < 0 ? 1 : center - width,
                                    width < 0 ? 0 : center + width};
    }
    return;
  }
}

static bool in_window(const SnesRenderRegisters *p, int layer, int x, int extra, const SmWideWindow *wide) {
  unsigned flags = (p->windowsel >> (layer * 4)) & 15;
  bool enabled1 = (flags & 2) != 0, enabled2 = (flags & 8) != 0;
  int l1 = p->window1left == 0 ? -extra : p->window1left;
  int r1 = p->window1right == 255 ? 255 + extra : p->window1right;
  int l2 = p->window2left == 0 ? -extra : p->window2left;
  int r2 = p->window2right == 255 ? 255 + extra : p->window2right;
  if (wide->valid && (x < 0 || x >= 256)) {
    if (wide->first) { l1 = wide->left; r1 = wide->right; }
    else { l2 = wide->left; r2 = wide->right; }
  }
  bool a = (x >= l1 && x <= r1) != ((flags & 1) != 0);
  bool b = (x >= l2 && x <= r2) != ((flags & 4) != 0);
  if (!enabled1) return enabled2 && b;
  if (!enabled2) return a;
  switch ((p->wbgobjlog >> (layer * 2)) & 3) {
  case 0: return a || b;
  case 1: return a && b;
  case 2: return a != b;
  default: return a == b;
  }
}

static bool player_projectile_visible(const uint8_t *ram, unsigned slot) {
  unsigned offset = slot * 2, type = read16(ram, 0xc18 + offset);
  if (!read16(ram, 0xc40 + offset)) return false;
  unsigned category = type & 0xf00;
  if (category >= 0x300)
    return category != 0x300 || read16(ram, 0xc7c + offset) != 0;
  if (slot >= 5) return false;
  unsigned nmi = read16(ram, 0x5b6);
  if (type & 0xf10) return true;
  if (type & 12) return ((slot & 1) != 0) == ((nmi & 2) != 0);
  return ((slot & 1) != 0) != ((nmi & 1) != 0);
}
typedef struct SmProjectilePose {
  unsigned bank, map, palette, base, identity;
  int x, y, world_x, world_y;
} SmProjectilePose;
static void project_player_position(const uint8_t *ram, unsigned type,
                                    int world_x, int world_y, int *x, int *y) {
  unsigned category = type & 0xf00;
  if ((read16(ram, 0x93f) & 0x8000) && category != 0x300 && category != 0x500)
    SmMode7ObjectPosition((uint16_t)read16(ram, 0x78), (uint16_t)read16(ram, 0x7a),
        (uint16_t)read16(ram, 0x7c), (uint16_t)read16(ram, 0x80),
        (uint16_t)read16(ram, 0x82), (uint16_t)world_x, (uint16_t)world_y,
        (uint16_t)read16(ram, 0x911), (uint16_t)read16(ram, 0x915), x, y);
}
static bool projectile_pose(const SmSourceFrame *f, bool enemy, unsigned slot,
                            SmProjectilePose *pose) {
  const uint8_t *ram = f->object_ram;
  unsigned offset = slot * 2;
  if (enemy) {
    pose->identity = read16(ram, 0x1997 + offset);
    if (!pose->identity || read16(ram, 0x1840)) return false;
    unsigned gfx = read16(ram, 0x19bb + offset);
    pose->bank = 0x8d; pose->map = read16(ram, 0x1b6b + offset);
    pose->palette = gfx & 0xff00; pose->base = gfx & 255;
    pose->world_x = (int)read16(ram, 0x1a4b + offset);
    pose->world_y = (int)read16(ram, 0x1a93 + offset);
  } else {
    if (!player_projectile_visible(ram, slot)) return false;
    pose->identity = read16(ram, 0xc18 + offset);
    pose->bank = 0x93; pose->map = read16(ram, 0xcb8 + offset);
    pose->palette = pose->base = 0;
    pose->world_x = (int)read16(ram, 0xb64 + offset);
    pose->world_y = (int)read16(ram, 0xb78 + offset);
  }
  pose->x = pose->world_x - (int)read16(ram, 0x911);
  pose->y = pose->world_y - (int)read16(ram, 0x915);
  if (!enemy)
    project_player_position(ram, pose->identity, pose->world_x, pose->world_y, &pose->x, &pose->y);
  return pose->map >= 0x8000;
}
static bool projectile_motion(const SmSourceFrame *f, const SmSourceFrame *old,
                              bool enemy, unsigned slot,
                              SmProjectilePose *now, SmProjectilePose *before) {
  if (!old || !f->object_state_valid || !old->object_state_valid ||
      !projectile_pose(f, enemy, slot, now) || !projectile_pose(old, enemy, slot, before))
    return false;
  if (enemy && read16(f->object_ram, 0x1bd7 + slot * 2) !=
               read16(old->object_ram, 0x1bd7 + slot * 2)) return false;
  return now->identity == before->identity &&
      abs(now->world_x - before->world_x) <= 32 &&
      abs(now->world_y - before->world_y) <= 32 &&
      abs(now->x - before->x) <= 32 && abs(now->y - before->y) <= 32;
}
static bool sprite_object_pose(const SmSourceFrame *f, unsigned slot, SmProjectilePose *pose) {
  const uint8_t *ram = f->object_ram;
  unsigned offset = slot * 2;
  pose->identity = read16(ram, 0xef78 + offset);
  const uint8_t *data = rom_bytes(0xb4, pose->identity, 4);
  if (!data) return false;
  pose->bank = 0xb4; pose->map = read16(data, 2);
  unsigned gfx = read16(ram, 0xf078 + offset);
  pose->palette = gfx & 0xe00; pose->base = gfx & 0x1ff;
  pose->world_x = (int)read16(ram, 0xf0f8 + offset);
  pose->world_y = (int)read16(ram, 0xf1f8 + offset);
  pose->x = pose->world_x - (int)read16(ram, 0x911);
  pose->y = pose->world_y - (int)read16(ram, 0x915);
  return pose->map >= 0x8000 && pose->y >= 0 && pose->y < 272;
}
static bool sprite_object_motion(const SmSourceFrame *f, const SmSourceFrame *old,
                                 unsigned slot, SmProjectilePose *now, SmProjectilePose *before) {
  if (!old || !f->object_state_valid || !old->object_state_valid ||
      !sprite_object_pose(f, slot, now) || !sprite_object_pose(old, slot, before)) return false;
  /* There is no separate owner ID. Admit unchanged instructions and the
   * ordinary four-byte animation advance at timer expiry, not arbitrary
   * jumps/reused instruction lists. Never execute an instruction or timer. */
  bool continuous = now->identity == before->identity ||
      (now->identity == before->identity + 4 && read16(old->object_ram, 0xeff8 + slot * 2) == 1);
  return continuous && now->palette == before->palette && now->base == before->base &&
      abs(now->world_x - before->world_x) <= 32 && abs(now->world_y - before->world_y) <= 32 &&
      abs(now->x - before->x) <= 32 && abs(now->y - before->y) <= 32;
}
static int moving_effect_slot(const SmSourceFrame *f, const SmSourceFrame *old) {
  for (unsigned slot = 0; slot < 32; ++slot) {
    SmProjectilePose now, before;
    if (!sprite_object_motion(f, old, slot, &now, &before) ||
        (now.x == before.x && now.y == before.y) || now.y < 32 || now.y >= 216) continue;
    const SmRasterLine *line = &f->lines[now.y];
    SnesRenderRegisters p;
    memcpy(&p, line->registers, PPU_SAVESTATE_REGS_SIZE);
    if ((p.inidisp & 0x8f) != 15 || !(p.screenEnabled[0] & 16)) continue;
    for (unsigned obj = 0; obj < 128; ++obj) {
      unsigned high = (line->high_oam[obj / 4] >> ((obj & 3) * 2)) & 3;
      int actual_x;
      if (map_piece(now.bank, now.map, now.x, now.y, now.palette, now.base,
                    line->oam[obj * 2], line->oam[obj * 2 + 1], high, &actual_x))
        return (int)slot;
    }
  }
  return -1;
}
static bool grapple_flare_pose(const SmSourceFrame *f, SmProjectilePose *pose) {
  const uint8_t *ram = f->object_ram;
  unsigned frame = read16(ram, 0xcd6), function = read16(ram, 0xd32);
  if (!f->object_state_valid || read16(ram, 0xa5c) != 0xeb86 ||
      !read16(ram, 0xcd0) || function >= 0xc856 || frame > 63) return false;
  const uint8_t *base = rom_bytes(0x93, ram[0xa1e] == 4 ? 0xa22b : 0xa225, 2);
  if (!base) return false;
  unsigned index = read16(base, 0) + frame;
  if (index > 0x7fff) return false;
  const uint8_t *map = rom_bytes(0x93, 0xa1a1 + 2 * index, 2);
  if (!map) return false;
  *pose = (SmProjectilePose){0};
  pose->bank = 0x93; pose->map = read16(map, 0);
  pose->identity = function | ((unsigned)ram[0xa1e] << 16);
  pose->world_x = (int)read16(ram, 0xd1a); pose->world_y = (int)read16(ram, 0xd1c);
  pose->x = pose->world_x - (int)read16(ram, 0x911);
  pose->y = pose->world_y - (int)read16(ram, 0x915);
  return pose->map >= 0x8000 && pose->y >= 0 && pose->y < 256;
}
static bool grapple_flare_motion(const SmSourceFrame *f, const SmSourceFrame *old,
                                  SmProjectilePose *now, SmProjectilePose *before) {
  return old && grapple_flare_pose(f, now) && grapple_flare_pose(old, before) &&
      now->identity == before->identity &&
      abs(now->world_x - before->world_x) <= 32 && abs(now->world_y - before->world_y) <= 32 &&
      abs(now->x - before->x) <= 32 && abs(now->y - before->y) <= 32;
}
static bool projectile_piece_motion(const SmSourceFrame *f, const SmSourceFrame *old,
                                     unsigned position, unsigned attr, unsigned high,
                                     int *dx, int *dy, int *actual_x, bool *effect) {
  bool matched = false;
  *effect = false;
  *dx = *dy = 0;
  *actual_x = 0;
  for (unsigned kind = 0; kind < 4; ++kind) {
    for (unsigned slot = 0; slot < (kind == 3 ? 1u : kind == 2 ? 32u : kind ? 18u : 10u); ++slot) {
      SmProjectilePose now, before;
      bool moving = kind == 3 ? grapple_flare_motion(f, old, &now, &before)
                    : kind == 2 ? sprite_object_motion(f, old, slot, &now, &before)
                             : projectile_motion(f, old, kind != 0, slot, &now, &before);
      if (!moving) continue;
      int ax;
      if (!map_piece(now.bank, now.map, now.x, now.y, now.palette, now.base,
                      position, attr, high, &ax)) continue;
      int mx = before.x - now.x, my = before.y - now.y;
      if (matched && (*dx != mx || *dy != my || *actual_x != ax)) return false;
      *dx = mx; *dy = my; *actual_x = ax; matched = true;
      if (kind == 2) *effect = true;
    }
  }
  return matched && (*dx || *dy);
}
/* OAM normally stays constant across scanlines. Cache ownership only within
 * one presentation, keyed by the current complete entry, so raster OAM changes
 * still force verification and no match can leak across snapshots. */
static struct {
  unsigned position, attr, high;
  int dx, dy, actual_x;
  bool valid, matched;
  bool effect;
} object_matches[128];
static struct {
  unsigned position, attr, high;
  bool valid, matched;
} samus_matches[128];
static struct {
  unsigned position, attr, high;
  int dx, dy, actual_x;
  bool valid, matched;
} enemy_matches[128];
static bool current_enemy_motion(unsigned slot, const SmSourceFrame *f,
                                 const SmSourceFrame *old, unsigned position,
                                 unsigned attr, unsigned high, int *dx, int *dy, int *actual_x) {
  high &= 3;
  if (enemy_matches[slot].valid && enemy_matches[slot].position == position &&
      enemy_matches[slot].attr == attr && enemy_matches[slot].high == high) {
    *dx = enemy_matches[slot].dx; *dy = enemy_matches[slot].dy;
    *actual_x = enemy_matches[slot].actual_x;
    return enemy_matches[slot].matched;
  }
  bool matched = false;
  *dx = *dy = 0;
  *actual_x = 0;
  for (unsigned owner = 0; owner < 32; ++owner) {
    int mx, my, ax;
    if (!enemy_motion(f, old, owner, &mx, &my) ||
        !enemy_piece(f, owner, position, attr, high, &ax)) continue;
    /* Overlapping maps can describe the same OAM piece. Do not guess when
     * those owners imply different motion. */
    if (matched && (*dx != mx || *dy != my || *actual_x != ax)) { matched = false; break; }
    *dx = mx; *dy = my; *actual_x = ax; matched = true;
  }
  matched = matched && (*dx || *dy);
  enemy_matches[slot].position = position;
  enemy_matches[slot].attr = attr;
  enemy_matches[slot].high = high;
  enemy_matches[slot].dx = *dx; enemy_matches[slot].dy = *dy;
  enemy_matches[slot].actual_x = *actual_x;
  enemy_matches[slot].valid = true; enemy_matches[slot].matched = matched;
  return matched;
}
static bool current_samus_piece(unsigned slot, const SmSourceFrame *f,
                                unsigned position, unsigned attr, unsigned high) {
  high &= 3;
  if (samus_matches[slot].valid && samus_matches[slot].position == position &&
      samus_matches[slot].attr == attr && samus_matches[slot].high == high)
    return samus_matches[slot].matched;
  bool matched = samus_piece(f, position, attr, high);
  samus_matches[slot].position = position;
  samus_matches[slot].attr = attr;
  samus_matches[slot].high = high;
  samus_matches[slot].valid = true;
  samus_matches[slot].matched = matched;
  return matched;
}
static bool object_motion_match(unsigned slot, const SmSourceFrame *f,
                                 const SmSourceFrame *old, unsigned position,
                                 unsigned attr, unsigned high, int *dx, int *dy, int *actual_x) {
  high &= 3;
  if (object_matches[slot].valid && object_matches[slot].position == position &&
      object_matches[slot].attr == attr && object_matches[slot].high == high) {
    *dx = object_matches[slot].dx; *dy = object_matches[slot].dy;
    *actual_x = object_matches[slot].actual_x;
    return object_matches[slot].matched;
  }
  bool matched = projectile_piece_motion(f, old, position, attr, high, dx, dy, actual_x,
                                         &object_matches[slot].effect);
  object_matches[slot].position = position;
  object_matches[slot].attr = attr;
  object_matches[slot].high = high;
  object_matches[slot].dx = *dx; object_matches[slot].dy = *dy;
  object_matches[slot].actual_x = *actual_x;
  object_matches[slot].valid = true;
  object_matches[slot].matched = matched;
  return matched;
}
static bool has_edge_motion(const SmSourceFrame *f, const SmSourceFrame *old) {
  if (!f->object_state_valid || !old->object_state_valid) return false;
  memset(enemy_matches, 0, sizeof(enemy_matches));
  memset(object_matches, 0, sizeof(object_matches));
  const SmRasterLine *line = &f->lines[100];
  SnesRenderRegisters p;
  memcpy(&p, line->registers, PPU_SAVESTATE_REGS_SIZE);
  if (!(p.screenEnabled[0] & 16) || (p.inidisp & 128) || !(p.inidisp & 15)) return false;
  static const int sizes[8][2] = {{8,16},{8,32},{8,64},{16,32},
                                  {16,64},{32,64},{16,32},{16,32}};
  for (unsigned slot = 0; slot < 128; ++slot) {
    unsigned position = line->oam[slot * 2], attr = line->oam[slot * 2 + 1];
    unsigned high = line->high_oam[slot / 4] >> ((slot % 4) * 2);
    int size = sizes[p.obsel >> 5][(high >> 1) & 1];
    int x = (position & 255) | ((high & 1) << 8);
    if (x >= 256) x -= 512;
    if (x + size > 0 && x < 256) continue;
    int dx, dy, actual_x;
    if (!current_enemy_motion(slot, f, old, position, attr, high, &dx, &dy, &actual_x) &&
        !object_motion_match(slot, f, old, position, attr, high, &dx, &dy, &actual_x)) continue;
    int mx = lerp_pixel(actual_x + dx, actual_x, 0.5);
    int my = lerp_pixel((int)(position >> 8) + dy, (int)(position >> 8), 0.5);
    if (mx < 256 && mx + size > 0 && my >= 32 && my < 224) return true;
  }
  return false;
}
static void sprites(const SnesRenderRegisters *p, const SmRasterLine *line, int y,
                    SmViewport viewport, uint16_t *pixels,
                    const SmSourceFrame *f, const SmSourceFrame *previous, double alpha,
                    bool reuse_center) {
  static const int sizes[8][2] = {{8,16},{8,32},{8,64},{16,32},
                                  {16,64},{32,64},{16,32},{16,32}};
  memset(pixels, 0, (size_t)viewport.width * sizeof(*pixels));
  int samus_dx = 0, samus_dy = 0;
  bool interpolate_objects = previous && f->object_state_valid && previous->object_state_valid && alpha < 1;
  if (interpolate_objects && read16(f->object_ram, 0xa1c) == read16(previous->object_ram, 0xa1c)) {
    int dx = (int16_t)(uint16_t)(read16(previous->object_ram, 0xb04) - read16(f->object_ram, 0xb04));
    int dy = (int16_t)(uint16_t)(read16(previous->object_ram, 0xb06) - read16(f->object_ram, 0xb06));
    if (abs(dx) <= 32 && abs(dy) <= 32) { samus_dx = dx; samus_dy = dy; }
  }
  for (int slot = 127; slot >= 0; --slot) {
    unsigned position = line->oam[slot * 2], attr = line->oam[slot * 2 + 1];
    unsigned high = line->high_oam[slot / 4] >> ((slot % 4) * 2);
    int size = sizes[p->obsel >> 5][(high >> 1) & 1];
    int sprite_y = position >> 8;
    int x = (position & 255) | ((high & 1) << 8);
    if (x >= 256) x -= 512;
    /* Offscreen OAM is ambiguous until a live ROM-map owner supplies its
     * signed screen position. Only verified motion may recover such entries
     * into the native area; unrecognized hidden reservations stay hidden. */
    bool native_footprint = x + size > 0 && x < 256;
    bool recovered = false;
    int enemy_dx, enemy_dy, actual_x;
    if (interpolate_objects && grapple_motion[slot].valid &&
        grapple_motion[slot].position == position && grapple_motion[slot].attr == attr &&
        grapple_motion[slot].high == (high & 3)) {
      x = lerp_pixel(x + grapple_motion[slot].dx, x, alpha);
      sprite_y = lerp_pixel(sprite_y + grapple_motion[slot].dy, sprite_y, alpha);
      ++render_stats.matched_object_samples;
    } else if (native_footprint && interpolate_objects && (samus_dx || samus_dy) &&
        abs(y - sprite_y) <= size + 32 &&
        current_samus_piece((unsigned)slot, f, position, attr, high)) {
      /* Animate the current ROM piece at the interpolated owner origin.
       * Prior OAM reservations and animation tile identities may differ. */
      x = lerp_pixel(x + samus_dx, x, alpha);
      sprite_y = lerp_pixel(sprite_y + samus_dy, sprite_y, alpha);
      ++render_stats.matched_object_samples;
      ++render_stats.matched_samus_samples;
    } else if (interpolate_objects && abs(y - sprite_y) <= size + 32 &&
        current_enemy_motion((unsigned)slot, f, previous, position, attr, high, &enemy_dx, &enemy_dy, &actual_x)) {
      if (!native_footprint) x = actual_x;
      recovered = true;
      x = lerp_pixel(x + enemy_dx, x, alpha);
      sprite_y = lerp_pixel(sprite_y + enemy_dy, sprite_y, alpha);
      ++render_stats.matched_object_samples;
    } else if (interpolate_objects && abs(y - sprite_y) <= size + 32 &&
        object_motion_match((unsigned)slot, f, previous, position, attr, high, &enemy_dx, &enemy_dy, &actual_x)) {
      if (!native_footprint) x = actual_x;
      recovered = true;
      x = lerp_pixel(x + enemy_dx, x, alpha);
      sprite_y = lerp_pixel(sprite_y + enemy_dy, sprite_y, alpha);
      ++render_stats.matched_object_samples;
      if (object_matches[slot].effect) ++render_stats.matched_effect_samples;
    }
    if (!native_footprint && !recovered) continue;
    /* OBJ evaluation is one hardware line behind BG sampling. */
    int row = (y - sprite_y) & 255;
    if (row >= size) continue;
    x += viewport.extra;
    if (attr & 0x8000) row = size - 1 - row;
    unsigned base = (p->obsel & 7) * 8192;
    if (attr & 256) base += (((p->obsel >> 3) & 3) + 1) * 4096;
    unsigned palette = 128 + ((attr >> 9) & 7) * 16;
    unsigned priority = ((attr >> 12) & 3) * 4 + 2;
    unsigned layer = attr & 0x800 ? 4 : 6;
    for (int col = 0; col < size; ++col) {
      int dest = x + col;
      if (dest < 0 || dest >= viewport.width) continue;
      if (reuse_center && dest >= viewport.extra && dest < viewport.extra + 256) continue;
      int cx = attr & 0x4000 ? size - 1 - col : col;
      unsigned tile = (((((attr & 255) >> 4) + row / 8) & 15) << 4) |
                      (((attr & 15) + cx / 8) & 15);
      unsigned pixel = tile_pixel(line->vram, base + tile * 16, cx & 7, row & 7, 4);
      if (pixel) {
        pixels[dest] = (priority << 12) | (layer << 8) | palette | pixel;
        if (!native_footprint && recovered && dest >= viewport.extra && dest < viewport.extra + 256)
          ++render_stats.recovered_edge_pixels;
      }
    }
  }
}
static bool window_condition(unsigned mode, bool inside) {
  return mode == 3 || (mode == 1 && !inside) || (mode == 2 && inside);
}

/* $A0:944A and $81:8AB8, interpreted only as rendering data. Do not call the
 * guest routines: WriteEnemyOams decrements shake timers and extended tilemap
 * handlers write guest RAM. Invisible/deleted enemies remain invisible. */
static void enemy_map(const SmRasterLine *line, const SnesRenderRegisters *p,
                      unsigned bank, unsigned map, int origin_x, int origin_y,
                      unsigned palette, unsigned base_tile, int y,
                      SmViewport viewport, uint16_t *pixels) {
  const uint8_t *data = rom_bytes(bank, map, 2);
  if (!data) return;
  unsigned count = read16(data, 0);
  if (!count || count > 128) return;
  data = rom_bytes(bank, map + 2, count * 5);
  if (!data) return;
  static const int sizes[8][2] = {{8,16},{8,32},{8,64},{16,32},
                                  {16,64},{32,64},{16,32},{16,32}};
  for (unsigned i = count; i-- > 0;) {
    const uint8_t *entry = data + i * 5;
    unsigned offset = read16(entry, 0);
    int dx = (int)(offset & 0x1ff);
    if (dx & 0x100) dx -= 512;
    int x = origin_x + dx;
    int sprite_y = origin_y + (int8_t)entry[2];
    int size = sizes[p->obsel >> 5][offset >> 15];
    int row = y - sprite_y;
    if (row < 0 || row >= size) continue;
    unsigned attr = (uint16_t)(palette | (base_tile + read16(entry, 3)));
    if (attr & 0x8000) row = size - 1 - row;
    unsigned base = (p->obsel & 7) * 8192;
    if (attr & 256) base += (((p->obsel >> 3) & 3) + 1) * 4096;
    for (int col = 0; col < size; ++col) {
      int native_x = x + col, dest = native_x + viewport.extra;
      if ((native_x >= 0 && native_x < 256) || dest < 0 || dest >= viewport.width)
        continue;
      int cx = attr & 0x4000 ? size - 1 - col : col;
      unsigned tile = (((((attr & 255) >> 4) + row / 8) & 15) << 4) |
                      (((attr & 15) + cx / 8) & 15);
      unsigned pixel = tile_pixel(line->vram, base + tile * 16, cx & 7, row & 7, 4);
      if (pixel)
        pixels[dest] = ((((attr >> 12) & 3) * 4 + 2) << 12) |
                       ((attr & 0x800 ? 4 : 6) << 8) |
                       128 | (((attr >> 9) & 7) * 16) | pixel;
    }
  }
}

static void enemies(const SmSourceFrame *f, const SmRasterLine *line,
                    const SnesRenderRegisters *p, int y, SmViewport viewport, uint16_t *pixels,
                    unsigned phase, const SmSourceFrame *previous, double alpha) {
  if (!viewport.extra || !renderer_rom) return;
  for (int slot = 31; slot >= 0; --slot) {
    const uint8_t *e = f->object_ram + 0xf78 + slot * 64;
    unsigned id = read16(e, 0), properties = read16(e, 14);
    if (!id || id == 0xdaff || (properties & 0x300)) continue;
    if (read16(e, 34) != phase) continue;
    unsigned map = read16(e, 22), bank = e[46];
    int x = (int)read16(e, 2) - (int)read16(f->object_ram, 0x911) +
            (int16_t)read16(f->object_ram, 0x7010 + slot * 64);
    int sy = (int)read16(e, 6) - (int)read16(f->object_ram, 0x915) +
             (int16_t)read16(f->object_ram, 0x7012 + slot * 64);
    unsigned palette = read16(e, 30), frozen = read16(e, 38);
    if (read16(e, 36) && (read16(f->object_ram, 0xe44) & 2)) palette = 0;
    else if (frozen && (frozen >= 0x5a || (frozen & 2))) palette = 3072;
    if (read16(e, 42)) x += read16(e, 44) & 2 ? -1 : 1;
    int dx, dy;
    if (alpha < 1 && enemy_motion(f, previous, (unsigned)slot, &dx, &dy)) {
      x = lerp_pixel(x + dx, x, alpha);
      sy = lerp_pixel(sy + dy, sy, alpha);
    }
    unsigned base_tile = read16(e, 32);
    if (read16(e, 16) & 4) {
      const uint8_t *ext = rom_bytes(bank, map, 2);
      if (!ext) continue;
      unsigned n = read16(ext, 0);
      if (!n || n > 64) continue;
      ext = rom_bytes(bank, map + 2, n * 8);
      if (!ext) continue;
      for (unsigned i = n; i-- > 0;) {
        const uint8_t *child = ext + i * 8;
        enemy_map(line, p, bank, read16(child, 4),
                  x + (int16_t)read16(child, 0), sy + (int16_t)read16(child, 2),
                  palette, base_tile, y, viewport, pixels);
      }
    } else {
      enemy_map(line, p, bank, map, x, sy, palette, base_tile, y, viewport, pixels);
    }
  }
}

static void enemy_projectiles(const SmSourceFrame *f, const SmRasterLine *line,
                              const SnesRenderRegisters *p, int y, SmViewport viewport,
                              uint16_t *pixels, bool low_priority,
                              const SmSourceFrame *previous, double alpha) {
  int shake_x = 0, shake_y = 0;
  unsigned earthquake = read16(f->object_ram, 0x183e), timer = read16(f->object_ram, 0x1840);
  if (timer && !read16(f->object_ram, 0xa78) && earthquake < 36) {
    const uint8_t *offset = rom_bytes(0x86, 0x846b + 4 * earthquake, 4);
    if (offset) {
      int sign = timer & 2 ? -1 : 1;
      shake_x = sign * (int16_t)read16(offset, 0);
      shake_y = sign * (int16_t)read16(offset, 2);
    }
  }
  /* $86:8390 / $86:83B2 emit slots in descending order. This compositor
   * overwrites pixels, so reverse that order to retain native OBJ ownership. */
  for (unsigned slot = 0; slot < 18; ++slot) {
    unsigned offset = slot * 2;
    if (!read16(f->object_ram, 0x1997 + offset) ||
        ((read16(f->object_ram, 0x1bd7 + offset) & 0x1000) != 0) != low_priority)
      continue;
    int x = (int)read16(f->object_ram, 0x1a4b + offset) - (int)read16(f->object_ram, 0x911) + shake_x;
    int sy = (int)read16(f->object_ram, 0x1a93 + offset) - (int)read16(f->object_ram, 0x915) + shake_y;
    SmProjectilePose now, before;
    if (alpha < 1 && projectile_motion(f, previous, true, slot, &now, &before)) {
      x = lerp_pixel(before.x, now.x, alpha);
      sy = lerp_pixel(before.y, now.y, alpha);
    }
    if (sy < -128 || sy >= 384) continue;
    unsigned gfx = read16(f->object_ram, 0x19bb + offset);
    enemy_map(line, p, 0x8d, read16(f->object_ram, 0x1b6b + offset), x, sy,
              gfx & 0xff00, gfx & 255, y, viewport, pixels);
  }
}

static void player_projectiles(const SmSourceFrame *f, const SmRasterLine *line,
                               const SnesRenderRegisters *p, int y, SmViewport viewport,
                               uint16_t *pixels, bool explosions,
                               const SmSourceFrame *previous, double alpha) {
  for (unsigned slot = 0; slot < (explosions ? 10u : 5u); ++slot) {
    unsigned offset = slot * 2;
    if (!player_projectile_visible(f->object_ram, slot)) continue;
    unsigned type = read16(f->object_ram, 0xc18 + offset);
    unsigned category = type & 0xf00;
    if ((category >= 0x300) != explosions) continue;
    int x = (int)read16(f->object_ram, 0xb64 + offset) - (int)read16(f->object_ram, 0x911);
    int sy = (int)read16(f->object_ram, 0xb78 + offset) - (int)read16(f->object_ram, 0x915);
    project_player_position(f->object_ram, type,
        (int)read16(f->object_ram, 0xb64 + offset), (int)read16(f->object_ram, 0xb78 + offset), &x, &sy);
    SmProjectilePose now, before;
    if (alpha < 1 && projectile_motion(f, previous, false, slot, &now, &before)) {
      x = lerp_pixel(before.x, now.x, alpha);
      sy = lerp_pixel(before.y, now.y, alpha);
    }
    if (sy < 0 || sy >= 256) continue;
    enemy_map(line, p, 0x93, read16(f->object_ram, 0xcb8 + offset),
              x, sy, 0, 0, y, viewport, pixels);
  }
}

static void sprite_objects(const SmSourceFrame *f, const SmRasterLine *line,
                           const SnesRenderRegisters *p, int y, SmViewport viewport, uint16_t *pixels,
                           const SmSourceFrame *previous, double alpha) {
  /* $B4:BD32: pickup/effect spritemaps. Read the current instruction's map
   * operand only; never advance instruction timers from presentation. */
  for (unsigned slot = 0; slot < 32; ++slot) {
    SmProjectilePose now, before;
    if (!sprite_object_pose(f, slot, &now)) continue;
    int x = now.x, sy = now.y;
    if (alpha < 1 && sprite_object_motion(f, previous, slot, &now, &before)) {
      x = lerp_pixel(before.x, now.x, alpha);
      sy = lerp_pixel(before.y, now.y, alpha);
    }
    enemy_map(line, p, now.bank, now.map, x, sy,
              now.palette, now.base, y, viewport, pixels);
  }
}

unsigned SmRendererGrapplePieces(const uint8_t ram[0x20000], SmGrapplePiece pieces[17]) {
  if (!ram || !pieces || read16(ram, 0xa5c) != 0xeb86) return 0;
  unsigned length = read16(ram, 0xcfe), function = read16(ram, 0xd32);
  if (!length || (length & 0x8000) || function == 0xc4f0 || function >= 0xc856) return 0;
  int dx = (int16_t)(uint16_t)(read16(ram, 0xd08) - read16(ram, 0xd1a));
  int dy = (int16_t)(uint16_t)(read16(ram, 0xd0c) - read16(ram, 0xd1c));
  if (abs(dx) > 255 || abs(dy) > 255) return 0;
  unsigned x = (unsigned)abs(dx), y = (unsigned)abs(dy), quadrant = (dx < 0 ? 2u : 0u) | (dy < 0 ? 1u : 0u);
  unsigned angle, ratio = (y < x ? (y * 256 / x) : y ? (x * 256 / y) : 65535) >> 3;
  static const int x_offsets[] = {64,64,-64,-64}, x_signs[] = {1,-1,-1,1};
  static const int y_offsets[] = {128,0,128,0}, y_signs[] = {-1,1,1,-1};
  angle = (unsigned)((y < x ? x_offsets[quadrant] : y_offsets[quadrant]) +
                    (int)ratio * (y < x ? x_signs[quadrant] : y_signs[quadrant])) & 255;
  const uint8_t *sine = rom_bytes(0xa0, 0xb3c3 + 2 * angle, 2);
  const uint8_t *cosine = rom_bytes(0xa0, 0xb443 + 2 * angle, 2);
  if (!sine || !cosine) return 0;
  int step_x = (int16_t)read16(cosine, 0) * 2048, step_y = (int16_t)read16(sine, 0) * 2048;
  int sx = (int)read16(ram, 0xd1a) - (int)read16(ram, 0x911) - 4;
  int sy = (int)read16(ram, 0xd1c) - (int)read16(ram, 0x915) - 4;
  unsigned flip = (ram[0xcfb] & 0x80) >> 1;
  flip = (flip | (2 * (((ram[0xcfb] ^ flip) & 0x40) ^ 0x40))) << 8;
  unsigned count = (length / 8) & 15;
  if (!count) count = 1;
  bool stopped = false;
  for (unsigned i = 0; i < count; ++i) {
    unsigned slot = 15 - i, pointer = read16(ram, 0xd62 + slot * 2);
    /* Visible slots have already advanced their instruction in the guest.
     * Slots after its native clipping break have not: peek their next art
     * without changing a timer or executing guest code. */
    if (stopped && read16(ram, 0xd42 + slot * 2) == 1) {
      bool found = false;
      for (unsigned hop = 0; hop < 16; ++hop) {
        const uint8_t *instruction = rom_bytes(0x94, pointer, 4);
        if (!instruction) return 0;
        unsigned value = read16(instruction, 0);
        if (!(value & 0x8000)) { pointer += 4; found = true; break; }
        if (value != 0xb0f4) return 0;
        pointer = read16(instruction, 2);
      }
      if (!found) return 0;
    }
    const uint8_t *art = rom_bytes(0x94, pointer - 2, 2);
    if (!art) return 0;
    int64_t fx = (int64_t)sx * 65536 + (int64_t)step_x * i;
    int64_t fy = (int64_t)sy * 65536 + (int64_t)step_y * i;
    int px = (int)(fx >= 0 ? fx / 65536 : -((-fx + 65535) / 65536));
    int py = (int)(fy >= 0 ? fy / 65536 : -((-fy + 65535) / 65536));
    pieces[i] = (SmGrapplePiece){px, py, (uint16_t)(flip | read16(art, 0)), false};
    stopped |= px < 0 || px > 255 || py < 0 || py > 255;
  }
  unsigned pose = read16(ram, 0xa1c);
  int end_y = (int)read16(ram, 0xd0c) - (int)read16(ram, 0x915);
  if ((end_y >= 0 && end_y < 256) || pose == 0xb2 || pose == 0xb3)
    pieces[count++] = (SmGrapplePiece){(int)read16(ram, 0xd08) - (int)read16(ram, 0x911) - 4, end_y - 4, 0x3a20, true};
  return count;
}

static void grapple_pixels(const SmRasterLine *line, const SnesRenderRegisters *p, int y,
                            SmViewport viewport, uint16_t *pixels,
                            const SmGrapplePiece *pieces, const bool *recover_native, unsigned count) {
  for (unsigned i = count; i-- > 0;) {
    int row = y - pieces[i].y;
    if (row < 0 || row >= 8) continue;
    unsigned attr = pieces[i].attr;
    if (attr & 0x8000) row = 7 - row;
    unsigned base = (p->obsel & 7) * 8192;
    if (attr & 256) base += (((p->obsel >> 3) & 3) + 1) * 4096;
    for (int col = 0; col < 8; ++col) {
      int x = pieces[i].x + col, dest = x + viewport.extra;
      bool native = x >= 0 && x < 256;
      if ((native && !recover_native[i]) || dest < 0 || dest >= viewport.width) continue;
      unsigned pixel = tile_pixel(line->vram, base + (attr & 255) * 16, attr & 0x4000 ? 7 - col : col, row, 4);
      if (pixel) {
        pixels[dest] = (uint16_t)(((((attr >> 12) & 3) * 4 + 2) << 12) |
                      ((attr & 0x800 ? 4 : 6) << 8) | 128 | (((attr >> 9) & 7) * 16) | pixel);
        if (native) ++render_stats.recovered_edge_pixels;
        else ++render_stats.grapple_margin_pixels;
      }
    }
  }
}

static uint32_t colour(const SnesRenderRegisters *p, const uint16_t *palette, const uint8_t *brightness, uint16_t main,
                       uint16_t sub, bool inside) {
  unsigned rgb = main & SM_PIXEL_OPAQUE_BLACK ? 0 : palette[main & 255];
  unsigned layer = (main >> 8) & 7;
  bool clipped = window_condition(p->cgwsel >> 6, inside);
  bool math = !window_condition((p->cgwsel >> 4) & 3, inside) &&
              ((p->cgadsub & 63) & (1u << layer));
  unsigned other = p->fixedColor;
  bool half = math && (p->cgadsub & 64) && !clipped;
  if (math && (p->cgwsel & 2)) {
    if (sub & SM_PIXEL_OPAQUE_BLACK) other = 0;
    else if ((sub & 255) != 0) other = palette[sub & 255];
    else half = false;
  }
  uint32_t result = 0;
  for (int component = 0; component < 3; ++component) {
    int c = clipped ? 0 : (rgb >> (component * 5)) & 31;
    if (math) {
      int second = (other >> (component * 5)) & 31;
      c += p->cgadsub & 128 ? -second : second;
      if (c < 0) c = 0;
      if (half) c /= 2;
      if (c > 31) c = 31;
    }
    c = brightness[c];
    result |= (uint32_t)c << (16 - component * 8);
  }
  return result;
}

#if SM_NATIVE_RASTER && SM_NATIVE_ROW_PLAN
#include "sm_row_plan.inc"
#endif

bool SmRendererDraw(uint32_t *out, SmViewport viewport, bool hud_anchored, double alpha) {
  memset(grapple_motion, 0, sizeof(grapple_motion));
  memset(&render_stats, 0, sizeof(render_stats));
  memset(object_matches, 0, sizeof(object_matches));
  memset(samus_matches, 0, sizeof(samus_matches));
  memset(enemy_matches, 0, sizeof(enemy_matches));
  const SmSourceFrame *f = &frames[current];
  const SmSourceFrame *previous = &frames[current ^ 1];
  if (!isfinite(alpha)) alpha = 1;
  if (alpha < 0) alpha = 0;
  if (alpha > 1) alpha = 1;
  if (!continuous_frames(previous, f)) previous = NULL;
  if (!f->valid || !out || viewport.width < 256 || viewport.width > SM_MAX_WIDTH ||
      viewport.extra < 0 || viewport.width != 256 + 2 * viewport.extra) return false;
  memset(out, 0, (size_t)viewport.width * 224 * sizeof(*out));
  unsigned state = read16(f->ram, 0x998);
  bool room = room_state(state);
  SmBg2Object bg2 = bg2_object(f, previous, alpha);
  int camera_x = (int16_t)read16(f->ram, 0x911);
  int camera_y = (int16_t)read16(f->ram, 0x915);
  if (previous && alpha < 1) {
    camera_x = lerp_pixel((int16_t)read16(previous->ram, 0x911), camera_x, alpha);
    camera_y = lerp_pixel((int16_t)read16(previous->ram, 0x915), camera_y, alpha);
  }
  uint16_t objects[SM_MAX_WIDTH];
  uint8_t mode7_pixels[SM_MAX_WIDTH];
  SmWideWindow wide_windows[224];
  power_bomb_windows(f, wide_windows);
  bool power_bomb = false;
  for (unsigned y = 0; y < 224; ++y) power_bomb |= wide_windows[y].valid;
  bool xray = xray_windows(f, wide_windows);
  bool eye_beam = !xray && !power_bomb && eye_beam_windows(f, wide_windows);
  bool xray_reveal = xray && SmRendererXrayBlocks(f->ram, xray_blocks);
  SmGrapplePiece grapple[17];
  bool grapple_recover_native[17] = {false};
  SmProjectilePose flare, old_flare;
  bool has_flare = grapple_flare_pose(f, &flare);
  if (has_flare) {
    const SmRasterLine *line = &f->lines[64];
    for (unsigned slot = 0; slot < 128; ++slot) {
      int actual_x;
      if (map_piece(flare.bank, flare.map, flare.x, flare.y, 0, 0,
                    line->oam[slot * 2], line->oam[slot * 2 + 1],
                    (line->high_oam[slot / 4] >> (2 * (slot & 3))) & 3, &actual_x))
        ++render_stats.grapple_flare_native_matches;
    }
    has_flare = render_stats.grapple_flare_native_matches != 0;
  }
  if (has_flare && alpha < 1 && grapple_flare_motion(f, previous, &flare, &old_flare)) {
    flare.x = lerp_pixel(old_flare.x, flare.x, alpha);
    flare.y = lerp_pixel(old_flare.y, flare.y, alpha);
  }
  unsigned grapple_count = f->object_state_valid ? SmRendererGrapplePieces(f->object_ram, grapple) : 0;
  for (unsigned i = 0; i < grapple_count; ++i) {
    if (grapple[i].x < 0 || grapple[i].x >= 256 || grapple[i].y < 0 || grapple[i].y >= 256) continue;
    bool matched = false;
    const SmRasterLine *line = &f->lines[64];
    for (unsigned slot = 0; slot < 128; ++slot)
      if (line->oam[slot * 2] == (unsigned)(grapple[i].x | (grapple[i].y << 8)) &&
          line->oam[slot * 2 + 1] == grapple[i].attr &&
          !((line->high_oam[slot / 4] >> (2 * (slot & 3))) & 3)) matched = true;
    if (!matched) { grapple_count = 0; break; }
    ++render_stats.grapple_native_matches;
  }
  if (grapple_count && previous && previous->object_state_valid && alpha < 1 &&
      read16(f->object_ram, 0xd32) == read16(previous->object_ram, 0xd32) &&
      f->object_ram[0xa1e] == previous->object_ram[0xa1e]) {
    bool continuous = true;
    const unsigned coordinates[] = {0xd08,0xd0c,0xd1a,0xd1c};
    for (unsigned i = 0; i < 4; ++i)
      if (abs((int)read16(f->object_ram, coordinates[i]) - (int)read16(previous->object_ram, coordinates[i])) > 32)
        continuous = false;
    SmGrapplePiece before[17];
    unsigned old_count = continuous ? SmRendererGrapplePieces(previous->object_ram, before) : 0;
    int origin_dx = old_count ? before[0].x - grapple[0].x : 0;
    int origin_dy = old_count ? before[0].y - grapple[0].y : 0;
    for (unsigned i = 0; old_count && i < grapple_count; ++i) {
      unsigned old_index = grapple[i].endpoint ? old_count - 1 : i;
      bool new_segment = old_index >= old_count || before[old_index].endpoint != grapple[i].endpoint;
      if (new_segment && grapple[i].endpoint) continue;
      /* Newly emitted segments have no previous piece. Move their current
       * geometry with the beam origin instead of leaving a translation gap. */
      int dx = new_segment ? origin_dx : before[old_index].x - grapple[i].x;
      int dy = new_segment ? origin_dy : before[old_index].y - grapple[i].y;
      if (abs(dx) > 32 || abs(dy) > 32 || (!dx && !dy)) continue;
      grapple_recover_native[i] = grapple[i].x < 0 || grapple[i].x >= 256 ||
                                  grapple[i].y < 0 || grapple[i].y >= 256;
      const SmRasterLine *line = &f->lines[64];
      for (unsigned slot = 0; slot < 128; ++slot) {
        unsigned position = line->oam[slot * 2], attr = line->oam[slot * 2 + 1];
        unsigned high = (line->high_oam[slot / 4] >> (2 * (slot & 3))) & 3;
        if (grapple[i].x >= 0 && grapple[i].x < 256 && grapple[i].y >= 0 && grapple[i].y < 256 &&
            !high && position == (unsigned)(grapple[i].x | (grapple[i].y << 8)) && attr == grapple[i].attr) {
          grapple_motion[slot].position = position; grapple_motion[slot].attr = attr;
          grapple_motion[slot].high = high; grapple_motion[slot].dx = dx;
          grapple_motion[slot].dy = dy; grapple_motion[slot].valid = true;
        }
      }
      grapple[i].x = lerp_pixel(grapple[i].x + dx, grapple[i].x, alpha);
      grapple[i].y = lerp_pixel(grapple[i].y + dy, grapple[i].y, alpha);
      ++render_stats.grapple_interpolated_pieces;
    }
  }
  if (xray_reveal) {
    const char *report_path = getenv("SM_XRAY_TILE_REPORT");
    FILE *report = report_path ? fopen(report_path, "w") : NULL;
    if (report) fputs("world_x,world_y,quadrant,block,expected,actual,bg1_backup,base_room\n", report);
    unsigned w = read16(f->ram, 0x7a5), h = read16(f->ram, 0x7a7);
    unsigned bx = read16(f->ram, 0x911) / 16, by = read16(f->ram, 0x915) / 16;
    for (unsigned y = 0; y < 16 && by + y < h; ++y)
      for (unsigned x = 0; x < 16 && bx + x < w; ++x)
        for (unsigned q = 0; q < 4; ++q) {
          unsigned expected = xray_entry(f->ram, xray_blocks[(by + y) * w + bx + x], q);
          unsigned actual = read16(f->ram, 0x4000 + y * 128 + x * 4 + (q & 1) * 2 + (q >> 1) * 64);
          if (report) {
            unsigned tx = (((read16(f->ram, 0x911) + read16(f->ram, 0x91d)) & ~15u) / 8 + 2 * x + (q & 1)) & 63;
            unsigned ty = (((read16(f->ram, 0x915) + read16(f->ram, 0x91f)) & ~15u) / 8 + 2 * y + (q >> 1)) & 31;
            unsigned backup = read16(f->ram, 0x6000 + 2 * ((tx & 31) + ty * 32 + ((tx & 32) ? 1024 : 0)));
            uint16_t base = 0;
            SmRendererRoomTile(f->ram, (int)((bx + x) * 16 + (q & 1) * 8),
                               (int)((by + y) * 16 + (q >> 1) * 8), &base);
            fprintf(report, "%u,%u,%u,%04x,%04x,%04x,%04x,%04x\n", bx + x, by + y, q,
                    xray_blocks[(by + y) * w + bx + x], expected, actual, backup, base);
          }
          ++render_stats.xray_tiles_compared;
          if (expected != actual) ++render_stats.xray_tiles_differ;
        }
    if (report) fclose(report);
  }
  for (int y = 0; y < 224; ++y) {
    const SmRasterLine *l = &f->lines[y];
    memcpy(&scanout, l->registers, PPU_SAVESTATE_REGS_SIZE);
    uint8_t brightness[32];
    for (unsigned c = 0; c < 32; ++c)
      brightness[c] = (uint8_t)(((c << 3) | (c >> 2)) * (scanout.inidisp & 15) / 15);
    /* The IRQ status band is an opaque screen-space composite. Moving its
     * three column groups together retains HUD OBJ icons, palette effects
     * and energy digits without moving similarly shaped world sprites. */
    if (room && y < 32 && scanout.screenEnabled[0] == 4 &&
        scanout.cgadsub == 0 && scanout.cgwsel == 0) {
      ++render_stats.hud_lines;
      uint32_t *row = out + y * viewport.width;
      const uint32_t *native = f->stock + y * 256;
      if (hud_anchored) {
        memcpy(row, native, 80 * sizeof(*row));
        memcpy(row + 80 + viewport.extra, native + 80, 128 * sizeof(*row));
        memcpy(row + 208 + 2 * viewport.extra, native + 208, 48 * sizeof(*row));
      } else {
        memcpy(row + viewport.extra, native, 256 * sizeof(*row));
      }
      continue;
    }
    int mode = scanout.bgmode & 7;
    if (!room || (mode != 1 && mode != 7)) {
      ++render_stats.stock_lines;
      memcpy(out + y * viewport.width + viewport.extra,
             f->stock + y * 256, 256 * sizeof(*out));
      continue;
    }
    ++render_stats.custom_lines;
    if (mode == 7) ++render_stats.mode7_lines;
    if (wide_windows[y].valid) {
      if (xray) ++render_stats.xray_lines;
      else if (eye_beam) ++render_stats.eye_beam_lines;
      else ++render_stats.power_bomb_lines;
    }
    if (scanout.inidisp & 128) continue;
#if SM_NATIVE_RASTER
    int wl[2] = {scanout.window1left == 0 ? -viewport.extra : scanout.window1left,
                 scanout.window2left == 0 ? -viewport.extra : scanout.window2left};
    int wr[2] = {scanout.window1right == 255 ? 255 + viewport.extra : scanout.window1right,
                 scanout.window2right == 255 ? 255 + viewport.extra : scanout.window2right};
    SnesRenderWindowPlan native_windows = SnesRenderRegistersWindows(&scanout, wl[0], wr[0], wl[1], wr[1]);
    if (wide_windows[y].valid) {
      unsigned which = !wide_windows[y].first;
      wl[which] = wide_windows[y].left; wr[which] = wide_windows[y].right;
    }
    SnesRenderWindowPlan margin_windows = SnesRenderRegistersWindows(&scanout, wl[0], wr[0], wl[1], wr[1]);
#endif
    int mode7_y = y + 1;
    int mode7_size = (scanout.mosaic >> 4) + 1;
    /* Mode 7 quantizes screen coordinates before the affine transform and
     * flips. BG1 controls vertical mosaic for the shared Mode 7 source. */
    if (scanout.mosaic & 1) mode7_y -= mode7_y % mode7_size;
    SmMode7Line transform = SmMode7Transform(scanout.m7matrix, scanout.m7sel, mode7_y);
    if (mode == 7 && previous && alpha < 1) {
      SnesRenderRegisters old;
      memcpy(&old, previous->lines[y].registers, PPU_SAVESTATE_REGS_SIZE);
      if ((old.bgmode & 7) == 7 && old.mosaic == scanout.mosaic)
        transform = SmMode7Interpolate(
            SmMode7Transform(old.m7matrix, old.m7sel, mode7_y), transform, alpha);
    }
    SmBg2Object line_bg2 = bg2;
    line_bg2.parked = scanout.hScroll[1] == 256;
    /* Interpolate raster scroll registers, not the final composed image.
     * Layout/mode changes and large jumps snap to the current frame. */
    if (previous && alpha < 1) {
      SnesRenderRegisters old;
      memcpy(&old, previous->lines[y].registers, PPU_SAVESTATE_REGS_SIZE);
      line_bg2.parked |= old.hScroll[1] == 256;
      if (old.bgmode == scanout.bgmode && old.bgTileAdr == scanout.bgTileAdr &&
          !memcmp(old.bgXsc, scanout.bgXsc, sizeof(old.bgXsc))) {
        for (int layer = 0; layer < (mode == 7 ? 0 : 3); ++layer) {
          unsigned size = (scanout.bgmode >> layer) & 0x10 ? 16 : 8;
          int dx = background_scroll_delta((int)old.hScroll[layer] - scanout.hScroll[layer],
                                            scanout.bgXsc[layer], size, false);
          int dy = background_scroll_delta((int)old.vScroll[layer] - scanout.vScroll[layer],
                                            scanout.bgXsc[layer], size, true);
          if (layer == 0 && ((dx && abs(dx) <= 32) || (dy && abs(dy) <= 32)))
            ++render_stats.interpolated_scroll_lines;
          if (abs(dx) <= 32)
            scanout.hScroll[layer] = (uint16_t)(scanout.hScroll[layer] + lerp_pixel(dx, 0, alpha));
          if (abs(dy) <= 32)
            scanout.vScroll[layer] = (uint16_t)(scanout.vScroll[layer] + lerp_pixel(dy, 0, alpha));
        }
      }
    }
    SmBackgroundLine background_lines[3];
    if (mode == 1)
      for (unsigned layer = 0; layer < 3; ++layer)
        background_lines[layer] = background_line(&scanout, f->ram, layer, line_bg2);
    if (xray_reveal && mode == 1 && scanout.bgXsc[1] == 0x49 && scanout.windowsel == 0x8008c8) {
      background_lines[1].room = f->ram;
      background_lines[1].revealed = xray_blocks;
      background_lines[1].world_x = ((int)read16(f->ram, 0x911) & ~15) + (scanout.hScroll[1] & 15);
      background_lines[1].world_y = ((int)read16(f->ram, 0x915) & ~15) + (scanout.vScroll[1] & 15);
    }
    bool reuse_center = false;
#if SM_NATIVE_RASTER && SM_NATIVE_CENTER_REUSE
    reuse_center = mode == 1 && alpha >= 1 && native_centers[current][y];
    if (reuse_center) ++render_stats.native_center_lines;
#endif
    sprites(&scanout, l, y, viewport, objects, f, previous, alpha, reuse_center);
    if (viewport.extra && renderer_rom) {
      /* Reverse $A0:884D's emission order because pixel writes overwrite.
       * The native center continues to use captured OAM exclusively. */
      for (int phase = 7; phase >= 0; --phase) {
        enemies(f, l, &scanout, y, viewport, objects, (unsigned)phase, previous, alpha);
        if (phase == 6) enemy_projectiles(f, l, &scanout, y, viewport, objects, false, previous, alpha);
        if (phase == 3) player_projectiles(f, l, &scanout, y, viewport, objects, false, previous, alpha);
      }
      enemy_projectiles(f, l, &scanout, y, viewport, objects, true, previous, alpha);
      player_projectiles(f, l, &scanout, y, viewport, objects, true, previous, alpha);
      sprite_objects(f, l, &scanout, y, viewport, objects, previous, alpha);
    }
    grapple_pixels(l, &scanout, y, viewport, objects, grapple, grapple_recover_native, grapple_count);
    if (viewport.extra && has_flare)
      enemy_map(l, &scanout, flare.bank, flare.map, flare.x, flare.y, 0, 0, y, viewport, objects);
    if (mode == 7)
      for (int sx = 0; sx < viewport.width; ++sx)
        mode7_pixels[sx] = SmMode7Sample(&transform, l->vram, sx - viewport.extra);
    unsigned enabled = scanout.screenEnabled[0] | scanout.screenEnabled[1];
#if SM_NATIVE_RASTER
    /* The hardware center uses unmodified tilemap addressing; scene-aware
     * room/object reconstruction still owns margins. Decode each enabled
     * ordinary layer a tile at a time after raster-scroll interpolation. */
    uint16_t native_backgrounds[3][256];
    bool native_layer[3] = {false, false, false};
    if (mode == 1 && !reuse_center) for (unsigned layer = 0; layer < 3; ++layer) {
      if (!(enabled & (1u << layer)) ||
          ((scanout.mosaic & (1u << layer)) && (scanout.mosaic >> 4))) continue;
      native_layer[layer] = SnesRenderRegistersMode1TileRow(&scanout, l->vram, layer,
          0, y + 1, 256, native_backgrounds[layer]);
    }
#endif
    /* BG1 scroll is meaningful when terrain is enabled. Color-only and
     * BG2-only effect lines can leave its registers stale. */
    int matte_x = mode == 1 && (enabled & 1) ? background_lines[0].world_x : camera_x;
    int matte_y = mode == 1 && (enabled & 1) ? background_lines[0].world_y : camera_y;
#if SM_NATIVE_RASTER && SM_NATIVE_ROW_PLAN
    if (mode == 1) {
      native_render_mode1_row(&scanout, l, background_lines, native_backgrounds,
          native_layer, objects, y, viewport, matte_x, matte_y, brightness,
          &native_windows, &margin_windows, reuse_center ? f->stock + y * 256 : NULL,
          out + y * viewport.width);
      continue;
    }
#endif
    for (int sx = 0; sx < viewport.width; ++sx) {
      int x = sx - viewport.extra;
      /* The room matte owns the finished world pixel, including OBJ and
       * liquid/window color math. Clipping only individual BG samples lets
       * COLDATA and effects paint over the black side bars. Use the same
       * raster/interpolated BG1 camera as terrain so the edge cannot wobble
       * between simulation ticks and intermediate presentations. */
      const SmBackgroundLine *world = &background_lines[0];
      if (mode == 1 && (x < 0 || x >= 256) && world->room_width &&
          (matte_x + x < 0 || matte_x + x >= world->room_width ||
           matte_y + y + 1 < 0 || matte_y + y + 1 >= world->room_height)) continue;
      uint16_t screens[2] = {0x500, 0x500};
#if SM_NATIVE_RASTER
      unsigned window_mask = SnesRenderWindowMask(x < 0 || x >= 256 ? &margin_windows : &native_windows, x);
#endif
      uint16_t backgrounds[3] = {0, 0, 0};
      int layers = mode == 7 ? ((scanout.setini & 64) ? 2 : 1) : 3;
      /* Tile addressing/decoding is identical on main and sub. Cache it
       * before applying each screen's independent layer/window enables. */
      for (int layer = 0; layer < layers; ++layer) {
        if (!(enabled & (1u << layer))) continue;
        if (mode == 7) {
          unsigned index = mode7_pixels[sx];
          if ((scanout.mosaic & (1u << layer)) && mode7_size > 1) {
            int mx = x - ((x % mode7_size) + mode7_size) % mode7_size;
            index = SmMode7Sample(&transform, l->vram, mx);
          }
          unsigned priority = 5;
          if (layer == 1) {
            priority = index & 128 ? 9 : 1;
            index &= 127;
          }
          backgrounds[layer] = index ? (priority << 12) | (layer << 8) | index : 0;
        } else {
#if SM_NATIVE_RASTER
          if (native_layer[layer] && x >= 0 && x < 256) {
            backgrounds[layer] = native_backgrounds[layer][x];
            continue;
          }
#endif
          int sample_x = x, sample_y = y + 1;
          if (scanout.mosaic & (1u << layer)) {
            int size = (scanout.mosaic >> 4) + 1;
            /* Mosaic precedes scroll and tile lookup, independently per BG.
             * Floor division extends the native X=0 grid into negative X;
             * C's truncating remainder would shift blocks in the left margin. */
            sample_x -= ((sample_x % size) + size) % size;
            sample_y -= sample_y % size;
          }
          bool margin = sample_x < 0 || sample_x >= 256;
          backgrounds[layer] = background_pixel(&background_lines[layer], l->vram,
                                                 sample_x, sample_y, margin);
        }
      }
      for (int sub = 0; sub < 2; ++sub) {
        for (int layer = 0; layer < layers; ++layer) {
          if (!(scanout.screenEnabled[sub] & (1u << layer))) continue;
          if ((scanout.screenWindowed[sub] & (1u << layer)) &&
#if SM_NATIVE_RASTER
              (window_mask & (1u << layer))) continue;
#else
              in_window(&scanout, layer, x, viewport.extra, &wide_windows[y])) continue;
#endif
          if (backgrounds[layer] > screens[sub]) screens[sub] = backgrounds[layer];
        }
        if ((scanout.screenEnabled[sub] & 16) &&
            (!(scanout.screenWindowed[sub] & 16) ||
#if SM_NATIVE_RASTER
             !(window_mask & 16)) &&
#else
             !in_window(&scanout, 4, x, viewport.extra, &wide_windows[y])) &&
#endif
            objects[sx] > screens[sub]) screens[sub] = objects[sx];
      }
      out[y * viewport.width + sx] = colour(&scanout, l->palette, brightness, screens[0], screens[1],
#if SM_NATIVE_RASTER
                                            (window_mask & 32) != 0);
#else
                                            in_window(&scanout, 5, x, viewport.extra, &wide_windows[y]));
#endif
    }
  }
  return true;
}
