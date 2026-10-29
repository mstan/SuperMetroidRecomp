# Owner map and auto-charge visual fixes, 2026-10-29

The owner's `owner-playtest-final/notes.json` records ten individual Pass results
and two failures: the original map flashes during pause open/close, and the
auto-charge cannon flare floats beside Samus's moving arm. All ten acceptances
are retained. Only those two failed checks are prepared for owner follow-up.

## Behavior and implementation

The enhanced map now renders through native pause fade-in/out and equipment-tab
fades. `pause_screen_mode` identifies the actually loaded page, while settled
map controls retain their original input gate. The overlay follows INIDISP
brightness, including forced blank; native borders/footer are not dimmed again.
Exiting directly from the world map retains that page through the fade instead
of briefly showing the area map. The transient closing flag clears on leaving
the pause presentation and on state-load/rewind timeline changes.

Auto-charge follows the owner's idle-only flare suggestion. During movement
without held Shoot, it omits the cannon flare's draw call. Standing idle and
actual held-Shoot poses keep native rendering. Charge timers, Samus's native
palette and the charge sound continue; Hyper Beam remains native. One declared
hook at `90:BBE1` precedes PHP and uses the function's bare RTS at `90:BCBD`,
without skipping a stack restore that already ran. No cartridge-ROM writes,
forced firing-arm poses or synthetic held input are introduced.

## Qualification

Evidence root: `G:/Temp/Codex/sm-qol-20261027/`. Exact source/tool hashes are in
`visual-build-identity.json`. Candidate Windows binary:
`sm-visual-build/SuperMetroidSNESRecomp.exe`, SHA-256
`8d3136ac41d8baa252379ac8d267afa3d408921a0b884da30dc3e76a04e02998`.
Engine/UI/net pins remain `e265a2be` / `7fdd3a87` / `445eed76`.

- `tools/test_qol_visual_fixes.py` uses actual native inputs and private muted
  headless profiles. Its old-build control reproduces both reports: 165 map
  frames fail the enhanced-header/brightness comparison, and moving auto-charge
  produces detached flare sprites. This is correctness evidence, not a speed
  benchmark or a performance-win claim.
- The candidate passes all 258 captured map frames across open, equipment
  transition, return to map, close, reopen and direct world-map close. Every
  enhanced header pixel matches the selected page at native brightness.
  Full map guest-WRAM checkpoints remain identical to the old build.
- Forty-seven moving idle-charge samples have no cannon flare. Idle samples
  retain it, as do 30 charged held-fire samples. Flare tiles are derived from
  the native ROM spritemap table and identified in actual OAM. Charge, pose,
  position, held controls, native flare animation timers, sound queue bytes and
  projectile observations exactly match the old build. Before/after running
  captures and the retained idle flare were visually inspected.
- `visual-{native,interp}` match every observation, full WRAM checkpoint and
  composed bitmap, with increased interpreter work for both cases. Denied
  targets include the available scheduler and relevant charge owners; no
  nonexistent compiled pause owner is claimed. SRAM remains unchanged.
- Existing five auto-charge scenarios pass normal and alternate execution:
  charged/normal/normal/charged tap sequence, no idle shots, missing-Charge-Beam
  control, native PB-selected and optional Select combos. All observed fields
  and WRAM/SRAM/VRAM/CGRAM/OAM/framebuffer checkpoints match across paths.
- The all-off 3,200-frame stock trace remains the original
  `66976992eb0baea5b7c6696cd6f8e0def597797cb8880497204a58ac93cfc947`.
- Strict regeneration is byte-identical twice. Windows 25/25 title CTests pass;
  after the final world-fade addition, the affected map test was rebuilt and
  repeated, reusing the 24 unchanged test results. That regression covers all
  three map presentation states, seven brightness/forced-blank values, native
  equipment/gameplay exclusions, full-frame guards and world closing.
- The affected map test also rebuilds and passes under Ubuntu/GCC. The existing
  Linux game binary is not replaced, and a new Linux full-game visual matrix is
  not claimed. Logs: `visual-{final,linux}-map-ctest.log`, `visual-ctest.log`,
  `visual-auto-{native,interp}.log`; suite JSONs are in the matching directories.

Capacity reservations and telemetry are retained. Local build headroom became
available; matched old/candidate checks ran on this PC. Earlier capacity-rejected
attempts launched no tests. Unused agent-created SERVER staging and the local
transfer ZIP were removed after exact containment/reparse/repository audits.
Current build, limited states and evidence remain tracked for owner follow-up.

## Owner follow-up

`tools/sm_qol_playtest.py prepare --retest-failures --notes-from <previous>/notes.json`
prepares only the failed subchecks. The selector uses its prepared manifest;
saving those observations preserves all hidden Pass results. New notes retain
the original failures until the owner changes them. Prior profiles and notes
remain preserved, and ordinary saves/settings are never used.

Current follow-up root: `owner-visual-retest`, selector localhost8786. The map
and combat states use the candidate binary and are independently restored and
resumed before handoff. Normal window/audio and keyboard/controller selection
remain available. Owner visual acceptance of these fixes is still pending;
the ten earlier Pass results do not need repeating. Falling aim-down and suit
transformation scope remain separately unanswered. No main merge, code push,
release or delivery replacement is included.

```powershell
python tools/test_qol_visual_fixes.py --exe <old-exe> --rom <stock-rom> --out <old> --baseline
python tools/test_qol_visual_fixes.py --exe <candidate-exe> --rom <stock-rom> --out <native> --mechanics-baseline <old>
python tools/test_qol_visual_fixes.py --exe <candidate-exe> --rom <stock-rom> --out <alternate> --interp --compare <native>
```
