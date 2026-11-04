# Super Metroid ROM-write removal — 2026-10-28

The enhancement plugins now run against the unchanged stock cartridge image.
The engine/UI pins are `a4f3674f61ba7de256cbd13d199b05c6669632a6` and
`e4d876266ca0f6fab9fe557e2b3760387465586c`; `framework_pins.txt` agrees with
both gitlinks and the nested network/rollback pins. No main merge or release.

## Changes

- All 34 immediate declarations/callers use mod-owned operands. Both engine
  parsers reject the removed `live_operand` directive.
- Shake offsets, the ammo-station message row and the shinespark dispatcher
  are supplied by guest-instruction hooks. Original cartridge tables remain.
- Heavy physics uses 15 consumer hooks and one declared operand. Its 69
  historical byte runs become typed physics values and 67 timed demo records.
  Traversal of all 25 demo objects found 796 records and accounted for every
  changed demo byte. No arbitrary ROM-read overlay is introduced.
- ROM-writing/guarded-patch helpers and the title's 64-entry hook list are
  deleted. Read-only stock PLM access remains as `SmModReadRom16`.
- Mod implementation sources/headers participate in replay/snapshot/build
  identity, alongside the effective feature selections.

## Evidence

Evidence root: `G:/Temp/Codex/sm-qol-20261027`.

| Gate | Result |
|---|---|
| Supported full regeneration, twice | Byte-identical output (`sm-table-regen.log`) |
| Windows Release build | Passed, MinGW GCC/Ninja (`sm-operand-build.log`) |
| Game CTests | 20/20 passed; controls and data-consumer assertions enabled in Release |
| Data-consumer native regression | Windows and Ubuntu/WSL passed; positive/negative shake overflow, off offsets/flags, final message letter, adjacent tile and other-message isolation |
| Framework pin check | All four pins match |
| Disabled features, 3,200-frame attract trace | Matches original `build-base` exactly: SHA-256 `66976992eb0baea5b7c6696cd6f8e0def597797cb8880497204a58ac93cfc947` |
| Heavy physics | Normal jump launch 4 → 6; gravity `$1C00` → `$3400`; measured airborne interval 90 → 68 frames |
| Shake types 8, 17, 35 | 220 nonzero offset observations: stock ±3 px, half ±2 px, off 0; timer sequences identical |
| Missile station | Actual stock PLM instruction refills 5/0/0 missiles/supers/PBs; mod refills 5/3/2. Full message row and surrounding spaces checked in WRAM |
| Steering | Horizontal, vertical and diagonal sparks run; mod selects `$D0D7` and changes the perpendicular coordinate under D-pad input |
| Current catalog activation | All 27 features activate; no missing-hook or refused-write messages (`sm-cases/all-current/stderr.log`) |

The runtime tool completed 14 candidate cases plus the original stock control.
Raw scripts, logs, initial-condition saves, WRAM traces, binary hashes and
machine-readable results are in `rom-removal-runtime-v2/`. All automated game
launches used dummy video/audio, fresh profiles and disposable native saves;
the original builds and owner saves/settings were preserved.

The original text edit started on a tile's attribute byte. The runtime check
caught an incomplete replacement span; the corrected hook covers all 17 tiles
and preserves the remaining surrounding space. An initial steering assertion
also counted the pose-selection frame before its movement handler was installed;
the check now counts actual spark-handler frames. The final suite passes.
Final review also corrected the room-shake hook's surviving CPU overflow flag;
the new native test covers that arithmetic boundary. The final game build was
rechecked for the stock trace and shake path after this correction, with raw
profiles under `sm-cases/stock-final` and `sm-cases/shake-half-final`.

Reproduce with an owned stock ROM and a new output directory:

```powershell
python tools/test_qol_rom_removal.py --build <candidate-build> --rom <owned-ROM> --out <new-evidence-directory> --baseline <original-build>
```

The engine mechanism separately passed 615 Python tests, Rust 112 library + 7
binary tests, and the full portable C gates on Windows and Ubuntu/WSL. The
operand runtime fixture exercises compiled/interpreted execution, all supported
opcodes/widths, bank aliases, clear/reset, 1,100 active sites and immutable ROM.
See `snesrecomp/docs/validation/MOD_OPERAND_20261028.md`.

## Remaining qualification

Game-level tier parity for every feature remains pending. The runtime tool's
`--interp-targets` option requires a separate QA build emitted with
`SNESRECOMP_EMIT_AOT_DENY_GATE`; a normal build does not provide that proof.
Physics table consumers beyond the measured normal jump, all retimed title
demos, the broader combat/balance burndown, Linux game build, netplay mismatch
check and owner visual/controller review still require their final gates.

Retain these builds/raw results for the ongoing feature burndown. Temporary
fixtures/build output live outside delivery folders and are tracked under the
evidence root for cleanup when the goal is complete.
