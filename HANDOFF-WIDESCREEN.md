# Super Metroid widescreen / seamless recovery goal

## Objective and current disposition

Finish the requested exposed widescreen and seamless-room feature, starting by
fixing the five failures the owner reported during human validation on
2026-11-06. The owner rejected the review and requested that this session park
its work on a branch and provide this new-session goal. Do not count the feature
as complete, merge it, publish v0.4.3, or deliver the candidate before fixing and
validating the failures. Resume implementation in the new session; this parking
session intentionally made no additional gameplay fixes.

Owning feature: **beads-8wg.6.44**, reopened for the parked work. Five P1 bugs
below are open under the existing Super Metroid game epic. The current burndown
is **five failed human checks**, plus still-unverified human netplay/rewind.
Automated green checks are supporting evidence, not owner acceptance.

## Worktree and branch

- Work here: `G:/Temp/Codex/supermetroid/exposed-rooms-20261105/source`.
- Branch: `feat/exposed-widescreen-rooms-20261105`.
- Remote: `git@github.com:mstan/SuperMetroidRecomp.git`.
- Draft, unmerged PR: <https://github.com/mstan/SuperMetroidRecomp/pull/52>.
- Functional implementation checkpoint: `ecea946`; release-preparation checkpoint:
  `072563f`. The parking commit containing this file follows them on the branch.
- Base was unified main `cfb1d6b`, not the stale ordinary checkout. Do not start
  over in `F:/Projects/snesrecomp/SuperMetroidRecomp` or replace unrelated work.
- `VERSION` is **0.4.3 planned**, not a published/delivered release.

Read current AGENTS instructions, this file, `docs/exposed-rooms.md`, the owning
Beads entries, and any new owner messages before editing. All source/test/docs
work is committed on the branch. ROMs, generated C, guarded snapshots and QA
artifacts remain private in the task workspace; they must not enter Git.

## Five failed checks

The authoritative feedback and copied screenshots are in
`G:/Temp/Codex/supermetroid/exposed-rooms-20261105/human-feedback-20261106/`.
`reports.json` contains the owner reports, acceptance criteria, screenshot SHA256s
and Beads IDs. Original ShareX files remain untouched. Owner chat is a valid
failure verdict even though the selector's observations arrays were empty.

<a id="hf-01"></a>
### HF-01 — visible hatch closes while it remains passable

**beads-8wg.6.45**, screenshot `HF-01.png`.

After a valid shot opens a hatch, it looks closed again shortly afterward but
Samus can still walk through it. The owner explicitly prefers keeping it shown
open whenever it is logically open/passable. Make both faces agree with actual
passability after opening, extended idle, approach, crossing, return and
save/load, with neighboring logic On and Off. Preserve native ammo and locks;
do not solve the mismatch by arbitrarily unlocking doors or making an otherwise
passable doorway solid merely to match a stale graphic.

<a id="hf-02"></a>
### HF-02 — entry loses/corrupts the room canvas

**beads-8wg.6.46**, screenshot `HF-02.png`.

Entering the pictured green room produces a rejected visual result: scenery
is cropped/disappears into extensive black space. Establish the actual room,
camera and authored geometry from state/ROM evidence, rather than guessing from
its palette or dismissing the report as an intentional boundary. Keep active
and adjoining scenery, foreground, sprites and camera coherent on the first
entry frame, continued movement and return. Match the owner's requested exposed
foreground behavior at screen edges.

<a id="hf-03"></a>
### HF-03 — Native transitions must not black out the world

**beads-8wg.6.47**, screenshot `HF-03.png`.

The Native transition option produces a black fade with a floating hatch and
partial scenery. The owner now explicitly requests removal of that black fade.
Keep the original scrolling/transition animation as the selectable Native
option while maintaining a coherent visible world. Test its full duration in
both directions, not just the settled endpoint. A brightness override that
exposes partially loaded garbage is not an acceptable substitute.

<a id="hf-04"></a>
### HF-04 — lava entry misplaces Samus and liquid presentation

