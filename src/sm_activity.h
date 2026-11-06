#pragma once
#include <stdbool.h>
#include <stdint.h>

/* Simulation geometry, distinct from the read-only renderer. */
enum { SM_ACTIVITY_EXTRA_MAX = 214, SM_ACTIVITY_PROFILE = 2 };
unsigned SmActivityExtra(void);
/* Player-owned native slots and linked missile indices survive a continuous
 * room handoff; only their room-relative positions and emitted trails move. */
void SmActivityRebaseProjectiles(uint8_t ram[0x20000], int x, int y);
bool SmActivityExtraValid(unsigned extra);
void SmActivityRestore(unsigned extra);
bool SmActivityAugmentQueues(uint8_t *ram, unsigned extra);
bool SmActivityFilterQueues(uint8_t *ram,uint32_t simulation,uint32_t interaction);
/* Shared horizontal part of native AD70/ADE7. Their original vertical
 * branches still decide the result, including their distinct Y padding. */
bool SmActivityEnemyHorizontalVisible(const uint8_t *ram,unsigned index,unsigned extra,bool normal);

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
