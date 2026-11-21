#include "sm_mods.h"
#include "sm_activity.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Same built-in provider contract as F-Zero; no ROM patch or guest write. */
static SmVideoSettings *video;
static const char *path;
static char error[128];
enum { WIDESCREEN, PRESENTATION_FPS, CONTINUOUS_ROOMS, REPLAY_RECORDING, BUILTIN_COUNT };
static const char *const packages[] = {"sm-widescreen", "sm-presentation-fps", "sm-continuous-rooms", "sm-diagnostics"};
static const char *const features[] = {"widescreen", "presentation-fps", "continuous-rooms", "replay-recording"};
static const char *const names[] = {"Widescreen", "Higher FPS", "Continuous Rooms (Experimental)", "Gameplay Diagnostics"};
static const char *const versions[] = {"2", "2", "1", "1"};
static const int option_counts[] = {2, 1, 3, 0};
static const char *const descriptions[] = {
  "Render a wider view of the current room with optional HUD anchoring.",
  "Smooth object and camera motion at your display rate while keeping game timing unchanged.",
  "Experimental: show adjoining rooms, scenery and optional gameplay across doors. Some scripted encounters and effects may have issues. Works at any aspect ratio.",
  "Record gameplay inputs and replay checkpoints locally so you can reproduce a gameplay issue."
};
static const char *const aspects[] = {"Fit", "16:9", "21:9", "32:9"};
static const char *const rates[] = {"Auto", "60", "90", "120", "144", "165", "240", "360"};
static const SmDoorPresentation door_choices[] = {
  SM_DOOR_PRESENT_LIGHTING, SM_DOOR_PRESENT_SHADOW, SM_DOOR_PRESENT_REFERENCE,
};
#define COPY(field, text) snprintf(field, sizeof(field), "%s", text)

