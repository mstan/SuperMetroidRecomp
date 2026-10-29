# Super Metroid enhancements: per-feature QoL mods

Working handoff, updated 2026-10-28. Codex is working solo on the feature burndown; the old combat agent remains stopped. It records the plan, the owner's rulings, the
current state of every branch, the work in flight and what is left, so another
agent (Codex or Claude) can resume without the original conversation.

## 1. Goal

Owner follow-up at Codex takeover (2026-10-27): the original wishlist and
exact Falcon Zero image are retained in
[`docs/SM_QOL_TESTER_REQUEST.md`](docs/SM_QOL_TESTER_REQUEST.md) and
[`docs/reference/metroid3_map.gif`](docs/reference/metroid3_map.gif).
Auto charge is defined: charge automatically while idle, tap to release a charged shot, then fire normal successive shots until another charge is ready. The owner deferred falling aim-down clarification until the final handoff.

**Current checkpoint:** the post-review visual candidate extends production base
`b7bf82b` (cut-all pickups and ship-save), engine `e265a2be`, UI
`7fdd3a87`, nested recomp-net `445eed76`. Windows and Linux pass all 25 title
tests; the final all-off stock trace is unchanged and all 40 features activate.
Actual local game peer pairs on Windows and on Linux accept matching complete settings and refuse a
tail-option mismatch before frame zero. Combat, movement, pacing and corrected
map comparisons pass the available execution-path checks. The map model now has
414 markers, including eye doors; native scripted escape and physical one-way
doors are audited. See the current measurements, exact coverage limits and
remaining owner judgments in
[`docs/validation/SM_QOL_FINAL_CHECKPOINT_20261028.md`](docs/validation/SM_QOL_FINAL_CHECKPOINT_20261028.md).
Owner review now records ten individual Pass results and two presentation
failures. Map-fade flashes and detached moving auto-charge flares are corrected
and pass targeted normal/alternate, stock and affected Linux checks. See
[`docs/validation/SM_VISUAL_FIXES_20261029.md`](docs/validation/SM_VISUAL_FIXES_20261029.md).
Follow-up uses `owner-visual-retest` and asks only those two failed subchecks;
all ten earlier Pass results are preserved. Visual acceptance of the fixes is
pending. Prepared owner setups use `tools/sm_qol_playtest.py`; notes are stored per check.
The owner subsequently rejected the redundant gameplay ammo row. That mode and
its Always control are removed; the optional pause display shows maximum
capacities only. See `docs/validation/SM_AMMO_HUD_20261029.md`. Live owner windows
are preserved; the replacement uses `sm-hud-build` and fresh limited states.
The earlier seven-setup handoff used `owner-playtest-final` at localhost8786;
see [`docs/validation/SM_OWNER_PLAYTEST_20261028.md`](docs/validation/SM_OWNER_PLAYTEST_20261028.md).
Prior profiles and observations are preserved.
The cut-all pickup jingle request is implemented and qualified in the combined
Windows/Linux builds, including216 matching pickup WRAM checkpoints across hosts.
Cinematic and isolated
resource/combo/visor gates are qualified by QA commits `8455d24` and `d3a3159`.
The inherited ship-save omission is corrected and qualified with four native-flow
hooks: save before the visible confirmation, preserving native sound/timing.
Ten Windows/Linux ship cases, normal/alternate comparisons, current-build state
reload, stock traces and setting-mismatch refusal pass. See
[`docs/validation/SM_SHIP_SAVE_20261028.md`](docs/validation/SM_SHIP_SAVE_20261028.md)
for final combined-build identities and measured scope.
Falling aim-down and suit-animation scope await the owner's
answers. Full hack packs remain deferred. No main
merge, code push, release or delivery replacement is authorized.

**Controlling scope after owner clarification:** preserve modern F-Zero/SMW
semantic content loaders, BS content, MSU/music, vehicles and native multiplayer.
Remove the historical generic playable-ROM patch workflow and the abandoned
SMW IPS/Luigi executable. Do not withdraw supported features or delete reviewed
donor-resource decoders that the modern loaders still require.

### Codex checkpoint: corrected candidates validated

The owner requested validation of the current F-Zero and both SMW branches,
then a check-in before returning to Super Metroid. That preservation regression
gate is complete. The owner then started the Super Metroid QoL goal; implementation resumed on 2026-10-28.

All checkouts below are under `G:/Temp/Codex/sm-qol-20261027/`:

| Current branch base | Corrected local candidate | Engine / UI | Checkout |
|---|---|---|---|
| F-Zero Forever `eaae4f4` | `78273b4`, `fix/preserve-fzero-loaders-20261027` | `0ff9416` / `a866b5b` | `fzero-modern` |
| SMW content `a2d5d81` | `4ae1888`, `fix/legacy-patcher-content-20261027` | `90d54e3` / `3d8cdf6` | `smw-content` |
| SMW native co-op `d4b5865` | `4115104`, `fix/legacy-patcher-native-20261027` | `be86f80` / `18e06fd` | `smw-coop` |

The generic shared variant IPS/BPS applier, launcher patch picker/API/tool,
localization ROM writer and obsolete SMW IPS executable/tools are removed.
Guarded writes reject cartridge-ROM overlaps while retaining RAM use.
`content_patch.c`, `cp_patch_apply` and `cp_pack_apply` remain for reviewed
donor-resource extraction. Existing F-Zero game-owned BS module/data and
legacy music compatibility adapters are preserved; this is **not** a claim
that those adapters now perform no image assembly or that every IPS parser
has been deleted. Their native loader/music/multiplayer paths remain working.

The earlier broad F-Zero withdrawal `5f03229` was reverted by `ffd825e`.
Do not integrate that old stock-only candidate. Earlier `253c635`/`e4d8762`/
`4a27a84` results are superseded by the per-pin candidates above. The shared
donor-helper correction is also committed as `ca9bc269` on the inherited
`feat/sm-qol-mod-hooks` branch; the later operand work is committed as
`a4f3674f` (6.1).

