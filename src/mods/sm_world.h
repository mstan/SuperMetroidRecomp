/* Read-only semantic map of the supported stock cartridge. No generated ROM
 * bytes are embedded in the program and no guest memory is changed. */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum { SM_WORLD_AREAS = 6, SM_WORLD_ROOMS = 512, SM_WORLD_DOORS = 2048,
       SM_WORLD_MARKERS = 4096, SM_WORLD_STATES = 2048 };
typedef uint8_t (*SmWorldRead)(void *context, uint32_t address);
typedef enum SmWorldMarkerKind {
  SM_MAP_ITEM, SM_MAP_SAVE, SM_MAP_MAP, SM_MAP_ENERGY, SM_MAP_AMMO,
  SM_MAP_CAP, SM_MAP_GATE, SM_MAP_DROP
} SmWorldMarkerKind;
typedef enum SmWorldCap {
  SM_CAP_BLUE, SM_CAP_MISSILE, SM_CAP_SUPER, SM_CAP_POWER_BOMB, SM_CAP_GREY, SM_CAP_EYE
} SmWorldCap;
typedef struct SmWorldRoom {
  uint16_t header, default_state, first_door, door_count;
  uint8_t area, x, y, width, height; /* map y includes the stock +1 row */
} SmWorldRoom;
typedef struct SmWorldDoor {
  uint16_t definition, source, destination, index;
  int16_t block_x, block_y; /* physical/scripted source blocks; -1 for header-only records */
  uint8_t direction, flags, arrival_x, arrival_y, arrival_screen_x, arrival_screen_y;
  bool reciprocal;
} SmWorldDoor;
typedef struct SmWorldMarker {
  uint16_t room, state, plm, argument;
  uint8_t x, y, kind, detail, direction; /* room block coordinates */
} SmWorldMarker;
typedef struct SmWorld {
  SmWorldRoom rooms[SM_WORLD_ROOMS];
  SmWorldDoor doors[SM_WORLD_DOORS];
  SmWorldMarker markers[SM_WORLD_MARKERS];
  uint16_t states[SM_WORLD_STATES], state_rooms[SM_WORLD_STATES];
  uint16_t room_count, door_count, marker_count, state_count;
  uint16_t tiles[SM_WORLD_AREAS][2048];
  uint8_t station_map[SM_WORLD_AREAS][256];
  uint16_t room_at[SM_WORLD_AREAS][2048]; /* index + 1, zero if unmapped */
  char error[128];
} SmWorld;

bool SmWorldLoad(SmWorld *world, SmWorldRead read, void *context);
const SmWorldRoom *SmWorldFindRoom(const SmWorld *world, uint16_t header);
uint16_t SmWorldRoomState(const SmWorldRoom *room, SmWorldRead read,
                          void *context, const uint8_t *wram);
bool SmWorldExplored(const uint8_t *wram, unsigned area, int x, int y);
bool SmWorldKnown(const SmWorld *world, const uint8_t *wram,
                  unsigned area, int x, int y, bool reveal, bool secrets);
bool SmWorldItemCollected(const uint8_t *wram, const SmWorldMarker *marker);
uint16_t SmWorldTile(const SmWorld *world, unsigned area, int x, int y);
const SmWorldRoom *SmWorldCellRoom(const SmWorld *world, unsigned area, int x, int y);
/* Geometric room atlas from actual exits/arrivals. Returns the number of
 * inconsistent directed constraints (warps/non-aligned passages); views may
 * draw connectors for those rather than claiming a planar embedding. */
unsigned SmWorldLayout(const SmWorld *world, int16_t xy[SM_WORLD_ROOMS][2]);
