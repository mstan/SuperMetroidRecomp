/* Presentation scene, compiled once from a completed immutable publication.
 *
 * Object identity belongs to the game object, not to an OAM reservation, tile,
 * pose, or animation frame. Keep the current art and interpolate the owner.
 * OAM is used only to bind the publication's exact pieces to those owners;
 * presentation never searches old OAM for a corresponding animation piece.
 * This is the same separation used by SonicAnthology's SceneFrame/IxFrame.
 */
enum {
  SM_SCENE_SAMUS = 1,
  SM_SCENE_ENEMIES,
  SM_SCENE_EPROJ = SM_SCENE_ENEMIES + 32,
  SM_SCENE_PPROJ = SM_SCENE_EPROJ + 18,
  SM_SCENE_EFFECTS = SM_SCENE_PPROJ + 10,
  SM_SCENE_FLARE = SM_SCENE_EFFECTS + 32,
  SM_SCENE_ACTORS
};
typedef struct SmSceneActor {
  SmProjectilePose pose;
  bool valid;
} SmSceneActor;
struct SmScenePiece {
  int x;
  unsigned owner;
};
typedef struct SmScene {
  SmSceneActor actors[SM_SCENE_ACTORS];
} SmScene;
static SmScene scenes[2];
static SnesRasterSnapshotStore scene_pieces[2];

static void scene_reset(void) {
  memset(scenes, 0, sizeof(scenes));
  for (unsigned i = 0; i < 2; ++i) SnesRasterSnapshotDestroy(&scene_pieces[i]);
}

static void scene_actors(const SmSourceFrame *f, SmScene *scene) {
  memset(scene, 0, sizeof(*scene));
  if (!f->object_state_valid || !gameplay_actor_state(read16(f->ram,0x998))) return;
  const uint8_t *ram = f->object_ram;
  SmSceneActor *samus = &scene->actors[SM_SCENE_SAMUS];
  samus->valid = true;
  samus->pose.world_x = (int16_t)read16(ram, 0xaf6);
  samus->pose.world_y = (int16_t)read16(ram, 0xafa);
  samus->pose.x = samus->pose.world_x - (int16_t)read16(ram, 0x911);
  samus->pose.y = samus->pose.world_y - (int16_t)read16(ram, 0x915);
  project_player_position(ram, 0, samus->pose.world_x, samus->pose.world_y,
      &samus->pose.x, &samus->pose.y);
  /* $B04/$B06 include pose-specific graphics offsets. They bind the current
   * map, but are not the physical owner: easing those offsets makes a crouch
   * or landing slide vertically between unrelated animation poses. */
  samus->pose.identity = 1;
  for (unsigned slot = 0; slot < 32; ++slot) {
    const uint8_t *e = ram + 0xf78 + slot * 64;
    SmSceneActor *a = &scene->actors[SM_SCENE_ENEMIES + slot];
    /* The old post-draw shake timer cannot establish whether its last shake
     * was drawn. Leave this piece discrete until the producer supplies it. */
    a->valid = read16(e, 0) && read16(e, 0) != 0xdaff &&
        !(read16(e, 14) & 0x300) && !read16(e, 42) && read16(e, 22) >= 0x8000;
    a->pose.identity = read16(e, 0) | ((unsigned)e[46] << 16);
    a->pose.world_x = (int16_t)read16(e, 2) + (int16_t)read16(ram, 0x7010 + slot * 64);
    a->pose.world_y = (int16_t)read16(e, 6) + (int16_t)read16(ram, 0x7012 + slot * 64);
    a->pose.x = a->pose.world_x - (int16_t)read16(ram, 0x911);
    a->pose.y = a->pose.world_y - (int16_t)read16(ram, 0x915);
  }
  for (unsigned slot = 0; slot < 18; ++slot) {
    SmSceneActor *a = &scene->actors[SM_SCENE_EPROJ + slot];
    a->valid = projectile_pose(f, true, slot, &a->pose);
  }
  for (unsigned slot = 0; slot < 10; ++slot) {
    SmSceneActor *a = &scene->actors[SM_SCENE_PPROJ + slot];
    a->valid = projectile_pose(f, false, slot, &a->pose);
  }
  for (unsigned slot = 0; slot < 32; ++slot) {
    SmSceneActor *a = &scene->actors[SM_SCENE_EFFECTS + slot];
    a->valid = sprite_object_pose(f, slot, &a->pose);
  }
  SmSceneActor *flare = &scene->actors[SM_SCENE_FLARE];
  flare->valid = grapple_flare_pose(f, &flare->pose);
}

static SmScenePiece scene_piece(const SmSourceFrame *f, const SmScene *scene,
                                unsigned position, unsigned attr, unsigned high) {
  SmScenePiece piece = {0};
  for (unsigned owner = 1; owner < SM_SCENE_ACTORS; ++owner) {
    const SmSceneActor *a = &scene->actors[owner];
    if (!a->valid) continue;
    int x = 0;
    bool matches;
    if (owner == SM_SCENE_SAMUS) {
      matches = samus_piece(f, position, attr, high);
      int local = (int)(((position & 255) | ((high & 1) << 8)) - a->pose.x) & 511;
      x = a->pose.x + (local >= 256 ? local - 512 : local);
    } else if (owner < SM_SCENE_EPROJ) {
      matches = enemy_piece(f, owner - SM_SCENE_ENEMIES, position, attr, high, &x);
    } else {
      matches = map_piece(a->pose.bank, a->pose.map, a->pose.x, a->pose.y,
          a->pose.palette, a->pose.base, position, attr, high, &x);
    }
    if (!matches) continue;
    /* Do not invent provenance where two live owners describe one piece. */
    if (piece.owner && piece.owner != owner) return (SmScenePiece){0};
    piece = (SmScenePiece){x, owner};
  }
  return piece;
}

