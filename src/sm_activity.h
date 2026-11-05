#pragma once
#include <stdbool.h>
#include <stdint.h>

/* Simulation geometry, distinct from the read-only renderer. */
enum { SM_ACTIVITY_EXTRA_MAX = 214, SM_ACTIVITY_PROFILE = 2 };
unsigned SmActivityExtra(void);
bool SmActivityExtraValid(unsigned extra);
void SmActivityRestore(unsigned extra);
bool SmActivityAugmentQueues(uint8_t *ram, unsigned extra);

#if defined(SNES_HAS_LOBBY_CLIENT) && defined(RECOMP_LAUNCHER)
#include "netplay/snes_netplay.h"
static inline bool SmActivityGeometryMutable(void) { return !snes_netplay_active(); }
#else
static inline bool SmActivityGeometryMutable(void) { return true; }
#endif

void SmActivityInitialize(void);
bool SmActivityFrameBegin(void);
struct SmViewport;
void SmActivityViewport(struct SmViewport *view);
bool SmActivityNetworkWide(void);
