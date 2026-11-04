# Optional Zero Mission map art

Clearer pause map and the detailed Zebes map can read original room tiles,
the map palette, and the S/M station glyphs from the player's **Metroid:
Zero Mission (USA)** GBA ROM. Select it in either feature's optional resource
picker; the selection is shared. With no donor selected, the existing map
appearance remains available. The gameplay minimap and small atlas zooms keep
their compact symbols. No Zero Mission program or ROM bytes ship in this
repository or a release package.

The supported 8 MiB donor is `BMXE`, revision 0, SHA-256
`fc94f65380b65b870a30b9b04b39cca1dc63d6e46a4a373d3904adc0912ebc37`.
The resource provider verifies that identity, and the importer checks it again
before reading the graphics. Unsupported or changed files are never decoded.
Both loaded game ROMs remain read only.

Nintendo R&D1 created the original graphics. Asset locations were identified
from [metroidret/mzm's extraction database](https://github.com/metroidret/mzm/blob/7708d105cb3537f941449a9ac74795bb435493fa/database.json)
and [map graphics declarations](https://github.com/metroidret/mzm/blob/7708d105cb3537f941449a9ac74795bb435493fa/src/data/menus/pause_screen_map_data.c):

| USA offset | Imported data |
| --- | --- |
| `0x40E0C4` | `minimap_tiles.gfx`, 5,120 bytes |
| `0x40F4C4` | adjoining `40f4c4.gfx`, 7,168 bytes |
| `0x411360` | `minimap_tiles.pal`, 80 little-endian BGR555 colors |

The 4-bit tile indices and flips provide all 16 room-wall combinations. The
donor's blue unexplored and orange explored palettes represent the existing
Super Metroid knowledge bits; importing art never reveals or collects anything.
Super Metroid still supplies the room/door graph, current-state door colors,
one-way/gate markers, item rings/dots and station locations. Energy/ammo labels
retain their existing distinct symbols. Detailed cells use nearest-neighbor
tile scaling; compact cells retain the established semantic rendering so thin
walls and markers remain readable. Existing Falcon Zero and mod credits remain.

`sm_map_test <owned-donor.gba>` checks the actual donor's complete identity and
all 16 boundary combinations without storing copyrighted fixtures in Git.
`tools/test_qol_maps.py --donor <owned-donor.gba>` uses the same normal runtime
resource selection and requires successful donor activation as well as the
existing discovery, item, SRAM, input and pause/resume checks.
