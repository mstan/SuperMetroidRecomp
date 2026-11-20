/* Presentation state belongs to the captured timeline. Restoring a game
 * invalidates the capture, so a saved game never carries a half-drawn fade. */
typedef struct SmDoorVisual {
  unsigned frame, room, direction;
  int x, y;
  int light_gain[3]; /* Q8 departure-to-arrival channel gain, relative to unity. */
  bool active;
} SmDoorVisual;

static SmDoorPresentation door_style;
static SmDoorVisual door_visual;

void SmRendererSetDoorPresentation(SmDoorPresentation style) {
  if (door_style != style) door_visual.active = false;
  door_style = style;
}

/* Ordinary doors use the completed Mode 1 world palette. Sample the BG
 * palettes below the HUD, excluding their transparent entries. */
static void door_ambient(const SmSourceFrame *f, int color[3]) {
  unsigned count = 0;
  for (unsigned i = 1; i < 128; ++i) {
    unsigned rgb = f->lines[100].palette[i] & 0x7fff;
    if (!(i & 15) || !rgb) continue;
    for (unsigned c = 0; c < 3; ++c)
      color[c] += (rgb >> (5 * c)) & 31;
    ++count;
  }
  if (count) for (unsigned c = 0; c < 3; ++c)
    color[c] = (color[c] * 8 + count / 2) / count;
}

static bool door_gameplay_field(const SmSourceFrame *f) {
  if (!f->valid || !f->rooms || !f->rooms->seamless || f->rooms->native ||
      f->rooms->root != read16(f->ram, 0x79b) || read16(f->ram, 0x998) != 8)
    return false;
  SnesRenderRegisters p;
  memcpy(&p, f->lines[100].registers, PPU_SAVESTATE_REGS_SIZE);
  return (p.bgmode & 7) == 1 && p.inidisp == 15;
}

static void update_door_visual(const SmSourceFrame *before, const SmSourceFrame *now) {
  if (door_style == SM_DOOR_PRESENT_REFERENCE) return;
  if (!door_gameplay_field(now) || !door_gameplay_field(before) ||
      now->number != before->number + 1) {
    door_visual.active = false;
    return;
  }
  unsigned room = now->rooms->root, departed = before->rooms->root;
  if (room == departed) return;

  door_visual.active = false;
  unsigned count, definition = read16(now->ram, 0x78d);
  const SmWorldDoor *doors = SmRoomPreviewDoors((uint16_t)departed, &count);
  const SmWorldDoor *door = NULL;
  for (unsigned i = 0; i < count; ++i)
    if (doors[i].definition == definition && doors[i].destination == room) {
      door = &doors[i];
      break;
    }
  if (!door || !door->reciprocal || (door->flags & 0x80) ||
      door->block_x < 0 || door->block_y < 0) return;

  /* Only a seamless handoff retains the departing active room at this
   * clipped door plane. Use that shared geometry in arrival coordinates. */
  const SmRoomPlacement *departure = NULL;
  for (unsigned i = 0; i < now->rooms->count; ++i)
    if (now->rooms->rooms[i].placement.room == departed) {
      departure = &now->rooms->rooms[i].placement;
      break;
    }
  if (!departure) return;

  unsigned direction = door->direction & 3;
  int x = departure->x + (door->block_x + (direction == 0)) * 16;
  int y = departure->y + (door->block_y + (direction == 2)) * 16;
  int plane = direction == 0 ? departure->right : direction == 1 ? departure->left :
      direction == 2 ? departure->bottom : departure->top;
  if (plane != (direction < 2 ? x : y)) return;
  if (direction < 2) y += 32;
  else x += 32;
  door_visual = (SmDoorVisual){.frame = now->number, .room = room,
      .direction = direction, .x = x, .y = y, .active = true};

  if (door_style == SM_DOOR_PRESENT_LIGHTING) {
    int old_color[3] = {0}, new_color[3] = {0};
    door_ambient(before, old_color);
    door_ambient(now, new_color);
    for (unsigned c = 0; c < 3; ++c) {
      /* Bound the change around the new palette, including very dark rooms. */
      int gain = (old_color[c] - new_color[c]) * 192 / (new_color[c] + 32);
      door_visual.light_gain[c] = gain < -64 ? -64 : gain > 64 ? 64 : gain;
    }
  }
}