Windows validation:

- F-Zero: desktop/headless builds with embedded BS Deluxe; 18/18 CTests;
  75 record identities and folder/ZIP parity; races from Astra/Bower/CGP/MAX;
  matched stock/BS/custom native two-player hashes with save/load and rewind;
  eight real synthetic MSU playback/fallback/rewind checks.
- SMW content: build; 204 content tests (seven private-fixture skips); real
  THNAN gameplay plus stock/installed captures byte-identical to the existing
  accepted build, including stock WRAM CRC `13D6AC0F`; native save isolation.
- SMW native co-op: build; core/hook/input/storage tests; both actors' cape,
  star, pipe and door runtime checks. Native co-op implementation/package
  unchanged by removal.
- Per-pin launcher checks: F-Zero 5/5, SMW content 5/5, native co-op 3/3;
  shared guarded-write/donor/manifest/localization checks pass.

One existing failure is retained explicitly: F-Zero's lifecycle harness has
an identical viewport hash mismatch before and after removal, tracked as
`beads-8wg.5.111`. It is not a passing viewport test or a removal regression.
No new removal regression was found in the checks above.

Evidence: `G:/Temp/Codex/sm-qol-20261027/modern-preservation-evidence.json`,
adjacent scripts/logs, and each candidate's
`docs/validation/ROM_PATCHER_REMOVAL_20261027.md`. Passing candidate builds
are retained for integration; temporary cleanup is recorded alongside them.
Original modern worktrees, delivered builds, owner saves and settings are
untouched. No code push, main merge, release, Linux/MSVC build or human
controller/visual qualification was performed.

**Historical Super Metroid conversion checkpoint (2026-10-28).** The engine operand
runtime is committed as `a4f3674f`; the SM integration branch pins it and UI
`e4d8762`. All 34 immediate sites use `mod_operand`. Cartridge writes and the
64-hook title registry are removed; table behavior is supplied by declared
runtime hooks. Full regeneration is deterministic, the Windows build and
20 CTests pass, and disabled-mod stock behavior matches the original trace.
Converted-feature runtime evidence and remaining qualification are in 6.1.
Auto charge is defined; falling aim-down is deferred to the final review.

A tester sent a Super Metroid quality-of-life wishlist plus three favourite
ROM hacks: **Ancient Chozo 1.1.5**, **Super Metroid Redux** and **Ascent
1.12**. The archives are at `E:\Downloads\Ancient chozo.7z`,
`E:\Downloads\Super Metroid Redux.zip` and `E:\Downloads\SM_Ascent_1.12.zip`.
The tester also gave a Falcon Zero full Zebes map as the reference for a
"clearer map": Zero Mission-style door colours, one-ways and item icons.

These are turned into **individually toggleable features**, not three hack
bundles. Features are grouped into themed packages, each with per-feature
toggles and options. All of them are implemented against the **stock ROM** as
engine mod packages.

## 2. Owner rulings (binding)

| Date | Ruling |
|---|---|
| 2026-10-27 clarification | Preserve modern semantic F-Zero/SMW content loaders, music/MSU, BS content and native two-player mods. Remove the historical playable patched-ROM/recompilation workflow and abandoned SMW IPS/Luigi co-op. Audit donor/resource-import helpers by their actual use before deleting shared dependencies; stock-only checks on old branches do not qualify modern functionality. This scope clarification takes precedence over the earlier broad-removal interpretation. |
| 2026-10-27 | Per-feature mods, grouped into themed packages with per-feature toggles. |
| 2026-10-27 | Full hacks (Ancient Chozo/Ascent level design, Ancient Chozo art) come later through the **SMW shared-data-pack approach** built for *This Hack Needs A Name* (`SuperMarioWorldRecomp/docs/SHARED_DATA_PACKS.md`, branch `feat/smw-custom-mods`): a semantic `pack.json` world/audio pack loaded by stock recompiled code, a content selector, a separate save namespace per pack, and the launcher's Custom Mods page. **Ancient Chozo work is deferred indefinitely** (bead `beads-8wg.6.33.41`). |
| 2026-10-27 | **ROM patching is forbidden for these enhancements.** Implement against the stock program without applying a playable IPS/BPS hack, recompiling a patched ROM, or writing into the loaded ROM image (SM's `SmModRomPatch`/`SmModPatchImm16`, the engine's old `live_operand`, `guarded_patch` aimed at ROM). Modern game loaders and reviewed donor-resource imports are preserved under the later scope clarification. Enhancement behaviour goes through the mod loader: `mod_hook`, `mod_operand`, input transforms, overlays and content loaders. Only content loaders in the SMW/F-Zero style get promoted into the shared engine. |
| 2026-10-27 | Credits skip must **not** skip the ending pose. That is what it does today, so no change is needed. |
| 2026-10-27 | **Automated playtesting** is the default. Validate every feature headlessly, using pokes and cheats to reach the needed states. Hand the owner only a short, focused checklist of what automation genuinely cannot judge. |
| 2026-10-27 | Do not give time estimates; report task burndown. |
| standing | Work on worktrees and branches. Commit only on feature branches; never push or merge to main without the owner. |
| standing | Track work in Beads at `F:\Software\beads\issues` (see section 7). |
| standing | Never run unscoped recursive searches over `F:\Projects`, `F:\Software` or whole drives. Use `git grep` inside one repo. |
| 2026-10-28 storage policy | Use bounded task scratch, check free space/I/O, and clean disposable outputs. Current scratch/evidence is `G:/Temp/Codex/sm-qol-20261027`; C: is allowed under the current global instructions. |
| standing | Never `taskkill /IM SuperMetroidSNESRecomp.exe`, which kills other agents' runs. Kill by PID only. |
| standing | "Most complete option" always: general mechanisms over one-off patches, regenerate all banks, run all tests. |
| open | **Auto charge** (`.33.20`) is now defined as idle charging plus tap-to-release. **Falling aim-down** (`.33.26`) is explicitly deferred; ask at final handoff and do not guess. |

