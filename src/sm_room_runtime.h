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
bool SmRoomRuntimeStepVisible(SmRoomRuntime *room, int samus_x, int samus_y,
    int camera_x, int camera_y, unsigned extra);
void SmRoomRuntimeBackground(SmRoomRuntime *room, const uint8_t vram[65536]);
const uint8_t *SmRoomRuntimeRam(const SmRoomRuntime *room);
const uint8_t *SmRoomRuntimeVram(const SmRoomRuntime *room);
const char *SmRoomRuntimeError(const SmRoomRuntime *room);
size_t SmRoomRuntimeStateSize(void);
bool SmRoomRuntimeSave(const SmRoomRuntime *room, void *out, size_t size);
bool SmRoomRuntimeLoad(SmRoomRuntime *room, const void *in, size_t size);
bool SmRoomRuntimeValidate(const void *in, size_t size, uint16_t room, uint16_t state, bool actors);
