#pragma once
#include "mods/sm_world.h"
typedef struct SmRoomRuntime SmRoomRuntime;
/* Independent cartridge execution for resident rooms. The active machine's
 * bus, timing, audio, globals and native saves are never installed here. */
SmRoomRuntime *SmRoomRuntimeCreate(SmWorldRead read, void *context,
    const SmWorldRoom *room, uint16_t state, const uint8_t active[0x20000]);
SmRoomRuntime *SmRoomRuntimeCreateConfigured(SmWorldRead read, void *context,
    const SmWorldRoom *room, uint16_t state, uint16_t incoming,
    const uint8_t active[0x20000], bool actors);
void SmRoomRuntimeDestroy(SmRoomRuntime *room);
bool SmRoomRuntimeStep(SmRoomRuntime *room, int samus_x, int samus_y,
                        int camera_x, int camera_y);
bool SmRoomRuntimeStepEffects(SmRoomRuntime *room);
/* Door PLMs run independently of ordinary neighboring gameplay actors. */
bool SmRoomRuntimeStepDoors(SmRoomRuntime *room);
bool SmRoomRuntimeDoorShot(SmRoomRuntime *room, unsigned block_x, unsigned block_y, uint16_t projectile_type);
bool SmRoomRuntimeDoorOpen(SmRoomRuntime *room, unsigned block_x, unsigned block_y, unsigned orientation);
bool SmRoomRuntimeDoorClose(SmRoomRuntime *room, unsigned block_x, unsigned block_y, unsigned orientation);
void SmRoomRuntimeProgress(SmRoomRuntime *room, const uint8_t active[0x20000]);
void SmRoomRuntimeDoorBits(const SmRoomRuntime *room, uint8_t active[0x20000]);
bool SmRoomRuntimeStepVisible(SmRoomRuntime *room, int samus_x, int samus_y,
    int camera_x, int camera_y, unsigned extra);
void SmRoomRuntimeBackground(SmRoomRuntime *room, const uint8_t vram[65536]);
bool SmRoomRuntimeCapture(SmRoomRuntime *room, const uint8_t ram[0x20000], const uint16_t vram[0x8000]);
void SmRoomRuntimePromote(const SmRoomRuntime *room, uint8_t ram[0x20000], uint16_t vram[0x8000]);
bool SmRoomRuntimePromoteDoors(const SmRoomRuntime *room, uint8_t ram[0x20000]);
const uint8_t *SmRoomRuntimeRam(const SmRoomRuntime *room);
const uint8_t *SmRoomRuntimeVram(const SmRoomRuntime *room);
const char *SmRoomRuntimeError(const SmRoomRuntime *room);
size_t SmRoomRuntimeStateSize(void);
bool SmRoomRuntimeSave(const SmRoomRuntime *room, void *out, size_t size);
bool SmRoomRuntimeLoad(SmRoomRuntime *room, const void *in, size_t size);
bool SmRoomRuntimeValidate(const void *in, size_t size, uint16_t room, uint16_t state, bool actors);