## 3. Repositories, worktrees, branches

All local; **nothing is pushed**.

| Path | Repo | Branch | Base | Purpose |
|---|---|---|---|---|
| `F:\Projects\snesrecomp\_wt-sm-qol-engine` | snesrecomp engine | `feat/sm-qol-mod-hooks` | origin/main `5ac4b98` | Engine mechanisms. `a4f3674f`: declared mod-owned operands, both execution tiers, full engine gates passed (6.1). |
| `F:\Projects\snesrecomp\_wt-sm-qol-mods` | SuperMetroidRecomp | `feat/qol-feature-mods` | origin/main `e617b65` | **Integration branch.** The inherited handoff checkpoint is `8a9ab42`; current conversion is recorded in 6.1. Candidate build/output: `G:/Temp/Codex/sm-qol-20261027/sm-{build,gen}`. Original `build-base` retained as a stock control. |
| `F:\Projects\snesrecomp\_wt-sm-qol-pacing` | SuperMetroidRecomp | `feat/qol-pacing` | | Merged (`6740f29`). Can be removed. |
| `F:\Projects\snesrecomp\_wt-sm-qol-movement` | SuperMetroidRecomp | `feat/qol-movement` | | Fast-forwarded into the integration branch (`edb3f8e`). Can be removed. |
| `F:\Projects\snesrecomp\_wt-sm-qol-combat` | SuperMetroidRecomp | `feat/qol-combat` | `edb3f8e` | Historical stopped branch `6e80b43` + `74a1abd`; both were integrated locally as `bf3d0f8` / `cb79ac3`, then fixed and qualified. No main merge. See 6.2. |

The SM `snesrecomp` submodule is pinned to engine commit `e265a2be` (operand
implementation base `a4f3674f`, plus complete netplay mod-set publication). That
commit exists only in the local engine repo, `F:\Projects\snesrecomp\snesrecomp`,
which shares its object store with the engine worktree. `recomp-ui` is pinned
to `7fdd3a87`; nested recomp-net is `445eed76` (wire 3 / module ABI 2), and
`framework_pins.txt` matches the physical checkouts and gitlinks.

**Network capacity checkpoint:** the full 40-feature settings list is 2,526
bytes. Shared bounded startup chunks, lobby fields and SNES/UI publication,
adoption and launch checks now carry it completely. Native 34-test net suite,
module ABI, Windows UDP, real catalog/option/adoption and two-/three-seat
latency/jitter/loss checks pass. Matching sets run; tail mismatches refuse at
frame zero. See `snesrecomp/docs/validation/LARGE_MOD_SETS_20261028.md`.
Final deterministic regeneration, network-enabled Windows/Linux game builds
and actual matching/mismatching game peers now pass. Other games' validated removal-preservation
candidate pins are unchanged.

- **New worktree submodule init:**
  ```
  git -c protocol.file.allow=always submodule update --init
  ```
  The submodule origin is that local path.
- **The ROM:** each worktree needs `Super Metroid (Japan, USA) (En,Ja).sfc` at
  its root. Copy it from `_wt-sm-qol-mods`.

### Engine commits on `feat/sm-qol-mod-hooks`
```
2c349ad Show a title's built-in Mods and the package catalog on one page
20f0e1f Parse mod_hook and mod_hook_target in both cfg parsers
f22323d Make mod_hook and target PCs value barriers in both analyzers
299a83a Emit armed mod_hook calls, redirect gotos and per-program hook table
64a0c87 Add mod_hook runtime and single pre-opcode hook removal
89cba76 Test and document mod_hook
e7f768e Build mod_hook runtime into every runner
923ab81 Add mod input transforms applied before netplay, replay and run-ahead
47f010b Define mod input transform helpers outside the lobby-client build gate
```

### SM commits on `feat/qol-feature-mods`
```
be5b13b Ship gameplay mods as engine packages; add faster elevators
9f24a3c Add no-shinespark-drain and wider X-ray mods; pin engine with mod_hook
3fdfdd0 Add missile/save station refill mods; fix two-option reads in plugins
72b64d1 Add half-drain option to the shinespark mod
1492159 Pin engine with mod input transforms
147ed92 Add Zero Mission shoulder and right-stick weapon select controls mods
25ffbf3 Add mod overlay layer; draw the right-stick weapon ring
7690b68 Add pause play-time/item-percent and ammo-total HUD mods
d570273 Add laser sight HUD mod
76d912d Stop the laser sight at solid blocks
75519db Add reveal-whole-map mod
745eae6 Add pacing mods (intro/Ceres/credits skips, doors, item pickup, shake, return to title)
6740f29 Merge branch 'feat/qol-pacing' into feat/qol-feature-mods
96b19f3 Add SM_WRAM_PROBE per-frame WRAM log
edb3f8e Add super-metroid.movement mod package
```

## 4. Mechanisms

Engine policy (`snesrecomp/docs/MOD_PACKAGES.md`, `MOD_AUTHORING.md`):

- Never recompile a patched ROM.
- Reimplement hack behaviour as trusted C plugins plus package manifests:
  `[[feature]]`, `[[option]]`, `[[plugin]]`, `presentation_only`.
- Hack IPS and ASM files are a specification only.