static int identity(const char *package, const char *feature) {
  if (package && feature) for (int i = 0; i < BUILTIN_COUNT; ++i)
    if (!strcmp(package, packages[i]) && !strcmp(feature, features[i])) return i;
  return -1;
}
static int count(void *ctx) { (void)ctx; return BUILTIN_COUNT; }
static int enabled(int index) {
  switch (index) {
    case CONTINUOUS_ROOMS: return video->connected_rooms;
    case PRESENTATION_FPS: return video->fps_enabled;
    case REPLAY_RECORDING: return video->replay_recording;
    default: return video->enhanced;
  }
}
static int package_get(void *ctx, int i, RecompLauncherCModPackage *out) {
  (void)ctx;
  if (!out || i < 0 || i >= BUILTIN_COUNT) return 0;
  memset(out, 0, sizeof(*out));
  COPY(out->id, packages[i]); COPY(out->name, names[i]); COPY(out->version, versions[i]);
  COPY(out->author, "SuperMetroidRecomp contributors");
  COPY(out->source_name, "SuperMetroidRecomp built-in mods");
  COPY(out->source_url, "https://github.com/mstan/SuperMetroidRecomp");
  COPY(out->description, descriptions[i]); out->enabled = enabled(i);
  return 1;
}
static int feature_get(void *ctx, int i, RecompLauncherCModFeature *out) {
  (void)ctx;
  if (!out || i < 0 || i >= BUILTIN_COUNT) return 0;
  memset(out, 0, sizeof(*out));
  COPY(out->id, features[i]); COPY(out->package_id, packages[i]);
  COPY(out->package_name, names[i]); COPY(out->package_version, versions[i]);
  COPY(out->name, names[i]);
  COPY(out->group, i == REPLAY_RECORDING ? "Diagnostics" : i == CONTINUOUS_ROOMS ? "Gameplay" : "Presentation");
  COPY(out->author, "SuperMetroidRecomp contributors");
  COPY(out->source_name, "SuperMetroidRecomp built-in mods");
  COPY(out->source_url, "https://github.com/mstan/SuperMetroidRecomp");
  COPY(out->description, descriptions[i]);
  out->enabled = enabled(i); out->option_count = option_counts[i];
  COPY(out->status, out->enabled ? "Enabled" : "Disabled");
  return 1;
}
static int option_get(void *ctx, const char *package, const char *feature,
                      int index, RecompLauncherCModOption *out) {
  (void)ctx;
  int kind = identity(package, feature);
  if (!out || kind < 0 || index < 0 || index >= option_counts[kind]) return 0;
  memset(out, 0, sizeof(*out)); out->type = RECOMP_MOD_OPTION_CHOICE; out->step = 1;
  if (kind == 1) {
    COPY(out->id, "fps"); COPY(out->label, "Presentation FPS");
    COPY(out->description, "Auto follows display refresh, up to 360 FPS.");
    if (video->fps) snprintf(out->value, sizeof(out->value), "%u", video->fps);
    else COPY(out->value, "Auto");
    COPY(out->default_value, "Auto"); out->choice_count = 8;
  } else if (kind == 0 && index == 0) {
    COPY(out->id, "aspect"); COPY(out->label, "Aspect ratio");
    COPY(out->description, "Fit adapts to the window between 4:3 and 32:9.");
    COPY(out->value, SmAspectName(video->aspect));
    COPY(out->default_value, "Fit"); out->choice_count = 4;
  } else if (kind == 0) {
    COPY(out->id, "hud"); COPY(out->label, "HUD anchoring");
    COPY(out->description, "Energy at the left edge, minimap at the right, weapons centered.");
    COPY(out->value, video->hud_anchored ? "Edges" : "Center");
    COPY(out->default_value, "Edges"); out->choice_count = 2;
  } else if (index == 0) {
    COPY(out->id,"neighbor-logic"); COPY(out->label,"Neighboring room logic");
    COPY(out->description,"Run enemies and gameplay actors in neighboring rooms. Off keeps animated scenery and doors. Applies when a room loads.");
    COPY(out->value,video->neighboring_logic ? "On" : "Off");
    COPY(out->default_value,"On"); out->choice_count=2;
  } else if (index == 1) {
    COPY(out->id,"transitions"); COPY(out->label,"Room transitions");
    COPY(out->description,"Continuous keeps the world and camera connected across doors. Native uses the original transition animation.");
    COPY(out->value,video->seamless ? "Continuous" : "Native");
    COPY(out->default_value,"Continuous"); out->choice_count=2;
  } else {
    COPY(out->id, "door-fade"); COPY(out->label, "Doorway fade");
    COPY(out->description, "Choose the visual effect for seamless doorway crossings.");
    COPY(out->value, SmDoorPresentationKey(video->door_presentation));
    COPY(out->default_value, SmDoorPresentationKey(SM_DOOR_PRESENT_DEFAULT));
    out->choice_count = SM_DOOR_PRESENT_COUNT;
  }
  return 1;
}
static const char *choice(int kind, const char *option, int index) {
  if (!option || index < 0) return NULL;
  if (kind == 0 && !strcmp(option, "aspect") && index < 4) return aspects[index];
  if (kind == 0 && !strcmp(option, "hud") && index < 2) return index ? "Center" : "Edges";
  if (kind == 2 && !strcmp(option,"neighbor-logic") && index<2) return index ? "Off" : "On";
  if (kind == 2 && !strcmp(option,"transitions") && index<2) return index ? "Native" : "Continuous";
  if (kind == CONTINUOUS_ROOMS && !strcmp(option, "door-fade") && index < SM_DOOR_PRESENT_COUNT)
    return SmDoorPresentationKey(door_choices[index]);
  if (kind == 1 && !strcmp(option, "fps") && index < 8) return rates[index];
  return NULL;
}
static int choice_get(void *ctx, const char *package, const char *feature,
                      const char *option, int index, RecompLauncherCModChoice *out) {
  (void)ctx;
  const int kind = identity(package, feature);
  const char *value = choice(kind, option, index);
  if (!out || !value) return 0;
  memset(out, 0, sizeof(*out)); COPY(out->value, value);
  const char *label = !strcmp(value, "Fit") ? "Fit to window" : value;
  if (kind == CONTINUOUS_ROOMS && !strcmp(option, "door-fade"))
    label = SmDoorPresentationLabel(door_choices[index]);
  COPY(out->label, label);
  return 1;
}
static int enable(void *ctx, const char *package, const char *feature, int on) {
  (void)ctx;
  int kind = identity(package, feature);
  if (kind < 0) return 0;
  if ((kind == WIDESCREEN || kind == CONTINUOUS_ROOMS) && !SmActivityGeometryMutable()) {
    COPY(error, "The initial host view stays fixed during this netplay match."); return 0;
  }
  if (kind == CONTINUOUS_ROOMS) video->connected_rooms = on != 0;
  else if (kind == PRESENTATION_FPS) video->fps_enabled = on != 0;
  else if (kind == REPLAY_RECORDING) video->replay_recording = on != 0;
  else video->enhanced = on != 0;
  return 1;
}
static int set_option(void *ctx, const char *package, const char *feature,
                      const char *option, const char *value) {
  (void)ctx;
  int kind = identity(package, feature);
  if (kind < 0 || !value) return 0;
  if (kind == CONTINUOUS_ROOMS && option && !strcmp(option, "door-fade"))
    return SmParseDoorPresentation(value, &video->door_presentation);
  if (kind != 1 && option && strcmp(option,"hud") && !SmActivityGeometryMutable()) {
    COPY(error, "The initial host view stays fixed during this netplay match."); return 0;
  }
  for (int i = 0; i < 8; ++i) {
    const char *candidate = choice(kind, option, i);
    if (!candidate || strcmp(candidate, value)) continue;
    if (kind == 1) video->fps = i ? (unsigned)atoi(value) : 0;
    else if (!strcmp(option, "hud")) video->hud_anchored = i == 0;
    else if (!strcmp(option,"neighbor-logic")) video->neighboring_logic=i==0;
    else if (!strcmp(option,"transitions")) video->seamless=i==0;
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
