#include "sm_mods.h"
#include "sm_activity.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Same built-in provider contract as F-Zero; no ROM patch or guest write. */
static SmVideoSettings *video;
static const char *path;
static char error[128];
static const char *const packages[] = {"sm-widescreen", "sm-presentation-fps"};
static const char *const features[] = {"widescreen", "presentation-fps"};
static const char *const names[] = {"Widescreen", "Presentation FPS"};
static const char *const descriptions[] = {
  "Render a wider room view and keep enemies active throughout it, with optional HUD anchoring.",
  "Interpolate rendered motion at your display rate while keeping game timing unchanged."
};
static const char *const aspects[] = {"Fit", "16:9", "21:9", "32:9"};
static const char *const rates[] = {"Auto", "60", "90", "120", "144", "165", "240", "360"};
#define COPY(field, text) snprintf(field, sizeof(field), "%s", text)

static int identity(const char *package, const char *feature) {
  if (package && feature) for (int i = 0; i < 2; ++i)
    if (!strcmp(package, packages[i]) && !strcmp(feature, features[i])) return i;
  return -1;
}
static int count(void *ctx) { (void)ctx; return 2; }
static int enabled(int index) { return index ? video->fps_enabled : video->enhanced; }
static int package_get(void *ctx, int i, RecompLauncherCModPackage *out) {
  (void)ctx;
  if (!out || i < 0 || i >= 2) return 0;
  memset(out, 0, sizeof(*out));
  COPY(out->id, packages[i]); COPY(out->name, names[i]); COPY(out->version, "1");
  COPY(out->author, "SuperMetroidRecomp contributors");
  COPY(out->source_name, "SuperMetroidRecomp presentation features");
  COPY(out->source_url, "https://github.com/mstan/SuperMetroidRecomp");
  COPY(out->description, descriptions[i]); out->enabled = enabled(i);
  return 1;
}
static int feature_get(void *ctx, int i, RecompLauncherCModFeature *out) {
  (void)ctx;
  if (!out || i < 0 || i >= 2) return 0;
  memset(out, 0, sizeof(*out));
  COPY(out->id, features[i]); COPY(out->package_id, packages[i]);
  COPY(out->package_name, names[i]); COPY(out->package_version, "1");
  COPY(out->name, names[i]); COPY(out->group, "Presentation");
  COPY(out->author, "SuperMetroidRecomp contributors");
  COPY(out->source_name, "SuperMetroidRecomp presentation features");
  COPY(out->source_url, "https://github.com/mstan/SuperMetroidRecomp");
  COPY(out->description, descriptions[i]);
  out->enabled = enabled(i); out->option_count = i ? 1 : 2;
  COPY(out->status, out->enabled ? "Enabled" : "Disabled");
  return 1;
}
static int option_get(void *ctx, const char *package, const char *feature,
                      int index, RecompLauncherCModOption *out) {
  (void)ctx;
  int kind = identity(package, feature);
  if (!out || kind < 0 || index < 0 || index >= (kind ? 1 : 2)) return 0;
  memset(out, 0, sizeof(*out)); out->type = RECOMP_MOD_OPTION_CHOICE; out->step = 1;
  if (kind == 1) {
    COPY(out->id, "fps"); COPY(out->label, "Presentation FPS");
    COPY(out->description, "Auto follows display refresh, up to 360 FPS.");
    if (video->fps) snprintf(out->value, sizeof(out->value), "%u", video->fps);
    else COPY(out->value, "Auto");
    COPY(out->default_value, "Auto"); out->choice_count = 8;
  } else if (index == 0) {
    COPY(out->id, "aspect"); COPY(out->label, "Aspect ratio");
    COPY(out->description, "Fit adapts to the window between 4:3 and 32:9.");
    COPY(out->value, SmAspectName(video->aspect));
    COPY(out->default_value, "Fit"); out->choice_count = 4;
  } else {
    COPY(out->id, "hud"); COPY(out->label, "HUD anchoring");
    COPY(out->description, "Energy at the left edge, minimap at the right, weapons centered.");
    COPY(out->value, video->hud_anchored ? "Edges" : "Center");
    COPY(out->default_value, "Edges"); out->choice_count = 2;
  }
  return 1;
}
static const char *choice(int kind, const char *option, int index) {
  if (!option || index < 0) return NULL;
  if (kind == 0 && !strcmp(option, "aspect") && index < 4) return aspects[index];
  if (kind == 0 && !strcmp(option, "hud") && index < 2) return index ? "Center" : "Edges";
  if (kind == 1 && !strcmp(option, "fps") && index < 8) return rates[index];
  return NULL;
}
static int choice_get(void *ctx, const char *package, const char *feature,
                      const char *option, int index, RecompLauncherCModChoice *out) {
  (void)ctx;
  const char *value = choice(identity(package, feature), option, index);
  if (!out || !value) return 0;
  memset(out, 0, sizeof(*out)); COPY(out->value, value);
  COPY(out->label, !strcmp(value, "Fit") ? "Fit to window" : value);
  return 1;
}
static int enable(void *ctx, const char *package, const char *feature, int on) {
  (void)ctx;
  int kind = identity(package, feature);
  if (kind < 0) return 0;
  if (!kind && !SmActivityGeometryMutable()) {
    COPY(error, "The initial host view stays fixed during this netplay match."); return 0;
  }
  if (kind) video->fps_enabled = on != 0;
  else video->enhanced = on != 0;
  return 1;
}
static int set_option(void *ctx, const char *package, const char *feature,
                      const char *option, const char *value) {
  (void)ctx;
  int kind = identity(package, feature);
  if (kind < 0 || !value) return 0;
  if (!kind && option && !strcmp(option, "aspect") && !SmActivityGeometryMutable()) {
    COPY(error, "The initial host view stays fixed during this netplay match."); return 0;
  }
  for (int i = 0; i < 8; ++i) {
    const char *candidate = choice(kind, option, i);
    if (!candidate || strcmp(candidate, value)) continue;
    if (kind) video->fps = i ? (unsigned)atoi(value) : 0;
    else if (!strcmp(option, "hud")) video->hud_anchored = i == 0;
    else return SmParseAspect(value, &video->aspect);
    return 1;
  }
  return 0;
}
static int commit(void *ctx, const char *image) {
  (void)ctx; (void)image; error[0] = 0;
  if (SmVideoSave(video, path)) return 1;
  COPY(error, "Unable to save sm-video.ini"); return 0;
}
static const char *last_error(void *ctx) { (void)ctx; return error; }

const RecompLauncherCModProvider *SmModsProvider(SmVideoSettings *settings,
                                               const char *config_path) {
  static RecompLauncherCModProvider provider;
  video = settings; path = config_path; error[0] = 0;
  memset(&provider, 0, sizeof(provider));
  provider.package_count = count; provider.package_get = package_get;
  provider.feature_count = count; provider.feature_get = feature_get;
  provider.feature_option_get = option_get; provider.feature_choice_get = choice_get;
  provider.feature_enable = enable; provider.feature_set_option = set_option;
  provider.commit = commit; provider.last_error = last_error;
  provider.archive_extension = ".snesmod";
  provider.archive_description = "SNESRecomp mod package (.snesmod)";
  return &provider;
}
