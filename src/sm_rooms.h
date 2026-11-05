#pragma once
#include "sm_room_scene.h"
#include <stdbool.h>
void SmRoomsSetRom(const uint8_t *rom, size_t bytes);
void SmRoomsReset(void);
bool SmRoomsFrame(const uint8_t active[0x20000], bool enabled, unsigned extra,
                    bool requested_logic, bool requested_seamless);
const SmRoomScene *SmRoomsScene(void);
size_t SmRoomsStateBound(void);
size_t SmRoomsSave(void *out, size_t capacity);
bool SmRoomsValidate(const void *in, size_t size);
bool SmRoomsLoad(const void *in, size_t size);