static bool scene_build(SmSourceFrame *f) {
  unsigned index = (unsigned)(f - frames);
  SmScene *scene = &scenes[index];
  if (!(f->rooms && f->rooms->ownership) && !interpolation_enabled && !getenv("SM_CAPTURE_MOTION_PREFIX") &&
      !getenv("SM_CAPTURE_EDGE_PREFIX")) {
    memset(scene, 0, sizeof(*scene));
    for (unsigned y = 0; y < 224; ++y) f->lines[y].scene = NULL;
    return true;
  }
  scene_actors(f, scene);
  if (!SnesRasterSnapshotBegin(&scene_pieces[index], sizeof(SmScenePiece) * 128, 224))
    return false;
  SmScenePiece pieces[128];
  for (unsigned y = 0; y < 224; ++y) {
    SmRasterLine *line = &f->lines[y];
    const SmRasterLine *old = y ? &f->lines[y - 1] : NULL;
    if (old && !memcmp(line->oam, old->oam, sizeof(line->oam)) &&
        !memcmp(line->high_oam, old->high_oam, sizeof(line->high_oam))) {
      line->scene = old->scene;
      continue;
    }
    memset(pieces, 0, sizeof(pieces));
    if (f->object_state_valid) for (unsigned slot = 0; slot < 128; ++slot) {
      unsigned high = (line->high_oam[slot / 4] >> (2 * (slot & 3))) & 3;
      pieces[slot] = scene_piece(f, scene, line->oam[slot * 2], line->oam[slot * 2 + 1], high);
    }
    line->scene = SnesRasterSnapshotCapture(&scene_pieces[index], pieces, true);
    if (!line->scene) return false;
  }
  return true;
}

static bool scene_motion(const SmSourceFrame *f, const SmSourceFrame *previous,
                         unsigned owner, int *dx, int *dy) {
  if (!previous || !owner || owner >= SM_SCENE_ACTORS) return false;
  const SmSceneActor *a = &scenes[f - frames].actors[owner];
  const SmSceneActor *b = &scenes[previous - frames].actors[owner];
  if (!a->valid || !b->valid || a->pose.identity != b->pose.identity) {
    /* B4 instructions advance independently of owner lifetime. Retain the
     * verified normal-animation contract until producer generation IDs exist. */
    SmProjectilePose now, before;
    if (owner < SM_SCENE_EFFECTS || owner >= SM_SCENE_FLARE ||
        !sprite_object_motion(f, previous, owner - SM_SCENE_EFFECTS, &now, &before)) return false;
  }
  if (owner >= SM_SCENE_EPROJ && owner < SM_SCENE_PPROJ) {
    unsigned offset = (owner - SM_SCENE_EPROJ) * 2;
    if (read16(f->object_ram, 0x1bd7 + offset) != read16(previous->object_ram, 0x1bd7 + offset))
      return false;
  }
  *dx = b->pose.x - a->pose.x;
  *dy = b->pose.y - a->pose.y;
  return abs(a->pose.world_x - b->pose.world_x) <= 32 &&
      abs(a->pose.world_y - b->pose.world_y) <= 32 && abs(*dx) <= 32 && abs(*dy) <= 32;
}

static bool has_samus_motion(const SmSourceFrame *f, const SmSourceFrame *old) {
  int dx, dy;
  if (!scene_motion(f, old, SM_SCENE_SAMUS, &dx, &dy) || (!dx && !dy)) return false;
  const SmRasterLine *line = &f->lines[100];
  for (unsigned slot = 0; line->scene && slot < 128; ++slot)
    if (line->scene[slot].owner == SM_SCENE_SAMUS) return true;
  return false;
}

static bool has_edge_motion(const SmSourceFrame *f, const SmSourceFrame *old) {
  const SmRasterLine *line = &f->lines[100];
  if (!line->scene) return false;
  SnesRenderRegisters p;
  memcpy(&p, line->registers, PPU_SAVESTATE_REGS_SIZE);
  if (!(p.screenEnabled[0] & 16) || (p.inidisp & 128) || !(p.inidisp & 15)) return false;
  static const int sizes[8][2] = {{8,16},{8,32},{8,64},{16,32},
                                  {16,64},{32,64},{16,32},{16,32}};
  for (unsigned slot = 0; slot < 128; ++slot) {
    unsigned position = line->oam[slot * 2];
    unsigned high = line->high_oam[slot / 4] >> ((slot & 3) * 2);
    int size = sizes[p.obsel >> 5][(high >> 1) & 1];
    int native_x = (position & 255) | ((high & 1) << 8);
    if (native_x >= 256) native_x -= 512;
    if (native_x + size > 0 && native_x < 256) continue;
    const SmScenePiece *piece = &line->scene[slot];
    int dx, dy;
    if (!scene_motion(f, old, piece->owner, &dx, &dy)) continue;
    int x = lerp_pixel(piece->x + dx, piece->x, 0.5);
    int y = lerp_pixel((int)(position >> 8) + dy, (int)(position >> 8), 0.5);
    if (x < 256 && x + size > 0 && y >= 32 && y < 224) return true;
  }
  return false;
}