**beads-8wg.6.48**, screenshot `HF-04.png`.

After going through the door into the pictured lava room, Samus appears on the
wrong layer/vertical position, and the owner reports that the lava looks wrong.
Preserve correct physical/world/body/camera coordinates and sprite/foreground
layering. Independently check liquid surface height, scrolling/raster effects
and room-owned palette/FX before, during and after entry/return, On and Off.
Passing the actor-position check does not clear the liquid-rendering caveat.
The fourth original setup starts in Wave Beam room ADDE and its connected
chamber; determine the actual failing destination from evidence.

<a id="hf-05"></a>
### HF-05 — the Ceres escape shaft cannot be exited

**beads-8wg.6.49**, screenshot `HF-05.png`.

The owner cannot exit the escape shaft in the fifth prepared case. Determine
whether the obstruction is at its hatch, shaft movement or elevator trigger;
do not assume the cause. From a disclosed access setup, normal controller
movement must complete the blocked route and native escape progression. Do not
teleport Samus out, inject velocity/fade timers or write successful progression
flags to claim a pass. Preserve native encounter/room-entry prerequisites.
Revalidate the actual playable route with the owner.

## Reviewed build versus later builds

The original, actually reviewed player is
`human-review1/player/SuperMetroidSNESRecomp.exe`, immutable Candidate5 SHA256:

`d7b026a7dcde8fd694cd61d98e1d1c7938c0753d244790a90ce306294c375b2c`

Its real file hash matches the original manifest. The last recorded selector
session was SMWS-05, PID47948/port13013; these are historical handles, so check
liveness before relying on them. Never attach a second TCP client to the
selector-owned connection, interrupt a live owner playtest, or overwrite its
binary, profile, states or notes. Preserve its per-case stdout/stderr logs and
`runtime/controller-commands.jsonl` along with the guarded states. The selector
explicitly disabled replay recording (`SM_REPLAY_RECORD=0`), so do not claim
that an input recording of these human failures exists.

The review uses **Fit and 120 FPS presentation**, normal audio and a normal
window. Many automated native gameplay sessions used presentation FPS Off.
Reproduce the owner configuration first. A 60-FPS/discrete A/B may diagnose
interpolation, but disabling Higher FPS, narrowing the viewport or changing
transition choice is not an acceptable fix. A tiny change to the existing
selector to retain ordinary supported replay evidence for future reviews is
reasonable; do not build another recorder or validation framework.

Later frozen players exist, but the owner has not cleared them:

- `build/SuperMetroidGrappleFinalCandidate15QA.exe`, SHA256
  `6d9228ac1c0efdcb826917ff4e6f041a1d1d8d28f5e18dd24201ba1111d60679`.
- `net-build/SuperMetroidExposedRoomsFinalQA.exe`, SHA256
  `76fb60aa81a7ebc8cd56da4f895efcdf912885a2ccdd80f34a7b89528888a71e`.
  Its six prepared cases/normal two-peer launchers are in `human-review-final1`.
- Actual Windows v0.4.3 candidate executable, SHA256
  `de5528c8442a986635217e81780a7f115ccfc900db52bf21002a55c742622d52`.

Later commits may affect these symptoms, but do not dismiss owner failures as
old or mark them resolved from commit descriptions. Reproduce/retest each on
fresh latest-build states. Do not migrate states or patch build guards: old
save-state compatibility is not required. Native SRAM/saves/settings are a
separate preservation requirement.

## Resume efficiently

1. Claim the feature/appropriate bug in central Beads. Preserve the five issues
   and their individual caveats. Read new owner observations before deciding
   what remains; do not ask the owner to assemble diagnostics already retained.
2. Build the current branch and prepare a few fresh, guarded reproductions of
   these exact failures using existing tools. Match Fit/120 FPS and the relevant
   On/Off/Native settings. Capture only the before/during/after state or frames
   needed to distinguish the failure. Derive destinations from actual ROM/state
   data; an earlier package fixture guessed the wrong destination.
