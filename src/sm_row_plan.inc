/* Private captured Mode-1 row. Room/camera policy remains in this title;
 * the shared planner owns screen/window decisions over immutable inputs. */
#include "snes/raster_plan.h"
#include <assert.h>

static void native_background_range(const Ppu *p, const SmRasterLine *line,
    const SmBackgroundLine *background, unsigned layer, int y, int extra,
    unsigned begin, unsigned end, uint16_t *pixels) {
  int sample_y = y + 1;
  unsigned size = p->mosaic & (1u << layer) ? (p->mosaic >> 4) + 1 : 1;
  if (size > 1) sample_y -= sample_y % (int)size;
  for (unsigned sx = begin; sx < end; ++sx) {
    int x = (int)sx - extra;
    if (size > 1) x -= ((x % (int)size) + (int)size) % (int)size;
    pixels[sx] = background_pixel(background, line->vram, x, sample_y, x < 0 || x >= 256);
  }
}

static void native_compose_range(const Ppu *p, const SmRasterLine *line,
    SmViewport view,
    const uint16_t backgrounds[3][SM_MAX_WIDTH], const uint16_t *objects,
    const uint8_t brightness[32], const SnesRenderWindowPlan *windows,
    int first, int end, uint32_t *out) {
  if (end <= first) return;
  SnesRenderComposePlan plan;
  bool planned = SnesRenderPlanCompose(&plan, p, windows, first, (unsigned)(end - first), NULL, 0);
  assert(planned);
  if (!planned) return;
  unsigned offset = (unsigned)(first + view.extra);
  for (unsigned s = 0; s < plan.count; ++s) {
    const SnesRenderComposeSpan *span = &plan.spans[s];
    for (unsigned sx = offset + span->begin; sx < offset + span->end; ++sx) {
      uint16_t main = 0x500, sub = 0x500;
      uint16_t b0 = backgrounds[0][sx], b1 = backgrounds[1][sx], b2 = backgrounds[2][sx];
      uint16_t candidate = b0 & span->main[0];
      if (candidate > main) main = candidate;
      candidate = b1 & span->main[1];
      if (candidate > main) main = candidate;
      candidate = b2 & span->main[2];
      if (candidate > main) main = candidate;
      candidate = objects[sx] & span->main[3];
      if (candidate > main) main = candidate;
      candidate = b0 & span->sub[0];
      if (candidate > sub) sub = candidate;
      candidate = b1 & span->sub[1];
      if (candidate > sub) sub = candidate;
      candidate = b2 & span->sub[2];
      if (candidate > sub) sub = candidate;
      candidate = objects[sx] & span->sub[3];
      if (candidate > sub) sub = candidate;
      out[sx] = colour(p, line->palette, brightness, main, sub, span->color_window);
    }
  }
}

static void native_render_mode1_row(const Ppu *p, const SmRasterLine *line,
    const SmBackgroundLine background_lines[3], const uint16_t center[3][256],
    const bool center_valid[3], const uint16_t *objects, int y, SmViewport view,
    int matte_x, int matte_y, const uint8_t brightness[32],
    const SnesRenderWindowPlan *native_windows, const SnesRenderWindowPlan *margin_windows,
    const uint32_t *native_center, uint32_t *out) {
  uint16_t backgrounds[3][SM_MAX_WIDTH];
  int first[2] = {-view.extra, 256}, end[2] = {0, 256 + view.extra};
  /* The authored room matte owns the finished pixel, including COLDATA/OBJ.
   * Clip margins before preparing their samples, never the hardware center. */
  const SmBackgroundLine *world = &background_lines[0];
  if (world->room_width) for (unsigned side = 0; side < 2; ++side) {
    if (matte_y + y + 1 < 0 || matte_y + y + 1 >= world->room_height) end[side] = first[side];
    else {
      if (first[side] < -matte_x) first[side] = -matte_x;
      if (end[side] > world->room_width - matte_x) end[side] = world->room_width - matte_x;
    }
  }
  unsigned enabled = p->screenEnabled[0] | p->screenEnabled[1];
  for (unsigned layer = 0; layer < 3; ++layer) {
    if (!(enabled & (1u << layer))) {
      memset(backgrounds[layer], 0, (size_t)view.width * sizeof(uint16_t));
    } else {
      /* A finished center needs no decoded center samples. */
      if (!native_center) {
        if (center_valid[layer])
          memcpy(backgrounds[layer] + view.extra, center[layer], 256 * sizeof(uint16_t));
        else
          native_background_range(p, line, &background_lines[layer], layer, y, view.extra,
              (unsigned)view.extra, (unsigned)view.extra + 256, backgrounds[layer]);
      }
      for (unsigned side = 0; side < 2; ++side)
        if (end[side] > first[side])
          native_background_range(p, line, &background_lines[layer], layer, y, view.extra,
              (unsigned)(first[side] + view.extra), (unsigned)(end[side] + view.extra), backgrounds[layer]);
    }
  }
  native_compose_range(p, line, view, backgrounds, objects, brightness,
      margin_windows, first[0], end[0], out);
  if (native_center)
    memcpy(out + view.extra, native_center, 256 * sizeof(*out));
  else
    native_compose_range(p, line, view, backgrounds, objects, brightness,
        native_windows, 0, 256, out);
  native_compose_range(p, line, view, backgrounds, objects, brightness,
      margin_windows, first[1], end[1], out);
}
