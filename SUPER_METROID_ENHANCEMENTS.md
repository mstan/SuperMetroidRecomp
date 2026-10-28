# Super Metroid enhancements: per-feature opt-in mods

Working handoff, updated 2026-10-28. Codex is working solo on the feature burndown; the old combat agent remains stopped. It records the plan, the owner's rulings, the
current state of every branch, the work in flight and what is left, so another
agent (Codex or Claude) can resume without the original conversation.

## 1. Goal

Owner follow-up at Codex takeover (2026-10-27): the original wishlist and
exact Falcon Zero image are retained in
[`docs/SM_QOL_TESTER_REQUEST.md`](docs/SM_QOL_TESTER_REQUEST.md) and
[`docs/reference/metroid3_map.gif`](docs/reference/metroid3_map.gif).
Auto charge is defined: charge automatically while idle, tap to release a charged shot, then fire normal successive shots until another charge is ready. The owner deferred falling aim-down clarification until the final handoff.

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

**Super Metroid conversion checkpoint (2026-10-28).** The engine operand
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
| `F:\Projects\snesrecomp\_wt-sm-qol-combat` | SuperMetroidRecomp | `feat/qol-combat` | `edb3f8e` | Combat + balance-fixes. **Stopped at handoff**: `6e80b43` (combat, done) + `74a1abd` (WIP balance-fixes). Not merged. Build dir `build-combat`. See 6.2. |

The SM `snesrecomp` submodule is pinned to engine commit `a4f3674f`. That
commit exists only in the local engine repo, `F:\Projects\snesrecomp\snesrecomp`,
which shares its object store with the engine worktree. `recomp-ui` is pinned to `e4d8762`, and `framework_pins.txt` matches the gitlinks.

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
measured headlessly; the owner hasn't playtested any feature yet.

### `super-metroid.pacing` (`mods/preloaded/packages/super-metroid.pacing/1.0.0`)
| Bead | Feature | Status / evidence |
|---|---|---|
| .3 | Intro skip, Ceres skip, credits skip | Done. Gameplay starts at frame 2553 instead of 9225 (re-measured). Credits 12728 → 4998 frames. The ending pose is kept (owner ruling). |
| .4 | Faster doors (2x/max) | Done. Left 106/60/54, down 98/56/38, up 97/55/49, right 136/90/84 frames for stock/2x/max. Max is close to 2x because larger scroll steps garbled tile rows and columns. Elevator and boss doors not yet run headlessly. |
| .5 | Faster elevators (2x/3x) | Done. The Ceres ride is 308 frames shorter at 3x. Uses 8 `mod_operand` sites in bank23 (`$A3:958B, 9594, 95A6, 95AF, 95C8, 95D1, 95E2, 95EB`). |
| .6 | Item pickup: short jingle, no freeze, skip message boxes | Done. Freeze goes 565 → 60 → 0 frames. |
| .7 | Reduce screen shake (off/half) | Converted to hooks at `$A0:86A5` / `$86:843C` plus operand `$A0:8705`; measured 3/2/0 px for stock/half/off, with unchanged timers. |
| .8 | Return to title from the mission-complete screen | Done. **TODO:** "quit to file select" action from gameplay. |

