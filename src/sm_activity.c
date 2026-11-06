#include "sm_activity.h"
#include <stddef.h>
#include <string.h>

static unsigned simulation_extra;
unsigned SmActivityExtra(void) { return simulation_extra; }
bool SmActivityExtraValid(unsigned extra) { return extra <= SM_ACTIVITY_EXTRA_MAX; }
void SmActivityRestore(unsigned extra) { simulation_extra = extra; }

static uint16_t word(const uint8_t *ram, unsigned at) {
  return (uint16_t)(ram[at] | (unsigned)ram[at + 1] << 8);
}
static void put(uint8_t *ram, unsigned at, unsigned value) {
  ram[at] = (uint8_t)value; ram[at + 1] = (uint8_t)(value >> 8);
}

void SmActivityRebaseProjectiles(uint8_t ram[0x20000], int x, int y) {
  for (unsigned i = 0; i < 10; ++i) {
    unsigned offset = i * 2;
    if (!word(ram, 0xc40 + offset) && !word(ram, 0xc18 + offset)) continue;
    put(ram, 0xb64 + offset, word(ram, 0xb64 + offset) - x);
    put(ram, 0xb78 + offset, word(ram, 0xb78 + offset) - y);
  }
  for (unsigned i = 0; i < 18; ++i) {
    unsigned offset = i * 2;
    if (word(ram, 0xd658 + offset)) {
      put(ram, 0xd730 + offset, word(ram, 0xd730 + offset) - x);
      put(ram, 0xd778 + offset, word(ram, 0xd778 + offset) - y);
    }
    if (word(ram, 0xd67c + offset)) {
      put(ram, 0xd754 + offset, word(ram, 0xd754 + offset) - x);
      put(ram, 0xd79c + offset, word(ram, 0xd79c + offset) - y);
    }
  }
}

bool SmActivityFilterQueues(uint8_t *ram,uint32_t simulation,uint32_t interaction) {
  for (unsigned list=0; list<2; ++list) {
    unsigned base=list ? 0x17ec : 0x17ac,count=0;uint32_t mask=list ? interaction : simulation;
    for (unsigned i=0; i<32; ++i) {
      unsigned index=word(ram,base+2*i);if (index==65535) break;
      if (index>=2048 || (index&63)) return false;
      if (mask&(1u<<(index/64))) { put(ram,base+2*count,index);++count; }
    }
    put(ram,base+2*count,65535);put(ram,list ? 0x17a6 : 0x17a4,count*2);
  }
  return true;
}
bool SmActivityAugmentQueues(uint8_t *ram, unsigned extra) {
  if (!SmActivityExtraValid(extra)) return false;
  /* Let the original ROM own deletion, always-active/frozen enemies, its
   * process-all path, and all CPU residue. Native geometry writes nothing. */
  if (!extra || word(ram, 0x183c)) return true;
  unsigned bytes = word(ram, 0x17a4);
  if ((bytes & 1) || bytes > 64) return false;
  bool active[32] = {false}, changed = false;
  for (unsigned i = 0; i < bytes; i += 2) {
    unsigned slot = word(ram, 0x17ac + i);
    if ((slot & 63) || slot >= 2048 || active[slot / 64]) return false;
    active[slot / 64] = true;
  }
  const unsigned camera_x = word(ram, 0x911), camera_y = word(ram, 0x915);
  for (unsigned i = 0; i < 32; ++i) {
    unsigned at = 0xf78 + i * 64, id = word(ram, at);
    unsigned properties = word(ram, at + 14);
    if (active[i] || !id || id == 0xdaff || (properties & 0x200)) continue;
    unsigned x = word(ram, at + 2), y = word(ram, at + 6), radius = word(ram, at + 10);
    /* Match the ROM's signed 16-bit comparisons, extending only X. */
    if ((int16_t)(radius + x - camera_x + extra) >= 0 &&
        (int16_t)(radius + camera_x + 256 + extra - x) >= 0 &&
        (int16_t)(y + 8 - camera_y) >= 0 &&
        (int16_t)(camera_y + 248 - y) >= 0) {
      active[i] = true; changed = true;
    }
  }
  if (!changed) return true;
  unsigned count = 0, interacting = 0;
  for (unsigned i = 0; i < 32; ++i) if (active[i]) {
    ++count; interacting += !(word(ram, 0xf78 + i * 64 + 14) & 0x400);
  }
  /* The two native lists are 32 words including their terminator. Refuse an
   * impossible population rather than overwrite the adjacent queue. */
  /* All 32 slots fit if none interact: the active terminator and the empty
   * interactive list share the same $17EC word. CA08's 32 sparks use this. */
  if (count > 31 && interacting) return false;
  unsigned processed = 0, interactive = 0;
  for (unsigned i = 0; i < 32; ++i) if (active[i]) {
    unsigned slot = i * 64;
    put(ram, 0x17ac + processed, slot); processed += 2;
    if (!(word(ram, 0xf78 + slot + 14) & 0x400)) {
      put(ram, 0x17ec + interactive, slot); interactive += 2;
    }
  }
  put(ram, 0x17a4, processed); put(ram, 0x17ac + processed, 0xffff);
  put(ram, 0x17a6, interactive); put(ram, 0x17ec + interactive, 0xffff);
  return true;
}