3. Check whether shared room/camera coordinates or state publication explains
   several failures before accumulating local offsets/overrides. Native collision
   and visible canvas state must use coherent ownership and coordinates.
4. Implement, build and evaluate coherent fixes promptly. Reuse the existing
   native tests, controller/replay fixtures, renderer capture tool and selector.
   Stop broad room/weapon inventories, new input drivers, redundant receipt
   archives and tooling projects. Burndown means these five failures actually
   resolve, not that more tests/tools run. Reassess after two unsuccessful fixes.
5. Qualify each bug against its acceptance above, then run the relevant combined
   existing gates. Update Beads with actual results, remaining failures and exact
   source/binary identities. Keep prepared/built/tested/owner-accepted distinct.
6. Present a fresh normal-window, normal-audio review with concise setups and
   saved Pass/Fail/notes. Do not interrupt the existing playtest or repeatedly
   re-present checks the owner has actually cleared. No test in this failed
   review has been cleared. Obtain the supported two-peer gameplay and actual
   rewind-commit/input-isolation results too; cold handshake/probe success is
   not their substitute.

At most one helper was authorized for this task. Respect current session and
AGENTS limits; do not infer general delegation or add more helpers.

## Full feature contract to preserve

- Expose secrets and connected rooms; remove concealment masks. Extend authored
  foreground/hatches where appropriate. Rooms retain their own terrain, graphics,
  palettes, items and animated environmental effects, including lava/steam.
- Neighboring room logic defaults On and is disable-able. Off preserves visible
  scenery/environmental animation and working door bubbles. Active rooms retain
  normal gameplay. Valid native weapons/ammo trigger either face and synchronize
  door state, with visible/passable consistency.
- Continuous camera/world movement is default. Native transition animation stays
  available, now with the owner's no-black-fade requirement. Walls/locks and
  native physics remain meaningful.
- Preserve native combat, actor/projectile ownership/transfer, drops and collected
  state without duplicate hits/rewards. Bosses/progression helpers begin through
  actual entry. Do not remove already working features to hide the failures.
- Keep current-build save/load, replay, rewind, run-ahead, rollback and agreed
  netplay state/settings correct. No old snapshot migration; preserve native SRAM.
- Fresh releases default to adaptive Fit; preserve explicit existing player
  aspect/mod/settings choices. Validate actual packaged gameplay/defaults.
- Keep portability/fallbacks, normal game timing and usable native runtime.
  This is a feature/correctness task, not a new optimization campaign.

## Existing build and evidence

Task root: `G:/Temp/Codex/supermetroid/exposed-rooms-20261105`.
Python: `C:/Users/Matthew/.pyenv/pyenv-win/versions/3.12.5/python.exe`.
Native CMake/Ninja/CTest/GCC: `C:/msys64/mingw64/bin` (call absolute executables).

Pinned engine is `G:/Temp/Codex/supermetroid/feedback-20261103/engine-mods`
at `6ca2dbbe12f65901ae76015a15943c8254c4a85f`; UI sibling `ui-mods`
at `7fdd3a872d556758d5f178a980ecc49eb6e5c4be`. The tracked
`framework_pins.txt` also records net/rollback pins. These are the established
dependencies; no engine source changes were needed for the parked feature.

ROM: `F:/Projects/snesrecomp/SuperMetroidRecomp/Super Metroid (Japan, USA) (En,Ja).sfc`,
SHA256 `12b77c4bc9c1832cee8881244659065ee1d84c70c3d29e6eaf92e6798cc2ca72`.
Do not upload or commit it. Native decomp reference is
`F:/Projects/snesrecomp/_sm_decomp/src`; NES reference is
`F:/Projects/nesrecomp/MetroidNESRecomp/metroid_ws_actors.c`.

Supported strict generation/build helper is task-root `build_continuous.py`,
using `source/`, `gen-continuous/` and `build/`. Preserve byte-identical generated
timestamps including `recomp/funcs.h` so regeneration does not cause needless full
rebuilds. Never hand-edit generated C. Supply pinned `SNESRECOMP_ROOT` and
`RECOMP_UI_ROOT`, bounded parallelism and the existing capacity helper. Reconcile
cached build version with tracked VERSION when creating a new candidate.