| Mechanism | Where | Use |
|---|---|---|
| `mod_hook <pc16> <Fn>` / `mod_hook_target <hook> <target>` | cfg directives; runtime `runner/src/mod_hook.{h,c}`; doc `docs/MOD_HOOK.md` | Mutating hook before a guest instruction, in both tiers (compiled AOT and interpreter). Armed and disarmed at run time with `snes_mod_hook_arm(pc24, bool)`. Signature: `uint32 Fn(CpuState*, uint32 pc24)`. Return 0 to continue, or a declared target to redirect there. The callback may change A/X/Y/D/DB/`_flag_*`/memory, but never M/X/E/S/PB. |
| `mod_operand <pc16>` (**replaces rejected `live_operand`**) | cfg directive; runtime `runner/src/mod_operand.{h,c}` | Mod-owned value for one declared immediate (LDA/LDX/LDY/ADC/SBC/AND/ORA/EOR/CMP/CPX/CPY/BIT). Set with `snes_mod_operand_set(pc24, value)` and clear with `snes_mod_operand_clear(_all)`. AOT emits `snes_mod_operand(pc24, stock)`; the interpreter substitutes the operand on fetch. **The ROM image is never written.** |
| Input transforms | `snes_mod_register_input_transform` (`runner/src/mod_runtime.*`) | Gameplay pad remaps applied before the netplay exchange, replay recording and run-ahead. Plugins also get right-stick axes and triggers through `SnesModInputContext`. |
| Overlays | `src/mods/sm_mod_overlay.{h,c}` (SM); composed in `src/main.c` `SmDrawFrame` | Host-drawn, presentation-only layers: HUD readouts, laser sight, weapon ring. |
| Provider compose | engine `runner/src/mod_provider_compose.{h,c}` | SM's built-in Mods (widescreen, FPS) and the package catalog shown on one Mods page. |
| `SmModOption(SmModValue *out, pkg, feature, option, fallback)` | `src/mods/sm_mod_support.{h,c}` | Reads a feature option into a caller-owned buffer. A shared static buffer broke plugins that read two options. |
| `SmModArmHooks` | `src/mods/sm_mod_support.c` | Arms a list of hooks; a reset callback disarms them. The generated declarations own the flags; there is no separate title-specific capacity limit. |
| `SmModReadRom16` | `src/mods/sm_mod_support.c` | Read-only cartridge access for stock PLM scripts. ROM-writing helpers have been deleted (6.1). |
| SMREPLAY 3 | `src/sm_replay.c` | The replay header carries `MODS %08x`, a hash of the effective mod set. A reader rejects a mismatch. `tools/replay_diagnostic.py` understands format 3. |

**Mod state.** The file is `<build>/mods/preloaded/state.toml`, holding
`[[package]]` and `[[feature]]` entries with `enabled = true`, plus a
`[feature.values]` table for options. Option values go in `[feature.values]`,
not `[[option]]`. `_scen/mkstate.py` writes it:
```
mkstate.py OUT pkg/feature[:opt=val,...] ...
```

**Symbols.** Use the snesrev decomp at
`F:\Projects\snesrecomp\SuperMetroidRecomp\refs\snesrev-sm\src`:
- `variables.h` holds WRAM names.
- `sm_*.c` holds routines, one file per bank.
- Every hook must sit on an instruction start; check it against the decomp.

## 5. Feature catalogue and status

Bead IDs are children of epic `beads-8wg.6.33`. "Done" means implemented and
measured headlessly. Owner notes now record ten Pass results and two visual
failures; the post-review fixes above await only those two repeated checks.
Those current observations supersede older pending-review wording below.

### `super-metroid.pacing` (`mods/preloaded/packages/super-metroid.pacing/1.0.0`)
| Bead | Feature | Status / evidence |
|---|---|---|
| .3 | Intro skip, Ceres skip, credits skip | Current runtime gates pass: matched English intro gameplay9304→3452; Japanese stock/mod WRAM identical; Ceres skip starts on Zebes1882 vs Ceres9274. Staff roll8306→1 after Start, with native pose/final sequence retained. Normal/alternate full WRAM matches. See `docs/validation/SM_CINEMATICS_20261028.md`. |
| .4 | Faster doors (2x/max) | Done. Left 106/60/54, down 98/56/38, up 97/55/49, right 136/90/84 frames for stock/2x/max. Real Tourian grey gates also pass: 121/106 stock, 75/60 at 2x, 69/54 at max. Full WRAM/observed-frame tier equality. |
| .5 | Faster elevators (2x/3x) | Done. Real Zebes down/up trips: stock651/641, 2x407/370, 3x326/280 frames; same heights/SRAM and tier equality. Ceres ride is 308 frames shorter at 3x. Uses 8 `mod_operand` sites in bank23 (`$A3:958B, 9594, 95A6, 95AF, 95C8, 95D1, 95E2, 95EB`). |
| .6 | Item pickup: cut every jingle, short expansion sounds, no freeze, skip messages | Owner clarified CUT every jingle, including majors/first ammo. mute-jingle overrides short sounds. Nine collectors + both suits pass56 fixture and8 real-room cases on Windows/Linux, normal/alternate Windows equality,216 cross-host WRAM matches, SRAM preserved. Real room tracks3/5/5/3 continue through Varia/Gravity/first missile/reserve pickups. Legacy pacing/stock traces/25 CTests/netplay setting checks pass. Native suit-animation scope remains pending. See `docs/validation/SM_PICKUP_SOUNDS_20261028.md`. |
| .7 | Reduce screen shake (off/half) | Converted to hooks at `$A0:86A5` / `$86:843C` plus operand `$A0:8705`; measured 3/2/0 px for stock/half/off, with unchanged timers. |
| .8 | Ending return; quit to file select during gameplay | Both implemented independently. Native ending: Start during pose is ignored; final-screen Start returns to title only when enabled, SRAM unchanged, normal/alternate full WRAM matches. Pause Options quit passes native transition, SRAM, fade-save/late-load, run-ahead2 and interpreted file-select checks. See cinematic and quit validation docs; owner review pending. |

