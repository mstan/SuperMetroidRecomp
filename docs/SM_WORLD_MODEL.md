# Shared map model

`src/mods/sm_world.{h,c}` reads semantic facts from the supported stock cartridge
through an injected byte reader. It changes neither cartridge bytes nor guest
memory. Map views can share this model and independently choose presentation.
No ROM-derived image or level data is embedded in the program.

The loader follows Landing Site's room graph, retaining elevator dummy slots
so subsequent door indices remain correct. It parses conditional/default room
states, every state's PLM population and compressed level data. Native door
blocks locate source exits by their BTS index. Room coordinates use the game's
map coordinates, including its additional Y row. Tiles/exploration use the
two 32-by-32 SNES tilemap pages, rather than a 64-column linear bitmap.

Items, coloured caps, shootable gates, save/map/refill stations retain their
room state and native coordinates. Gate arguments select colour and trigger
side. Item collection reads the original item-bit array. The current area's
live exploration bitmap takes precedence over its saved copy. Other areas
use their saved bitmaps; map station/reveal visibility never sets those bits.
The current room's actual state pointer takes precedence over predicting its
conditional state. Arrival-door conditions in other rooms remain unknown.

On the owned stock ROM, the first complete audit reads **255 Zebes rooms,
595 door-list entries, 310 room states and 408 state-specific markers**.
The markers contain **100 unique items**, 108 item entries including state
variants, 170 coloured caps, 19 saves, five maps, seven energy stations,
four ammo stations, 11 shootable gates and 84 crumble-floor markers. Crumble
arrows resolve the native BTS/PLM setup and only mark down-only floors, not
other special blocks. All 310 level streams decode with
bounded output. Some rooms retain padded source rows; the size word locates
BTS while the room dimensions bound playable coordinates.

Nine entries have no door block in their level variants and retain coordinates
`-1/-1`: `DD58:1`, `A0D2:1`, `D54D:0`, `D57A:0`, `D4EF:0`, `D51E:0`,
`D461:2`, `D4C2:2`, `B3A5:2`. These need scripted/arrival-geometry handling in
the full audit. Atlas placement uses reverse arrival geometry where available;
these do not receive invented physical blue door caps. A zero-destination
elevator dummy is not a one-way exit. A missing
reverse room edge alone does not establish the direction of an internal
crumble passage; the crumble setup itself supplies that directional fact.
Full reference-map visual comparison remains pending.

With `BUILD_TESTING=ON`, `sm_world_dump` exports machine-readable facts:

```powershell
sm_world_dump.exe <owned-unheadered-stock-ROM.sfc> <new-output.json>
```

The synthetic `sm_world_model` test checks graph/state/marker decoding,
LoROM-crossing compression, malformed data rejection, page/bit ordering,
live/saved exploration, item collection and unchanged guest memory.
It passes GCC with `-Wall -Wextra -Werror` on Windows and Ubuntu/WSL.
Final raw stock audit: `G:/Temp/Codex/sm-qol-20261027/world-model-final.json`.
Map-view qualification: [SM_MAPS_20261028.md](validation/SM_MAPS_20261028.md).