`net-source` is a separate worktree at functional `ecea946`; `net-gen`/`net-build`
use Net On, Trace On and established Windows release flags. Do not mistake that
older worktree/version or its frozen states for a rebuilt current main worktree.
Do not run concurrent builds in one build directory. Background tests are muted
and use the game's interfaces rather than moving desktop windows.

Useful existing implementations/tests: `src/sm_rooms.c`, `sm_room_runtime.c`,
`sm_seamless.c`, `sm_renderer.c`, `sm_activity*.c`, `sm_room_collision_host.c`,
`sm_video.c`, `sm_mods.c`, corresponding room/door/renderer tests and
`recomp/bank14.cfg`/`bank20.cfg`. Existing tooling includes
`tools/sm_progression_qa.py`, `tools/sm_replay_review.py`,
`tools/replay_diagnostic.py`, `tools/sm_render_capture.c` and task-local qualified
controller fixtures. Native step/replay explicitly does not support run-ahead;
network sessions deliberately have no local debugger. Reuse supported unpaused
scripts and the staged human netplay/rewind path; do not bypass those guards.

Historical supporting records: `grapple-final-main1`,
`grapple-native-controller4-{on,off}`, `grapple-live-entry1-on`,
`net-final-build2`, `final-runahead1`, `final-rollback1`, `net-final-cold1`,
`items-live3`, `items-return2`, `a45c-build3`, `ceres-escape-final2-{on,off}`
and `release-native-0.4.3/qualified.json`. They do not clear the owner's five
failures. The ordinary selector observations were empty; the separate preserved
owner reports are authoritative for rejection.

## Release hold and completion

Normal four-platform build/tests passed on **072563f**:
<https://github.com/mstan/SuperMetroidRecomp/actions/runs/42249517458>.
All four audited candidate ZIPs and `SHA256SUMS` are in
`release-candidate-0.4.3/`. Windows ZIP SHA256:
`43754aefc1bc6dae32b3e7a0eed9b19113298db4af53d9bff1dc7880fdf1f87b`.
The workflow used `publish_release=false`; there is no accepted new release.
Its private generated-input archive is `release-inputs-0.4.3/generated.tar.gz`,
SHA256 `4e2ab5da2d6bea91b8a836a2ecb56f00d23ea00aa2815891bbbdcae37764c17f`.
These candidates are comparison evidence and must not be shipped unchanged on
the strength of the failed review. Rebuild/requalify the actual fixes normally.

Once all failures and remaining required checks are genuinely cleared, finish
normal PR integration and automatically cut/deliver the next release. Do not
ask for another release request. Use the established four-platform GitHub release
channel, payload audit and fresh packaged defaults/gameplay/save-load checks.
Verify the current version/tags before deciding whether 0.4.3 is still next.
Deliver the verified Windows ZIP to `G:/Nextcloud/Games/SNES`; replace the older
Super Metroid archive only after verification, leaving one current archive for
this game and preserving unrelated files, personal saves and settings. The last
observed delivered archive was `supermetroid-0.3.22-windows-x64.zip`.

Keep all unresolved bugs/caveats open. Do not substitute a narrower feature,
hide correctness behind opt-in settings, waive human failures because CI passes,
or mark the full goal complete while release/required validation remains.
If another handoff is requested, park/push the changes and accurately report
what passed, failed and remains unverified.

Retain private evidence and guarded states while this recovery is ongoing.
At actual completion, clean known agent-created disposable build/QA data using
audited, non-forced native Windows cleanup; preserve useful evidence and owner
files. The existing docs list retained local worktrees/builds and SERVER scratch
`G:/Temp/Codex/supermetroid/exposed-rooms-20261105-validation/owned-shot-probe`
(portable compiler/owned-ROM/source copies) plus local `owned-server-input.zip`.
Do not crawl drives, force locks, kill unrelated jobs or touch other sessions.
