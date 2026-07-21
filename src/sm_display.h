#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

bool SmDisplay_IsWidescreenActive(void);
int SmDisplay_GetCurrentFrameWidth(void);

typedef struct SmDisplayViewport {
  int x, y, width, height;
} SmDisplayViewport;

int SmDisplay_ComputeFrameWidth(int drawable_width, int drawable_height,
                                bool widescreen);
void SmDisplay_ComputePresentationSize(int frame_width, int frame_height,
                                       int *width, int *height);
void SmDisplay_ComputeViewport(int source_width, int source_height,
                               int drawable_width, int drawable_height,
                               bool ignore_aspect, bool integer_scale,
                               SmDisplayViewport *viewport);
int SmDisplay_GetWindowBaseWidth(int frame_width);
int SmDisplay_GetWindowBaseHeight(void);

#ifdef __cplusplus
}
#endif
