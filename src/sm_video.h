#pragma once

#include <stdbool.h>
#include <stdint.h>

enum { SM_HEIGHT = 224, SM_STOCK_WIDTH = 256, SM_MAX_WIDTH = 684 };
#define SM_SIMULATION_HZ 60.098811862

typedef enum SmAspect {
  SM_ASPECT_STOCK,
  SM_ASPECT_16_9,
  SM_ASPECT_21_9,
  SM_ASPECT_32_9,
  SM_ASPECT_FIT,
  SM_ASPECT_COUNT
} SmAspect;

typedef struct SmVideoSettings {
  bool enhanced;
  SmAspect aspect;
  unsigned fps; /* 0 = display refresh (Auto). */
  bool fps_enabled;
  bool hud_anchored; /* Anchor energy left and minimap right. */
} SmVideoSettings;

extern SmVideoSettings g_sm_video;

/* Where g_sm_video lives beside the executable. The launcher's Mods page and
 * the pause menu's Options tab both write it. */
#define SM_VIDEO_CONFIG_FILE "sm-video.ini"

typedef struct SmViewport {
  int width, extra;
  double aspect;
  bool enhanced;
} SmViewport;

typedef struct SmRect { int x, y, w, h; } SmRect;

void SmVideoDefaults(SmVideoSettings *settings);
const char *SmAspectName(SmAspect aspect);
bool SmParseAspect(const char *text, SmAspect *aspect);
bool SmValidFps(unsigned fps);
double SmPresentationHz(unsigned fps, double refresh);
SmViewport SmCalculateViewport(const SmVideoSettings *settings,
                                     int drawable_width, int drawable_height);
SmRect SmDestination(SmViewport viewport, int width, int height);
int SmHudAnchorX(SmViewport viewport, int x, int anchor);
bool SmVideoLoad(SmVideoSettings *settings, const char *path);
bool SmVideoSave(const SmVideoSettings *settings, const char *path);

/* The presentation clock itself is the framework's (snesrecomp
 * runner/src/desktop/host_clock.h); it used to be SmClock here. These two
 * validate the fps Mod option against the rates that clock supports. */