### `super-metroid.movement`
| Bead | Feature | Hook sites / evidence |
|---|---|---|
| .9 | Respin | `$91:81A9`, `$91:FC99`. Fall pose 29 → spin pose 19, with y and y-speed identical to stock. |
| .10 | Shinespark steering (strength 0.5/1/2 px) | Hook `$91:FADB` supplies dispatcher `$D0D7` in a register; D0E6/D0EC/D0EF/D0F2 supply the movers. Horizontal, vertical and diagonal scenarios observe steering movement; no table repoint. |
| .11 | Keep momentum on landing (option: keep blue boost) | `$91:F030`, `$90:A3D0`. +40 px after 22 frames. |
| .12 | Speed booster in morph ball (option: hold run) | `$90:9771`. 441 vs 235 px over 80 frames. It can't turn blue by itself. |
| .13 | Quick morph (hold frames, default 12; item cancel + Down) | `$91:81A9`; counter in WRAM `$0B28` (unused by the game). |
| .14 | Space jump timing (falling/anytime) | `$90:A488`. |
| .15 | Instant bomb / no bomb jump (Down held) | `$90:BFFE`, `$A0:984B`. |
| .16 | Store shinespark | `$91:F7B0`, `$91:DB08`, `$90:D036`; flag in `$0B3A`. |
| .17 | Heavy physics (Redux Heavy Physics v3) | Converted to 15 consumer hooks plus `mod_operand` `$90:910F`. All 69 old runs accounted for, including 67 typed demo records. Launch speed 4 → 6; air gravity `$1C00` → `$3400`; shorter airtime. See 6.1 for raw traces. |

Skipped extras:
- Ascent's spark down-to-morph and jump-to-cancel.
- Ancient Chozo's L + Down morph in the air.

The WRAM addresses `$0B28`, `$0B3A`, `$0B4E`, `$0B50` and `$0B52` have no
definition in the decomp, so the plugin uses them as spare memory.

### `super-metroid.combat`
| Bead | Feature | Status |
|---|---|---|
| .22 | Shinespark health drain off/half | Done. "Off" raises `CMP #$001E` to `#$7FFF` at `$90:D0C9/D0F8/D124` (`mod_operand`). "Half" hooks the DEC at `$90:D0CE/D0FD/D129`. |
| .18 | Screw attack breaks shot blocks and frozen enemies | Done (`feat/qol-combat` 6e80b43). Stock stops at x $4BB at the shot-block wall; mod passes to $519, block breaks then respawns. Frozen enemy dies on contact. |
| .19 | Charge-beam item magnet (Ancient Chozo tractor beam) | Done. Drop moves 2 px/frame toward Samus and is collected. |
| .21 | Raise bomb limit (count option + "frames between bombs" option) | Done. Stock peaks at 4 because the 16-frame inter-bomb wait is the real cap; mod reaches 10 (or 6 at limit 6). |
| .23 | Charge combos via Select (Redux) | Done. Select fires the combo, 1 PB spent. Headless test forced the charge because scripts release all buttons for a frame between inputs. |
| .20 | Auto charge | Defined by owner: charges as if Shoot were held while idle; one tap releases the charged shot, repeated taps are normal until it charges again. Implementation pending. |

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
| .32 | Missile/super/PB totals (pause/always) | Done. Overlay. |
| .33 | Laser sight (diagonal/always) | Done. Overlay; pose table `$91:B629` and muzzle offsets `$90:BA56`; the line stops at solid blocks. |
| .34 | Reveal whole map (secrets option) | Done. Hooks `$80:85B1`, `$82:947F`, `$82:94EF`. |
| .2 | SM world model (`src/mods/sm_world.c`): rooms, doors, cap colours, one-ways, items, explored bits from ROM/WRAM | TODO, needed by .28–.30 |
| .28 | Zero Mission-style pause map (Falcon Zero reference; Redux DCMap + ItemCircles) | TODO |
| .29 | Zoomable whole-world map (Symphony of the Night style) from pause | TODO |
| .30 | Minimap overlay (live / pauses game; the pausing variant is gameplay and off in netplay) | TODO |

