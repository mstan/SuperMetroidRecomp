#pragma once
#include "sm_room_scene.h"
#include <stdbool.h>
void SmRoomsSetRom(const uint8_t *rom, size_t bytes);
void SmRoomsReset(void);
typedef bool (*SmRoomDoorEvent)(unsigned x, unsigned y, unsigned orientation, bool closing);
void SmRoomsSetDoorEvent(SmRoomDoorEvent event);
bool SmRoomsFrame(uint8_t active[0x20000], bool enabled, unsigned extra,
                    bool requested_logic, bool requested_seamless);
const SmRoomScene *SmRoomsScene(void);
bool SmRoomsDoor(uint16_t definition, SmRoomPlacement *destination);
unsigned SmRoomsDoorPhase(uint16_t definition);
/* Native collision keeps its own slope/solid responses. These queries only
 * replace samples belonging to an adjoining room at a planar doorway. */
int SmRoomsCollisionBlock(const uint8_t active[0x20000], int block_x, int block_y);
bool SmRoomsUpwardDoorCarry(const uint8_t active[0x20000]);
bool SmRoomsCrossing(const uint8_t active[0x20000], uint16_t *definition, SmRoomPlacement *destination);
bool SmRoomsHandoff(const uint8_t previous[0x20000], const uint16_t previous_vram[0x8000],
    uint8_t active[0x20000], uint16_t vram[0x8000], const SmRoomPlacement *destination, unsigned direction);
size_t SmRoomsStateBound(void);
size_t SmRoomsSave(void *out, size_t capacity);
bool SmRoomsValidate(const void *in, size_t size);
bool SmRoomsLoad(const void *in, size_t size);
