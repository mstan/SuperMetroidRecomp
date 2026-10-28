# Cut every pickup jingle, 2026-10-28

The owner explicitly requested cutting the jingle for every item, in addition
to the inherited hack behavior. `super-metroid.pacing/item-pickup` now offers
**No pickup jingle** (`mute-jingle`, default false). It removes the fanfare for
major upgrades, first ammo pickups and expansions. Room music continues, and
this setting overrides Short expansion sound: no replacement pickup sound is
queued. Message skipping and instant pickup remain independent. Native suit
transformations remain pending the owner's separate scope decision.

## Implementation

The declared `$84:8BDD` hook skips the item music instruction without clearing
the room music queue. All nine collector families skip their delayed room-music
restart. Four additional major-collector sites are declared in `bank04.cfg`:
`88DE→88E5`, `8905→890C`, `8930→8937`, `8957→895E`. Beam/equipment messages
load their ID from the PLM argument; their argument-load instruction is retained.
All six Varia/Gravity lists first run `E29D` (clear charge) before `88F3`;
the fanfare hook recognizes that exact sequence too. Native item grants,
transformations and unrelated music instructions keep their own paths.
These are runtime hooks; cartridge ROM is never modified.

## Windows gameplay evidence

Retained root: `G:/Temp/Codex/sm-qol-20261027/`. Separate executable:
`sm-pickup-build/SuperMetroidSNESRecomp.exe`, SHA-256
`1873daaecddc042e24bab7cd05d11fdd1c4167fecb2e8b817af9b0a89bcb109a`.
The existing owner playtest executable and profiles were left untouched.

- `pickup-all-suits-{native,interp}/results.json`: 56 cases cover all nine
  collectors, both suit prefixes, first/repeat ammo, legacy short sounds,
  cut-all with a message and cut-all with instant pickup. Native ROM instruction
  lists are injected into one PLM slot: item traversal/contact is explicitly
  bypassed. The native collector must grant the item. No acquisition result is
  manufactured. Cut-all retains room track5 and queues no replacement SFX;
  short-only keeps its existing minor/repeat behavior. Every full WRAM snapshot
  and observation matches alternate execution, with actual interpreter increase.
- `pickup-mute-rooms-suits-{native,interp}/results.json`: real Varia, Gravity,
  first missile and reserve pickups, stock/cut-all. Native rooms, shots and
  contact perform collection; position assistance only, plus the explicit
  Phantoon room-power prerequisite for Gravity. Room tracks remain respectively
  3/5/5/3 throughout cut-all; controls play fanfare2. Native transformations
  finish at stage0 and input handler `E695`. All observations/full WRAM match
  alternate execution; 8KB native SRAM remains unchanged in every case.
- `pickup-legacy-native/results.json`: the five existing real reserve pacing
  cases still pass unchanged, including frozen frames425/79/425/0/0.
- `pickup-final-smoke-v2/results.json`: all-off 3,200-frame raw trace remains
  `66976992eb0baea5b7c6696cd6f8e0def597797cb8880497204a58ac93cfc947`.
- `pickup-cut-netplay/results.json`: all40 features plus cut-all agree and run
  through frames897/898. Changing only `mute-jingle` on one peer is refused by
  both peers before frame0. Windows title CTests pass25/25.

Alternate gameplay includes available compiled scheduler/music consumers; it
does not invent a compiled collector variant where native execution remains
interpreter-authoritative. The first real Varia probe exposed the missing
charge-clear prefix; failed calibration output is retained separately and is
not passing evidence. The first stock smoke collided with a debugger port;
scripted QA now reserves a private port and the corrected stock run passes.

Linux qualification also passes in the separate `sm-pickup-linux-build`, SHA-256
`f3b2dce53d7497313cbba476039c78917262963de00f2ad2dd57d6eb7e54f54d`:
25/25 CTests, all56 collector cases, all8 real-room cases, matching/mismatching
cut-all peers and the original Linux stock trace. Logs/results use
`pickup-all-suits-linux-v2`, `pickup-mute-rooms-suits-linux-v2`,
`pickup-cut-netplay-linux`, `pickup-stock-linux`. Every observation and all216
complete WRAM checkpoints (176 collectors,40 rooms) also match Windows:
`pickup-cut-cross-host-validated.json`. These are local peer checks on each host;
the earlier cross-host network gate remains separately identified.

The first Linux TCP suite completed observations but timed out while stopping
its paused hosts. QA now resumes only after capturing final paused evidence,
then requests termination; bounded fallback targets only that owned test process.
The corrected full Linux run needed no forced stops. Original failed output is
retained separately, not counted as passing evidence.

## Reproduction

Regenerate with `tools/regen.py --out-dir <new-gen> --strict-idempotent`, using
the pinned engine and `SNESRECOMP_EMIT_AOT_DENY_GATE=1`. Two generations were
byte-identical. Build with `SNESRECOMP_ENABLE_TRACE=ON` for TCP QA.

```powershell
python tools/test_qol_pickup_sounds.py --exe <trace-exe> --rom <stock-rom> --out <fresh-native>
python tools/test_qol_pickup_sounds.py --exe <trace-exe> --rom <stock-rom> --out <fresh-interp> --interp --compare <fresh-native>
python tools/test_qol_pickup_rooms.py --exe <trace-exe> --rom <stock-rom> --out <fresh-rooms>
python tools/test_qol_pickup_rooms.py --exe <trace-exe> --rom <stock-rom> --out <fresh-rooms-interp> --interp --compare <fresh-rooms>
python tools/test_qol_netplay.py --build <build> --rom <stock-rom> --out <fresh-peers> --cut-pickup-jingle
```
