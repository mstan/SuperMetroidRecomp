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
595 door-list entries, 310 room states and 414 state-specific markers**.
The markers contain **100 unique items**, 108 item entries including state
variants, 176 coloured caps (including six eye/guard entries), 19 saves, five maps, seven energy stations,
four ammo stations, 11 shootable gates and 84 crumble-floor markers. Crumble
arrows resolve the native BTS/PLM setup and only mark down-only floors, not
other special blocks. All 310 level streams decode with
bounded output. Some rooms retain padded source rows; the size word locates
BTS while the room dimensions bound playable coordinates.

The nine missing source positions were audited against every level variant.
`DD58:1` is Mother Brain's escape door: native routine `$A9:B32A` creates
PLM `$B677` at `(0,6)`. The model reads that spawn record and the setup's
`$9001` block operand from the cartridge, with signature/index/bounds checks.
The remaining eight headers have no physical source block:
`A0D2:1`, `D54D:0`, `D57A:0`, `D4EF:0`, `D51E:0`, `D461:2`, `D4C2:2`,
`B3A5:2`. They retain `-1/-1` and impose no exit or atlas constraint.
Reciprocity requires physical exits on both sides. In particular, header-only
sand-passage reverse records must not hide a genuine one-way arrow.
A zero-destination elevator dummy is not a one-way exit. A missing
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
Eye PLMs `$DB48/$DB56` use orange guard markers with an eye detail, rather
than defaulting to a normal blue door. Their body pieces do not create duplicate caps.

Current raw stock audit: `G:/Temp/Codex/sm-qol-20261027/world-model-eye-guards.json`;
per-state source evidence is in `unknown-doors-audit.txt` beside it.
Map-view qualification: [SM_MAPS_20261028.md](validation/SM_MAPS_20261028.md).