### `super-metroid.movement`
| Bead | Feature | Hook sites / evidence |
|---|---|---|
| .9 | Respin | `$91:81A9`, `$91:FC99`. Fall pose 29 → spin pose 19, with y and y-speed identical to stock. |
| .10 | Shinespark steering (strength 0.5/1/2 px), optional Down morph / Jump cancel | Steering uses `$91:FADB` and D0E6/D0EC/D0EF/D0F2. Optional spark exits use D0AB/D0D7/D106 and native transition C2. Three directions, feature-off/item guards and tier parity pass; see `docs/validation/SM_MOVEMENT_EXTRAS_20261028.md`. |
| .11 | Keep momentum on landing (option: keep blue boost) | `$91:F030`, `$90:A3D0`. +40 px after 22 frames. |
| .12 | Speed booster in morph ball (option: hold run) | `$90:9771`. 441 vs 235 px over 80 frames. It can't turn blue by itself. |
| .13 | Quick morph (hold frames, default 12; item cancel + Down; optional L+Down air morph) | `$91:81A9`; counter `$0B28`. Air gesture is recorded ordinary input and works with shoulder remapping/custom item cancel. Actual jumping/spinning/descending/left-facing and tier checks pass. |
| .14 | Space jump timing (falling/anytime) | `$90:A488`. |
| .15 | Instant bomb / no bomb jump (Down held) | `$90:BFFE`, `$A0:984B`. |
| .16 | Store shinespark | `$91:F7B0`, `$91:DB08`, `$90:D036`; flag in `$0B3A`. |
| .17 | Heavy physics (Redux Heavy Physics v3) | Converted to 15 consumer hooks plus `mod_operand` `$90:910F`. All 69 old runs accounted for, including 67 typed demo records. Launch speed 4 → 6; air gravity `$1C00` → `$3400`; shorter airtime. See 6.1 for raw traces. |

All three formerly skipped extras are implemented as separate options (default
off). Their automated checks pass; the owner's movement, air and heavy-physics
checks are Pass. Retain those acceptances without repeating the setups.

The WRAM addresses `$0B28`, `$0B3A`, `$0B4E`, `$0B50` and `$0B52` have no
definition in the decomp, so the plugin uses them as spare memory.

### `super-metroid.combat`
| Bead | Feature | Status |
|---|---|---|
| .22 | Shinespark health drain off/half | Nine actual-flight cases pass: 12-frame drain12/6/0 for stock/half/none, horizontal/vertical/diagonal. Native trajectories/collisions stay identical; full WRAM matches alternate execution, SRAM unchanged. See isolated feature validation. |
| .18 | Screw attack breaks shot blocks and frozen enemies | Done (`feat/qol-combat` 6e80b43). Stock stops at x $4BB at the shot-block wall; mod passes to $519, block breaks then respawns. Frozen enemy dies on contact. |
| .19 | Charge-beam item magnet (Ancient Chozo tractor beam) | Done. Drop moves 2 px/frame toward Samus and is collected. |
| .21 | Raise bomb limit (count option + "frames between bombs" option) | Done. Stock peaks at 4 because the 16-frame inter-bomb wait is the real cap; mod reaches 10 (or 6 at limit 6). |
| .23 | Charge combos via Select (Redux) | Six exact-input cases pass with auto charge OFF: native held X builds charge, X+Select spends1 PB and creates4 special projectiles while retaining weapon0. Low charge/no ammo preserve stock full WRAM; all normal/alternate snapshots match. No charge-result injection. |
| .20 | Auto charge | Implemented to the owner's definition. Stock/auto/no-Charge-Beam and native/Select combos pass in both paths; four taps give charged/normal/normal/charged after waiting, no idle shots. See `docs/validation/SM_COMBAT_20261028.md`. Owner playtest pending. |

### `super-metroid.controls`
| Bead | Feature | Status |
|---|---|---|
| .24/.25 | Zero Mission/Fusion shoulder aim; missile hold/toggle/off | Done. Input transforms; uses the guest config words `$09B8`–`$09BE`. |
| .27 | Right-stick radial weapon menu | Done. Input transform plus ring overlay. |
| .26 | Falling aim-down fix | Owner deferred definition until the end; ask at final playtest handoff. |

### `super-metroid.map-hud`
| Bead | Feature | Status |
|---|---|---|
| .31 | Game time and item % on pause | Done. Overlay; uses the game's `CalcItemPercentageCount` formula. |
| .32 | Maximum missile/super/PB capacity on pause | Owner rejected duplicate gameplay counters. Gameplay mode and Always control removed; pause readout shows maximum capacities only. Native gameplay pixels and full WRAM match feature-off, SRAM unchanged; Windows/Linux affected tests pass. |
| .33 | Laser sight (diagonal/always) | Done. Overlay; pose table `$91:B629` and muzzle offsets `$90:BA56`; the line stops at solid blocks. |
| .34 | Reveal whole map (secrets option) | Done. Hooks `$80:85B1`, `$82:947F`, `$82:94EF`. |
| .2 | SM world model (`src/mods/sm_world.c`): rooms, doors, cap colours, one-ways, items, explored bits from ROM/WRAM | Read-only model: 255 rooms, 595 door records, 310 states, 414 markers, 176 caps, all 100 items. Native Mother Brain escape spawn verified; eight headers have no physical source in any level state. Physical reciprocity preserves sand one-way arrows; eye doors are orange. Windows/Linux tests and seven actual map comparisons pass. Falcon Zero overview inspected; owner readability judgment pending. |
| .28 | Zero Mission-style pause map (Falcon Zero reference; Redux DCMap + ItemCircles) | Implemented; same-input guest trace/full WRAM stays stock-identical while composed image changes. Native scrolling/tabs retained. Owner review pending; see `docs/validation/SM_MAPS_20261028.md`. |
| .29 | Zoomable whole-world map (Symphony of the Night style) from pause | Implemented. X opens; L/R zoom; D-pad pans; Y centres; X/B back. Seven map cases match observed fields, full WRAM and composed images across normal and interpreted surrounding gameplay; discovery/SRAM unchanged. Owner review pending. |
| .30 | Minimap overlay (live / pauses game; the pausing variant is gameplay and off in netplay) | Implemented. Select+Start toggles; compact/wide options. Live clocks advance; paused clock/Samus/enemies stay still while interrupts advance; movement resumes on close. Native tests prevent freezing in netplay and close UI on restore. Main gameplay is intentionally LLE; coverage limit documented. |

