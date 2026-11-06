#pragma once
#include "mods/sm_world.h"
#include "sm_eproj_view.h"
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
/* Ephemeral world terrain view; callbacks and in-flight native loans are not
 * part of a between-frame snapshot. Clear after the bounded native tick. */
void SmRoomRuntimeSetWorld(SmRoomRuntime *room,SmEprojQuery query,void *context);
void SmRoomRuntimeSetBirth(SmRoomRuntime *room,SmWorldBirth birth);
void SmRoomRuntimeSetMasks(SmRoomRuntime *room,uint32_t simulation_enemies,uint32_t simulation_projectiles,
    uint32_t interaction_enemies,uint32_t interaction_projectiles);
bool SmRoomRuntimeActors(const SmRoomRuntime *room);
bool SmRoomRuntimeEnableActors(SmRoomRuntime *room);
bool SmRoomRuntimeStepEffects(SmRoomRuntime *room);
/* Door PLMs run independently of ordinary neighboring gameplay actors. */
bool SmRoomRuntimeStepDoors(SmRoomRuntime *room);
bool SmRoomRuntimeDoorShot(SmRoomRuntime *room, unsigned block_x, unsigned block_y, uint16_t projectile_type);
bool SmRoomRuntimeDoorOpen(SmRoomRuntime *room, unsigned block_x, unsigned block_y, unsigned orientation);
bool SmRoomRuntimeDoorClose(SmRoomRuntime *room, unsigned block_x, unsigned block_y, unsigned orientation);
typedef bool (*SmRoomShotAir)(void *context,const uint8_t ram[0x20000],unsigned block);
/* Native block response only. Movement/slot ownership stays with the player. */
int SmRoomRuntimeShot(SmRoomRuntime *room,uint8_t active[0x20000],int origin_x,int origin_y,
    int block_x,int block_y,bool horizontal,SmRoomShotAir air,void *context);
/* The world selects eligible weapon slots. Native hitboxes/vulnerabilities,
 * shot AI, death/drop state and weapon reactions remain cartridge-owned. */
bool SmRoomRuntimeHitShots(SmRoomRuntime *room,uint8_t active[0x20000],
    int origin_x,int origin_y,unsigned mask);
/* Native actor contact with a borrowed player view. Only player effects cross
 * back; room progression, CPU, camera and script state remain isolated. */
bool SmRoomRuntimeTouch(SmRoomRuntime *room,uint8_t active[0x20000],int origin_x,int origin_y);
/* Call after native enemy-projectile movement. The world supplies eligible
 * player shots/body; native damage, suits, piercing and projectile reactions
 * retain their own rules and resource-owner slots. */
bool SmRoomRuntimeProjectiles(SmRoomRuntime *room,uint8_t active[0x20000],int origin_x,int origin_y,
    unsigned shot_mask,bool body);
void SmRoomRuntimeProgress(SmRoomRuntime *room, const uint8_t active[0x20000]);
void SmRoomRuntimeDoorBits(const SmRoomRuntime *room, uint8_t active[0x20000]);
bool SmRoomRuntimeStepVisible(SmRoomRuntime *room, int samus_x, int samus_y,
    int camera_x, int camera_y, unsigned extra);
void SmRoomRuntimeBackground(SmRoomRuntime *room, const uint8_t vram[65536]);
bool SmRoomRuntimeCapture(SmRoomRuntime *room, const uint8_t ram[0x20000], const uint16_t vram[0x8000]);
bool SmRoomRuntimeCaptureActive(SmRoomRuntime *room,const uint8_t ram[0x20000],const uint16_t vram[0x8000]);
void SmRoomRuntimePromote(const SmRoomRuntime *room, uint8_t ram[0x20000], uint16_t vram[0x8000]);
bool SmRoomRuntimePromoteDoors(const SmRoomRuntime *room, uint8_t ram[0x20000]);
const uint8_t *SmRoomRuntimeRam(const SmRoomRuntime *room);
const uint8_t *SmRoomRuntimeVram(const SmRoomRuntime *room);
const char *SmRoomRuntimeError(const SmRoomRuntime *room);
size_t SmRoomRuntimeStateSize(void);
bool SmRoomRuntimeSave(const SmRoomRuntime *room, void *out, size_t size);
bool SmRoomRuntimeLoad(SmRoomRuntime *room, const void *in, size_t size);
bool SmRoomRuntimeValidate(const void *in, size_t size, uint16_t room, uint16_t state, bool actors);
