#pragma once
#include "sm_room_scene.h"
#include "sm_eproj_view.h"
#include "sm_enemy_view.h"
#include <stdbool.h>
void SmRoomsSetRom(const uint8_t *rom, size_t bytes);
void SmRoomsReset(void);
typedef bool (*SmRoomDoorEvent)(unsigned x, unsigned y, unsigned orientation, bool closing);
void SmRoomsSetDoorEvent(SmRoomDoorEvent event);
typedef int (*SmRoomEnemyReaction)(uint8_t source[0x20000],int offset_x,int offset_y,
    int block_x,int block_y,bool horizontal);
void SmRoomsSetEnemyReaction(SmRoomEnemyReaction reaction);
int SmRoomsEnemyBlock(uint8_t active[0x20000],uint8_t source[0x20000],int origin_x,int origin_y,
    bool horizontal,uint16_t *index);
bool SmRoomsFrame(uint8_t active[0x20000], bool enabled, unsigned extra,
                    bool requested_logic, bool requested_seamless);
const SmRoomScene *SmRoomsScene(void);
bool SmRoomsDoor(uint16_t definition, SmRoomPlacement *destination);
unsigned SmRoomsDoorPhase(uint16_t definition);
/* Native collision keeps its own slope/solid responses. These queries only
 * replace samples belonging to an adjoining room at a planar doorway. */
int SmRoomsCollisionBlock(const uint8_t active[0x20000], int block_x, int block_y);
bool SmRoomsShotsActive(const uint8_t active[0x20000]);
bool SmRoomsShotDoorAir(const uint8_t ram[0x20000],int origin_x,int origin_y,unsigned block);
/* -1 retains the active native response (with a corrected byte index).
 * 0/1 is an adjoining-room native reaction; -2 is an execution failure. */
int SmRoomsShotBlock(uint8_t active[0x20000],bool horizontal,uint16_t *index);
bool SmRoomsEprojTile(const uint8_t active[0x20000],const uint8_t source[0x20000],
    int origin_x,int origin_y,int block_x,int block_y,SmEprojTile *tile);
void SmRoomsEntityBorn(const uint8_t active[0x20000],const uint8_t source[0x20000],
    int origin_x,int origin_y,unsigned slot,unsigned phase);
enum { SM_ROOM_SIMULATES=1,SM_ROOM_INTERACTS=2 };
unsigned SmRoomsActiveRules(uint8_t active[0x20000],unsigned slot);
bool SmRoomsFilterActiveQueues(uint8_t active[0x20000]);
bool SmRoomsUpwardDoorCarry(const uint8_t active[0x20000]);
bool SmRoomsCrossing(const uint8_t active[0x20000], uint16_t *definition, SmRoomPlacement *destination);
bool SmRoomsHandoff(const uint8_t previous[0x20000], const uint16_t previous_vram[0x8000],
    uint8_t active[0x20000], uint16_t vram[0x8000], const SmRoomPlacement *destination, unsigned direction);
size_t SmRoomsStateBound(void);
size_t SmRoomsSave(void *out, size_t capacity);
bool SmRoomsValidate(const void *in, size_t size);
bool SmRoomsLoad(const void *in, size_t size);