### `super-metroid.balance-fixes`
| Bead | Feature | Status |
|---|---|---|
| .39 | Wider X-ray (wide/widest, aim speed) | Done. Uses `mod_operand` `$88:8791`, `$88:8799`, `$88:87F7/8819/884F/887A`. |
| .35 | Missile station refills supers and PBs | Done. Hooks `$84:8CD2`→8CDA, B2EE→B2F0, B321→B323, AEC5→AEC7. Hook `$85:8311` supplies the complete AMMO RELOAD row while stock code copies it into WRAM. Runtime check refills 5/3/2 ammo versus stock 5/0/0 and validates the surrounding spaces. |
| .36 | Save station refills everything | Done. Hook `$84:8CF1`. |
| .37 | Leave Tourian before Mother Brain | **Partial** (WIP 74a1abd). The boss bit is set after the drain (f11954) but the grey door still doesn't open (Samus blocked at x $3BB). Next: check whether the grey-door hook at `$84:C794` fires (probe the PLM id, room argument and lock type after the room loads); if it does, check the door's per-frame check at `$84:BDF2`; then re-enter the rooms to test the music and enemy-list changes. |
| .38 | Power bombs reveal hidden blocks | Done (in the WIP commit). Hidden super-missile blocks revealed at f10398; stock stays hidden. |
| .40 | Vanilla bug fixes, one toggle per fix (Redux `vanillabugfixes.asm`) | **Built, untested** (WIP 74a1abd). 12 vanilla fixes + 6 reserve-tank fixes, one option each. The ship-save fix can't be done with hooks. The source's "known bugs" list has no fixes to port. Next: run `F:\claude-tmp\smqol\cb\scen\{reserve_auto,sbjump,iframes}.txt` stock vs mod. |

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

**Still owed:** tier-parity qualification for every feature, the full feature
burndown, Linux game build, netplay mismatch check and owner playtesting.
The engine's isolated compiled/interpreter operand tests already pass; that
does not replace game-level parity checks. No main merge, code push or release.

### 6.2 Combat + balance-fixes branch (stopped at handoff)

`_wt-sm-qol-combat`, `feat/qol-combat`:
- `6e80b43`: combat package (`src/mods/sm_combat_plugins.c`, cfg hooks, manifest). It was staged on its own and **never built in isolation**.
- `74a1abd`: **WIP** balance-fixes (`sm_balance_plugins.c`, `sm_bugfix_plugins.c`, cfg hooks, manifest).
- HEAD builds after a full regen. Every feature uses `mod_hook` only, with no ROM patching.
- Scenarios, probe CSVs and helper scripts: `F:\claude-tmp\smqol\cb\`. Beads .18/.19/.21/.23/.37/.38/.40 are `in_progress` with exact notes.

Before merging into `feat/qol-feature-mods`:
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

Human playtest checklist from the agent (things automation couldn't judge):
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
- **TODO:**
  - beads for each 6.3 removal;
  - a bead for "quit to file select";
  - a bead for the movement extras.

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
Currently 20/20 pass, including controls with Release assertions enabled and
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
    This is still owed for every feature. Use the interp-oracle deny-gate
    recipe in the engine docs.
- **Not yet done:**
  - a Linux build (`tools/build-linux.sh`; `REQUIRED_MOD_MANIFESTS` lists each package);
  - a netplay effective-set mismatch check.

## 9. Burndown (remaining tasks, in order)

1. **Engine operand mechanism implemented/committed and SM writes converted** (6.1). Complete the remaining game-level qualification with the final feature set.
2. Preserve the ROM-write removal while integrating further features.
3. **Finish `feat/qol-combat`** (Tourian door, validate bug fixes, hook-table capacity, invariance/ctest/idempotent) and integrate into this feature branch (6.2). No main merge is authorized.
4. **Map work:** world model (.2) → Zero Mission pause map (.28) → zoomable
   world map (.29) → minimap (.30).
5. **Pacing:**
   - quit to file select from gameplay;
   - elevator and boss-door headless checks.
6. **Movement extras:**
   - spark down-to-morph;
   - spark jump-to-cancel;
   - L + Down air morph.
7. **Tier-parity runs** for all hooked features; Linux build; netplay effective-set check.
8. **Automated playtest pass** over every feature. Then write the owner a
   short, focused checklist of what automation cannot judge (feel, visuals).
9. **Cross-repo removal preservation gate complete** (6.3); keep the validated modern loaders/music/multiplayer candidates intact.
10. **At final handoff only:** ask the owner to define .26 falling aim-down. Auto charge .20 is already defined and needs implementation.
11. **Owner-gated:** push and merge of the engine and SM branches.

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