### `super-metroid.balance-fixes`
| Bead | Feature | Status |
|---|---|---|
| .39 | Wider X-ray (wide/widest, aim speed) | Ten actual-input cases pass: both facings, half-width10/18/26, step1/2, both end limits and native close/unfreeze. Full WRAM/bitmaps match alternate execution; SRAM unchanged. |
| .35 | Missile station refills supers and PBs | Done. Hooks `$84:8CD2`→8CDA, B2EE→B2F0, B321→B323, AEC5→AEC7. Hook `$85:8311` supplies the complete AMMO RELOAD row while stock code copies it into WRAM. Runtime check refills 5/3/2 ammo versus stock 5/0/0 and validates the surrounding spaces. |
| .36 | Save station refills everything | Real room93D5/contact/prompt/Yes/Save Completed passes. Mod refills health/reserve/missiles/supers/PB499/300/50/15/10, stock retains depleted values. Saved resource words and native/mirrored SRAM checksums validate; full SRAM/WRAM match alternate execution. |
| .37 | Leave Tourian before Mother Brain | Automated traversal passes: natural baby drain, recovery, both grey gates to DC65/DC19; re-entry selects DCDD, music 1E/5, nine corpse population entries. Stock stays locked. Complete selector interpreted path matches; a narrower mixed-path engine boundary failure remains tracked separately. See `docs/validation/SM_COMBAT_20261028.md`. Owner playtest pending. |
| .38 | Power bombs reveal hidden blocks | Done (in the WIP commit). Hidden super-missile blocks revealed at f10398; stock stays hidden. |
| .40 | Vanilla bug fixes, one toggle per fix (Redux `vanillabugfixes.asm`) | 13 vanilla + 6 reserve options have boundary regressions. Actual jump/contact/refill, screw menu, manual reserves, X-ray, exact pause race, suit pickups, full new reserves, valid sprite clear, eye cleanup, native wall jump and bomb-block contexts are recorded in the final checkpoint with tier limits. Nonzero ghost-slot and native Speed Booster sound-call comparisons pass. Ship-save now saves before visible confirmation: Yes/No, all three files, default/off controls, normal quit persistence, current-build state reload and Windows/Linux checks pass; see SM_SHIP_SAVE_20261028.md. Sound quality/palette owner judgment remains. Correctness fixes default on; elective full tanks/manual-control default off. 25 CTests pass on both platforms. |

Full-hack content packs (`.41`) are deferred indefinitely.

## 6. Work in flight

### 6.1 ROM-write removal checkpoint (2026-10-28)

**Engine complete**, commit `a4f3674f` on `feat/sm-qol-mod-hooks`, following
portable test-plumbing commit `77a03539`. The dynamic operand registry is sized
from declarations and keyed by physical cartridge offset. Generated
`mod_operands_v2.c` registers and deduplicates sites, including mirrors. Both
parsers reject `live_operand`; invalid/non-immediate sites are refused.
Compiled code and interpreter fetches use mod-owned values without ROM writes.
The optional interpreter word-bus path is covered too. No directive leaves
unrelated generated banks unchanged.

Engine gates: 615 Python v2 tests; Rust 112 library + 7 binary tests; full
ROM-free C gates on Windows and Ubuntu/WSL, all successful. The POSIX account
secret fixture is qualified on Linux and skipped on MinGW. Optional firmware
fixtures remain optional. Tests cover all 12 supported opcodes, both widths,
aliases, clear/reset, immutable ROM and 1,100 active operand sites. Details:
`snesrecomp/docs/validation/MOD_OPERAND_20261028.md`.

**SM implementation converted** on `feat/qol-feature-mods`:

- All 34 cfg immediate sites/callers use `mod_operand`.
- Screen shake: mod-owned room/projectile offsets at `$A0:86A5` / `$86:843C`;
  the original timing/guards remain. Off also uses operand `$A0:8705`.
- Station text: `$85:8311` changes the loaded tile before the stock WRAM store
  and DMA. Its odd-address source span includes all letters/trailing spaces.
- Steering: `$91:FADB` selects the diagonal dispatcher in A, preserving the
  stock table. Existing movers handle the original directions and nudges.
- Heavy physics: 15 consumer hooks and the terminal-speed operand. The 69
  historical byte runs are accounted for as typed physics values and 67 timed
  demo-input records. All 25 stock demo objects were traversed to establish
  record boundaries; there is no generic ROM-address overlay.
- ROM write/guarded-patch helpers are deleted. The renamed `SmModReadRom16`
  only reads stock PLM data. Reset clears operands and the declaration-sized
  hook table, removing the old 64-hook cap.
- Engine/UI gitlinks and `framework_pins.txt` agree. Plugin sources/headers
  now contribute to replay/snapshot/build identity.

Full supported regeneration with `--strict-idempotent` produces identical
output twice; Windows build and 20 CTests pass. The 3,200-frame trace with
everything disabled matches the original build exactly:
`66976992eb0baea5b7c6696cd6f8e0def597797cb8880497204a58ac93cfc947`.

Runtime regression tool: `tools/test_qol_rom_removal.py` uses fresh disposable
profiles, muted/dummy video/audio and actual stock routines. It checks the
disabled trace, jump physics, three quake types and unchanged timers, station
refill/full text row, and all three steering directions. Raw evidence is under
`G:/Temp/Codex/sm-qol-20261027/rom-removal-runtime-v2/`; supporting conversion
audit, first probes and all-current-feature activation logs are adjacent.

**Historical remaining list, superseded by section9:** this early conversion
checkpoint preceded the combined available-tier, Linux and netplay gates now
recorded in the final checkpoint. The active burndown tracks the later cut-all
pickup/ship-save additions and owner review. No main merge, code push or release.