static double door_ease(double t) { return t * t * (3 - 2 * t); }

static unsigned door_shadow_weight(double distance, unsigned solid, unsigned limit) {
  if (distance >= limit) return 0;
  if (distance <= solid) return 256;
  return (unsigned)(256 * (1 - door_ease((distance - solid) / (limit - solid))));
}

static uint32_t door_light_pixel(uint32_t pixel, const unsigned gain[3]) {
  uint32_t result = pixel & 0xff000000u;
  for (unsigned c = 0; c < 3; ++c) {
    unsigned shift = 16 - c * 8;
    unsigned value = (((pixel >> shift) & 255) * gain[c] + 128) >> 8;
    result |= (value < 255 ? value : 255) << shift;
  }
  return result;
}

bool SmRendererApplyDoorPresentation(uint32_t *out, SmViewport view, double alpha) {
  const SmSourceFrame *now = &frames[current], *before = &frames[current ^ 1];
  unsigned age = now->number - door_visual.frame;
  const unsigned duration = door_style == SM_DOOR_PRESENT_SHADOW ? 9 : 12;
  if (!out || view.width < SM_STOCK_WIDTH || view.width > SM_MAX_WIDTH ||
      !door_visual.active || age > duration || !now->valid || !now->rooms ||
      now->rooms->root != door_visual.room) return false;

  double progress = age ? (age - 1 + fmax(0, fmin(1, alpha))) / duration : 0;
  double strength = 1 - door_ease(progress);
  if (door_style == SM_DOOR_PRESENT_LIGHTING) {
    unsigned gain[3];
    for (unsigned c = 0; c < 3; ++c)
      gain[c] = (unsigned)(256 + lround(door_visual.light_gain[c] * strength));
    if (gain[0] == 256 && gain[1] == 256 && gain[2] == 256) return false;
    for (int i = 32 * view.width; i < SM_HEIGHT * view.width; ++i)
      out[i] = door_light_pixel(out[i], gain);
    return true;
  }

  int cx = (int16_t)read16(now->ram, 0x911) + now->rooms->camera_dx;
  int cy = (int16_t)read16(now->ram, 0x915) + now->rooms->camera_dy;
  if (age && alpha < 1 && continuous_frames(before, now) && before->rooms) {
    cx = lerp_pixel((int16_t)read16(before->ram, 0x911) + before->rooms->camera_dx, cx, alpha);
    cy = lerp_pixel((int16_t)read16(before->ram, 0x915) + before->rooms->camera_dy, cy, alpha);
  }
  bool horizontal = door_visual.direction < 2;
  int advance = (int)lround(24 * door_ease(progress)) * (door_visual.direction & 1 ? -1 : 1);
  int x = door_visual.x - cx + view.extra + (horizontal ? advance : 0);
  int y = door_visual.y - cy + (horizontal ? 0 : advance);
  unsigned peak = (unsigned)lround(122 * strength);
  if (!peak) return false;

  int half_width = horizontal ? 42 : 54, half_height = horizontal ? 54 : 42;
  int left = x - half_width > 0 ? x - half_width : 0;
  int right = x + half_width < view.width ? x + half_width : view.width;
  int top = y - half_height > 32 ? y - half_height : 32;
  int bottom = y + half_height < SM_HEIGHT ? y + half_height : SM_HEIGHT;
  if (left >= right || top >= bottom) return false;
  unsigned columns[SM_MAX_WIDTH];
  for (int px = left; px < right; ++px)
    columns[px] = door_shadow_weight(abs(px - x), horizontal ? 9 : 19, half_width);
  for (int py = top; py < bottom; ++py) {
    unsigned row = peak * door_shadow_weight(abs(py - y), horizontal ? 19 : 9, half_height);
    for (int px = left; px < right; ++px) {
      unsigned scale = 256 - ((row * columns[px]) >> 16);
      const unsigned gain[3] = {scale, scale, scale};
      out[py * view.width + px] = door_light_pixel(out[py * view.width + px], gain);
    }
  }
  return true;
}
