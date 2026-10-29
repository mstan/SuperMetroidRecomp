# Ship save before confirmation, 2026-10-28

The ship now performs its native SRAM save before showing SAVE COMPLETED.
Previously, closing the game while that message was visible could lose the new
progress: the actual save happened only after dismissing it. `ship-save` is a
default-on correctness option under `super-metroid.balance-fixes/bug-fixes`.
No keeps its native behavior; disabling the option reproduces stock behavior.

## Implementation

Four declared hooks reuse the native message and gunship flow:

| Site | Action |
|---|---|
| `85:80CD` | On the ship prompt's actual Yes choice (`0`), tear down the prompt before showing Saved. No is `2`. |
| `A2:AB40` | After native SaveToSram, reuse the existing message call at `AB22` with message `18`. |
| `A2:AB26` | Returning from Saved, bypass the already-performed save; retain the skipped CMP flags. |
| `85:8093` | In the real gunship's second call, retain its original refill sound and 160-frame lead-in. Room, enemy header, aligned enemy slot and native actor state guard this path. |

Native message index/choice encode the phases. There is no additional mutable
mod state, borrowed WRAM, new guest-call API or cartridge-ROM write. The source
reference was Redux's `Backups/src/Bugfixes/ShipBugFix.asm`, read from the owner's
archive for behavior only; its patch was never applied.

## Qualified builds and evidence

Evidence root: `G:/Temp/Codex/sm-qol-20261027/`. Final audited summary:
`ship-final-validated.json`, including exact source/tool SHA-256 identities.

| Build | SHA-256 |
|---|---|
| Windows `sm-ship-build/SuperMetroidSNESRecomp.exe` | `4478f50a86fec4929cf7f2ed1054a4c2cd501ba97fd17f0724e08c69b09c3452` |
| Linux `sm-ship-linux-build/SuperMetroidSNESRecomp` | `03e61a3f08862b5c3286b31dc501161d0481237296b88d9b640d51a11432da07` |

Engine/UI/net pins remain `e265a2be` / `7fdd3a87` / `445eed76`. The final four-hook
candidate regenerated identically twice and passes 25/25 title CTests on both
Windows and Linux (`sm-ship-v2-ctest.log`, `sm-ship-linux-ctest.log`).

- Ten actual ship cases pass: stock/off/on/default Yes, all three save slots,
  stock/fixed No, and stock/fixed normal host quit while Saved remains visible.
  Fixtures set resources and selected slot only; native Down, Yes/No, refill,
  dialog and SaveToSram produce the results. No SRAM/result flags are injected.
- At the visible confirmation, only fixed Yes has already saved. Resources are
  health/reserve/missiles/supers/PBs `499/300/50/15/10`; native checksums and their
  mirrored complements validate. Unselected file bytes and all No-case SRAM
  remain unchanged. The normal-exit `saves/save.srm` exactly matches captured
  SRAM, proving persistence rather than only an in-memory update.
- `ship-save-native-v2` and `ship-save-interp` match every sampled WRAM field,
  complete WRAM/SRAM snapshot and observation; actual interpreter work increases.
  The comparison denies gunship/message roots and available compiled scheduler
  `A0:8EB6`; it does not imply every native message owner has an AOT variant.
- Linux `ship-save-linux` passes the same ten cases. All 38 WRAM checkpoints,
  38 SRAM checkpoints, ten sampled traces and ten persisted save files are
  byte-identical to Windows (trace rows compare independent of line endings).
- `ship-presentation-state-{native,interp}` match full WRAM, composed bitmaps
  and observations. Stock/fixed boxes fully open at tick `178/180` after message
  index assignment, retaining the original 160-frame sound lead-in. Saving and
  loading a current-build state while Saved is visible preserves its phase and
  SRAM, then normal dismissal exits the ship. Actual interpreter work increases.
- Final all-off 3,200-frame stock traces remain original on both platforms.
  Eight cut-all/control real pickup rooms and five legacy pickup-pacing modes
  also pass on the combined Windows candidate.
- `ship-set-netplay-{windows,linux}` accept matching complete 40-feature sets;
  changing only ship-save refuses both peers before frame zero. These are local
  peer pairs on each OS, not a mixed Windows/Linux network-session claim.

The initial three-hook candidate skipped native sound lead-in; it was replaced
and is not passing evidence. Early probes waited only 60 frames after message
index assignment, before the visible box. Final scenarios wait 400 frames and
the composed SAVE COMPLETED screen was inspected. Historical failed probes are
retained separately. No owner saves/settings or delivery builds were changed.

## Reproduction

Use the pinned trace-enabled build and fresh output directories:

```powershell
python tools/test_qol_ship_save.py --build <windows-build> --rom <stock-rom> --out <native>
python tools/test_qol_ship_save.py --build <windows-build> --rom <stock-rom> --out <alternate> --interp --compare <native>
python tools/test_qol_ship_presentation.py --exe <trace-exe> --rom <stock-rom> --out <presentation>
python tools/test_qol_ship_presentation.py --exe <trace-exe> --rom <stock-rom> --out <presentation-alternate> --interp --compare <presentation>
python tools/test_qol_netplay.py --build <build> --rom <stock-rom> --out <peers> --cut-pickup-jingle --mismatch-ship-save
```

The save suite supports Linux natively as well. Source is on the local feature
branch; owner visual/audio/controller acceptance remains separate. No main merge,
code push, release or delivery replacement has been performed.