### 6.2 Combat + balance-fixes integration checkpoint

Codex integrated both stopped commits as `bf3d0f8` and `cb79ac3` on
`feat/qol-feature-mods`, preserving all ROM-write removal. Combined strict
regeneration, Windows build, 20 CTests, unchanged disabled stock trace and
all 36 features activating together pass. Auto-charge and Tourian now have
reproducible compiled/interpreted gameplay fixtures. Evidence and the remaining
mixed selector-boundary caveat are in `docs/validation/SM_COMBAT_20261028.md`.
Converted combat, original/extra movement, real pacing routes and vanilla/reserve
contexts now have combined-build qualification in the final checkpoint. Current
Windows/Linux builds pass 25 CTests and all 40 features activate. The historical
stopped-branch details follow; their old task/checklist text is superseded by
the current checkpoint and prepared owner selector.

`_wt-sm-qol-combat`, `feat/qol-combat`:
- `6e80b43`: combat package (`src/mods/sm_combat_plugins.c`, cfg hooks, manifest). It was staged on its own and **never built in isolation**.
- `74a1abd`: **WIP** balance-fixes (`sm_balance_plugins.c`, `sm_bugfix_plugins.c`, cfg hooks, manifest).
- HEAD builds after a full regen. Every feature uses `mod_hook` only, with no ROM patching.
- Scenarios, probe CSVs and helper scripts: `F:\claude-tmp\smqol\cb\`. Beads .18/.19/.21/.23/.37/.38/.40 are `in_progress` with exact notes.

Historical handoff tasks (integration and combined gates above now complete):
1. Fix Tourian (.37) and validate the bug fixes (.40) (see section 5).
2. Run on HEAD (**not done since the last hook conversion**):
   - the stock-invariance hash;
   - `ctest`;
   - `regen --strict-idempotent`.
3. **Hook-table capacity resolved on the integration branch:** the local
   64-entry list is deleted. Preserve the declaration-sized flags when
   integrating combat, and repeat all-feature activation with the combined set.
4. Spot-check the agent's claims against the decomp, re-run measurements, then
   integrate into this feature branch, resolving cfg/source/manifest changes
   while preserving `mod_operand` and the removal of ROM-writing helpers.

Historical agent checklist (most mechanics below now exercised automatically):
- screw attack still selectable in the pause menu;
- X-ray over a respawning item;
- eye-door crash;
- Wrecked Ship ghosts;
- manual reserve refill (A to stop, moving off the button);
- reserve pause crash;
- wall-jump screw attack and bomb blocks;
- speed booster sound bug.

### 6.3 Cross-repo removal: corrected scope and validation

See the controlling checkpoint above and the per-game validation documents.
The original audit conflated generic playable-ROM patching with reviewed
resource extraction and game-owned compatibility adapters. Do not repeat that
blanket deletion.

| Area | Current result |
|---|---|
| Shared engine | Generic variant `rom_patch.c` and localization ROM writes removed where present; cartridge-ROM guarded writes rejected. Reviewed donor-resource APIs retained. |
| recomp-ui | Generic patch picker/API/tool and auto-patch prompts removed on each current pinned UI base; Custom Mods, music and native controls retained and tested. |
| F-Zero Forever | Modern courses, vehicles, BS content, host music and multiplayer preserved and validated. Existing game-specific BS/legacy-music adapters remain; not a blanket no-image-assembly migration. |
| SMW content | Abandoned IPS executable/tools removed; semantic world/audio/character packs preserved and tested. All-Stars remains outside scope. |
| SMW native co-op | Old IPS executable/tools removed; new normal-executable native co-op preserved and tested. |

All source changes are local feature-branch candidates. Push/main merge/release
remain pending. Do not remove or replace the owner's current deliveries during
the Super Metroid handoff.

## 7. Beads

Tracker: `F:\Software\beads\issues`. Run it with:
```
node "C:\Program Files\nodejs\node_modules\@beads\bd\bin\bd.js" -C F:\Software\beads\issues <cmd>
```
The `bd` shim isn't on PATH for Python.

- **Epic** `beads-8wg.6.33` "Super Metroid QoL feature mods", under `beads-8wg.6` (Game: Super Metroid).
  - `.33.1` F4 SM runtime; `.33.2` world model; `.33.3`–`.33.40` one per feature
    (see section 5); `.33.41` content packs (deferred).
- **Engine (snesrecomp meta):**
  - `beads-8wg.2.156` mod_hook;
  - `beads-8wg.2.157` mod-owned operands and ROM-write removal (engine mechanism committed; game validation recorded in 6.1);
  - `beads-8wg.2.158` input transforms.
- Every feature bead's notes hold the measurements. Features stay
  `in_progress` until the owner signs off; don't close them.
- Quit to file select is tracked in `.33.8`; movement extras in `.33.10/.33.13`;
  shared network capacity in `beads-3qv.1.20`. Do not create duplicates.

## 8. Build, run and verification recipes

Run everything under **PowerShell**: Git Bash breaks mingw gcc silently.
Write files containing backslash escapes with an editor tool, not Bash heredocs.

**Regenerate** after any cfg change:
```
C:\Users\Matthew\.pyenv\pyenv-win\versions\3.10.9\python.exe tools\regen.py
```
Add `--strict-idempotent` to check that output is byte-identical.

**Build and test:**
```
cmake --build build-mods -j 12
cd build-mods; ctest
```
Currently 25/25 pass on Windows and Linux, including controls with Release assertions enabled and
the new data-consumer overflow/text-boundary regression checks.

**Stock invariance and converted consumers.** Use the isolated runner:
```
python tools/test_qol_rom_removal.py --build <candidate> --rom <owned-rom> --out <new-evidence-directory> --baseline <original-build>
```
It preserves all existing build saves/settings and retains its raw results.
The disabled 3,200-frame attract trace must be
`66976992EB0BAEA5B7C6696CD6F8E0DEF597797CB8880497204A58AC93CFC947`.
Do not delete a build's saves to establish an initial condition. A new task
profile and `--config <profile>/config.ini` isolate all writable paths.

**Scripted scenarios:**
- **Legacy runner:** `_scen/run_scen.ps1` deletes build saves and must not be used unchanged. Use the isolated tool above or a fresh-profile runner.
- **Commands:** `wait N`, `press <buttons> N`, `poke <hexaddr> <hexword>`,
  `until16 <addr> == <val> <timeout>`, `quit`.
- **Reaching gameplay:** a new game needs about 27 rounds of `press a 8` /
  `wait 300` to reach game state 8 (Ceres, room `$DF45`, around frame 9224).
- **Pausing:** Ceres cannot pause. Jump to the Zebes landing site with
  `poke d914 2200` and `poke 998 0600`, then `until16 998 == 8`.
- **Cheats:** equipment `$09A2/$09A4`, beams `$09A6/$09A8`, ammo `$09C6`–`$09D0`.
- **Templates:**
  - `F:\claude-tmp\smqol\mv\scen\*.txt` (movement: heavy, bomb, sball, respin, steer_*, ...);
  - `_wt-sm-qol-mods\_scen\*.txt` (ceres_elevator, zebes_pause, zebes_aim*).
- **Probes:**
  - `SM_AUDIO_PROBE=<csv>`: per-frame game state and room;
  - `SM_WRAM_PROBE=<csv>` + `SM_WRAM_PROBE_ADDRS=0AFA:2,0B2E:2,...`: per-frame WRAM;
  - `SNESRECOMP_SCREENSHOT_DIR` / `_FROM` / `_TO`: PPM frames; convert with `magick`.
- **Required per feature:**
  - a stock-vs-mod numeric measurement;
  - a tier-parity run (compiled vs interpreter) for each hooked feature.
    Auto-charge, Tourian's complete selector, core vanilla/reserve routes and
    map views now have qualified comparisons. Verify actual interpreter work
    increases: a routine may already be LLE-only. Each validation document
    records exact coverage and limits.
- **Completed:** Linux build/current-build gameplay checks and real
  Windows/Linux netplay matching/mismatching effective-set checks. Exact
  evidence and execution-path limits are in the final checkpoint.

## 9. Burndown (remaining tasks, in order)

Current acceptance audit: isolated drain/Select-combo, native save-station refill
and wide/faster X-ray runtime/tier gates now pass, including actual native SRAM
checksums and X-ray bitmaps. See `docs/validation/SM_ISOLATED_FEATURE_CHECKS_20261028.md`.
Native pickup pacing, ghost, Speed Booster sound-call state, intro/Ceres and
credits/pose/ending return also have current actual-game comparisons. The new
cut-all pickup jingle passes Windows/Linux qualification. The inherited ship-save
omission is also closed: four declared hooks reorder the native save and
Save Completed message while preserving its sound/lead-in. Ten ship scenarios,
normal/alternate presentation/current-build state checks and Linux qualification
pass. All 38 WRAM and 38 SRAM snapshots, ten sampled traces and persisted saves
match Windows/Linux. This is still an active
burndown; prepared owner review does not declare the full wishlist complete.

1. **Owner review:** use `tools/sm_qol_playtest.py` and the prepared native-paused
   follow-up in `owner-visual-retest`: map transitions and auto-charge visuals
   only. Ten earlier Pass observations are retained. Save the two individual
   outcomes and any caveats; do not repeat the accepted setups.
2. **At this final handoff only:** ask the owner to define .26 falling aim-down.
   Auto charge .20 is implemented and automatically qualified. Do not guess .26.
   Pickup sound scope is decided: CUT every pickup fanfare, including majors/
   first ammo, implemented as mute-jingle. Only the separate suit-transformation
   scope remains pending; the broad wishlist is not declared complete.
3. Resolve concrete owner findings, then rerun affected checks. The narrowly mixed
   selector continuation defect remains separately tracked in `beads-8wg.2.163`;
   full-selector parity passes. Ghost nonzero-index gameplay now passes in a
   constructed unused-slot fixture; stock populations themselves use slot zero.
4. **Completed foundation:** ROM-write removal, integrated combat/bug fixes,
   source/reference map audit, real elevator/boss-door checks, movement extras,
   available-tier comparisons, Linux build and actual netplay mismatch refusal.
   Preserve stock behavior/SRAM and the exact coverage limits in the final report.
5. Keep the completed F-Zero/SMW preservation candidates intact. Full hack packs
   remain deferred indefinitely. No unrelated removal or delivery work is needed.
6. **Owner-gated:** code push, main merge and release. None has been performed.

## 10. Traps already hit

- `SmModOption` once used a static buffer, so the second option read
  overwrote the first. It now takes a caller buffer.
- Wrong hook addresses (mid-instruction):
  - missile station `8CD4` → correct `8CD2`;
  - X-ray CMP `878F` → correct `8791`.

  Always check hook PCs against the decomp.
- `ModTransformInputs` was defined inside the `SNES_HAS_LOBBY_CLIENT`
  region, causing a link error. It now sits after `static int g_mods_ready;`.
- The engine merge must add new runtime `.c` files to `runner/runner.cmake`
  (`mod_hook.c` was missed once).
- Merging `bank04.cfg` by union duplicated the function entries. Rebuild it as
  ours plus the other branch's appended block.
- Pytest needs the pyenv Python; mingw's Python lacks pytest.
- The old title hook list capped activation at 64. It is deleted; use generated declarations and never reintroduce a separate fixed title limit.
- Headless `--script` releases every button for one frame between inputs, so
  held-charge or held-button tests need a poke or a forced state.
- `PowerShell Remove-Item` on a variable path can be blocked as a "system
  path". Use fresh directories.
