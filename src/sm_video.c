/* Presentation geometry and cadence adapted from FZeroRecomp's custom renderer. */
#include "sm_video.h"
#include "host_clock.h"

#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#endif

static const char *const aspect_names[] = {"4:3", "16:9", "21:9", "32:9", "Fit"};

SmVideoSettings g_sm_video;

void SmVideoDefaults(SmVideoSettings *s) {
  *s = (SmVideoSettings){.enhanced = true, .aspect = SM_ASPECT_FIT, .hud_anchored = true};
}

const char *SmAspectName(SmAspect aspect) {
  return aspect >= 0 && aspect < SM_ASPECT_COUNT ? aspect_names[aspect] : "4:3";
}

bool SmParseAspect(const char *text, SmAspect *aspect) {
  for (int i = 0; i < SM_ASPECT_COUNT; ++i) {
    if (strcmp(text, aspect_names[i]) == 0) {
      *aspect = (SmAspect)i;
      return true;
    }
  }
  return false;
}

bool SmValidFps(unsigned fps) { return snes_host_valid_fps(fps); }

double SmPresentationHz(unsigned fps, double refresh) {
  return snes_host_presentation_hz(fps, refresh);
}

SmViewport SmCalculateViewport(const SmVideoSettings *s, int w, int h) {
  double aspect = 4.0 / 3.0;
  if (s->enhanced) {
    switch (s->aspect) {
    case SM_ASPECT_16_9: aspect = 16.0 / 9.0; break;
    case SM_ASPECT_21_9: aspect = 21.0 / 9.0; break;
    case SM_ASPECT_32_9: aspect = 32.0 / 9.0; break;
    case SM_ASPECT_FIT:
      if (w > 0 && h > 0) aspect = fmax(4.0 / 3.0, fmin(32.0 / 9.0, (double)w / h));
      break;
    default: break;
    }
  }
  /* Stock pixels have display aspect 7:6. Keep that scale at every width;
   * round to an even width so the stock center remains exactly centered. */
  int width = 2 * (int)floor((256.0 * aspect / (4.0 / 3.0)) / 2.0 + 0.5);
  if (width < 256) width = 256;
  if (width > SM_MAX_WIDTH) width = SM_MAX_WIDTH;
  return (SmViewport){width, (width - 256) / 2, aspect, width > 256};
}

SmRect SmDestination(SmViewport viewport, int width, int height) {
  if (width <= 0 || height <= 0) return (SmRect){0, 0, 0, 0};
  int w = width, h = (int)floor(width / viewport.aspect + 0.5);
  if (h > height) { h = height; w = (int)floor(height * viewport.aspect + 0.5); }
  return (SmRect){(width - w) / 2, (height - h) / 2, w, h};
}

int SmHudAnchorX(SmViewport viewport, int x, int anchor) {
  return x + (anchor < 0 ? 0 : anchor > 0 ? 2 * viewport.extra : viewport.extra);
}

bool SmVideoLoad(SmVideoSettings *s, const char *path) {
  SmVideoDefaults(s);
  FILE *f = fopen(path, "r");
  if (!f) return errno == ENOENT;
  char line[256], key[64], value[64], tail;
  bool valid = true;
  while (fgets(line, sizeof(line), f)) {
    if (sscanf(line, " %63[^= \t] = %63s", key, value) != 2) continue;
    if (!strcmp(key, "EnhancedRenderer")) {
      if (!strcmp(value, "0") || !strcmp(value, "1")) s->enhanced = value[0] == '1';
      else valid = false;
    } else if (!strcmp(key, "PresentationEnabled")) {
      if (!strcmp(value, "0") || !strcmp(value, "1")) s->fps_enabled = value[0] == '1';
      else valid = false;
    } else if (!strcmp(key, "HudAnchored")) {
      if (!strcmp(value, "0") || !strcmp(value, "1")) s->hud_anchored = value[0] == '1';
      else valid = false;
    } else if (!strcmp(key, "Aspect")) {
      if (!SmParseAspect(value, &s->aspect)) valid = false;
    } else if (!strcmp(key, "PresentationFPS")) {
      unsigned fps;
      if (!strcmp(value, "Auto")) s->fps = 0;
      else if (sscanf(value, "%u%c", &fps, &tail) == 1 && SmValidFps(fps)) s->fps = fps;
      else valid = false;
    }
  }
  if (ferror(f)) valid = false;
  fclose(f);
  return valid;
}

bool SmVideoSave(const SmVideoSettings *s, const char *path) {
  char temporary[1024];
  if (snprintf(temporary, sizeof(temporary), "%s.tmp", path) >= (int)sizeof(temporary)) return false;
  FILE *f = fopen(temporary, "w");
  if (!f) return false;
  bool ok = fprintf(f, "[SuperMetroidVideo]\nEnhancedRenderer=%d\nAspect=%s\nPresentationEnabled=%d\nPresentationFPS=%u\nHudAnchored=%d\n",
                    s->enhanced, SmAspectName(s->aspect), s->fps_enabled, s->fps, s->hud_anchored) > 0;
  if (fclose(f)) ok = false;
  if (ok) {
#ifdef _WIN32
    ok = MoveFileExA(temporary, path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
    ok = rename(temporary, path) == 0;
#endif
  }
  if (!ok) remove(temporary);
  return ok;
}
