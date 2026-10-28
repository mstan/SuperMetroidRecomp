# Super Metroid replay/video bugfix burndown

Updated: 2026-10-28. Tracker: **beads-8wg.6.34**.

Worktree: `G:\Temp\Codex\supermetroid\replay-bugfix-20261027\source`
Branch: `fix/replay-bugfixes-20261027`
Starting game commit: `e617b6573ea090e3bcab2690f4193ce5c2fa4869` (0.3.17).

## Burndown chart

**4 / 7 unique clip reports fixed and validated in the candidate; 3 remain unconfirmed/not reproduced. The owner confirms the light is fixed and earlier said the rest seemingly pass. Owner review is complete; no more routine rechecks.**

```mermaid
xychart-beta
    title "Super Metroid: unresolved clip reports"
    x-axis ["Intake", "Padding fix", "Owner findings fixed", "Stripe reported", "Stripe fixed"]
    y-axis "Reports remaining" 0 --> 7
    bar [7, 6, 3, 4, 3]
```

| Checkpoint | Total unique clip reports | Fixed and validated | Verified existing behavior / duplicate of another row | Unresolved (including deferred) |
| --- | ---: | ---: | ---: | ---: |
| Intake, 2026-10-27 | 7 | 0 | 0 | 7 |
| Room padding fix, 2026-10-28 | 7 | 1 | 0 | 6 |
| Six owner review setups, 2026-10-28 | 7 | 1 | 0 | 6 |
| Three owner-confirmed margin bugs fixed, 2026-10-28 | 7 | 4 | 0 | 3 |
| Owner finds residual eye stripe, 2026-10-28 | 7 | 3 | 0 | 4 |
| Residual stripe fixed; other checks accepted with owner caveat, 2026-10-28 | 7 | 4 | 0 | 3 |
| Owner confirms light fixed; review complete, 2026-10-28 | 7 | 4 | 0 | 3 |

Append actual milestones to this table and update the chart. Do not invent dates or predicted completion. The seven rows are evidence-backed intake items, **not seven confirmed independent defects**. Split multiple defects from one clip into linked subitems when demonstrated; preserve the original seven-row accounting. A shared fix may resolve multiple rows only after each row's symptom is checked.

## All supplied clips

Names below are exact files in `E:\Downloads`. Scene descriptions and inspection targets are provisional where the intended symptom is not yet established. The owner has not specified a separate symptom for every clip. Infer and investigate from the footage and logs; do not wait for a priority choice to start the clearest actionable item.

| ID | Work order | Clip | Length | Scene / observed candidate | Status | Next decisive check and closure target |
| --- | ---: | --- | ---: | --- | --- | --- |
| SM27-01 | 6 | [clip_1,793,140,731,658.mp4](E:/Downloads/clip_1,793,140,731,658.mp4) | 8.292 s | Plasma Spark `$D340` upper shaft: owner confirms a black left cutout when jumping and hugging the wall. | **Fixed; owner Pass recorded** | Explicit Pass saved in selector notes. Do not repeat this accepted check. |
| SM27-02 | 7 | [clip_1,793,140,528,702.mp4](E:/Downloads/clip_1,793,140,528,702.mp4) | 10.724 s | Morph Ball eye `$9E9F`: owner confirms the cone becomes a band when it extends far into the margin; owner recheck finds a thin yellow stripe at the old 4:3 edge. | **Fixed; owner confirms light is fixed** | Owner review complete. Do not request another routine check. |
| SM27-03 | 4 | [clip_1,793,140,650,331.mp4](E:/Downloads/clip_1,793,140,650,331.mp4) | 9.717 s | Cathedral `$A788`: owner confirms the lava surface bubbles remain within 4:3 while lava extends. | **Fixed; owner says rest seemingly pass** | Qualified owner acceptance retained; do not request another routine check. |
| SM27-04 | 3 | [clip_1,793,140,682,203.mp4](E:/Downloads/clip_1,793,140,682,203.mp4) | 16.136 s | Wave Beam room `$ADDE` to Double Chamber `$ADAD`, then a speed-boosted lava run. Isolated-door frames alone do not establish a phantom-door defect. | **Unconfirmed; no further bug reported in qualified owner recheck** | Shoot the left blue door (S), walk left through it, hold Left + Run (Z) and Jump (X) across gaps, then return right. Inspect transitions/lava; a specific fault remains unconfirmed. |
| SM27-05 | 1 | [clip_1,793,140,699,506.mp4](E:/Downloads/clip_1,793,140,699,506.mp4) | 17.250 s | Crocomire `$A98D`. Apparent terrain/liquid discontinuities remain a candidate; sampled original captures, adjacent interpolation and nearby dense replays did not reproduce a definite bad seam. | **Owner reviewed: nothing abnormal found** | Owner check complete. Preserve the original clip as not reproduced; do not request another routine check or claim a seam fix. |
| SM27-06 | 5 | [clip_1,793,140,666,141.mp4](E:/Downloads/clip_1,793,140,666,141.mp4) | 3.181 s | Bubble Mountain `$ACB3`; two original 600-frame candidate segments pass, but the clip movement and a definite fault have not been matched. HUD-edge noise was only a hypothesis. | **Owner could not reproduce an abnormality** | Qualified owner observation retained; this is not a demonstrated fix or a blanket Pass. Leave unresolved unless a failing behavior or explicit disposition is established. |
| SM27-07 | 2 | [clip_1,793,140,718,500.mp4](E:/Downloads/clip_1,793,140,718,500.mp4) | 14.085 s | Plasma Spark room `$D340`, lower route: white cross-pattern strip in the left margin (6.35-13.98 s). | **Fixed and validated: complete unused screens clipped** | Built traversal left, right, then left; current-build save/load; immutable before/after capture at camera `(256,1280)`; native pixels unchanged at four widths. See outcome below. |

The suggested order starts with the clearest visible candidates and may change when measurements identify a shared cause or a more consequential defect.

### Duplicate inputs

| Supplied input | Accounting |
| --- | --- |
| `clip_1,793,140,666,141 (1).mp4` | Byte-identical to SM27-06; SHA256 `bc38b2817da48686c7231a4dd7da241a39056998a7c242527adc6f3f9ecb6664`. Covered by SM27-06, not an eighth defect. |
| Concatenated/repeated `clip_1,793,140,731,658.mp4` path in the original message | One file, covered by SM27-01. |

## Owner recheck: residual eye-cone stripe, 2026-10-28

The owner reports a tiny yellow line at the right edge of the cone and says **"the rest seemingly pass"**. The shaft also has an explicit Pass in the selector's notes. Retain that qualified acceptance for the other checks; do not relaunch them for routine review. The three symptom-unknown reports remain unconfirmed/not reproduced, not source fixes. The screenshot and exact feedback are retained in [owner-eye-line-20261028/feedback.json](../../../evidence/owner-eye-line-20261028/feedback.json).

The line is the native x255 boundary (screenshot x836-838), below the real cone. A valid ROM HDMA offscreen walker saturates its window to `$FFFF` (or `$0000` on the opposite side), leaving a false one-pixel vertical strip. Commit `2511720` uses the already verified unclipped geometry at that saturated edge when a wider view is active. It preserves exact 4:3 behavior, real cone pixels, all other window effects and the existing portable fallback. This intentionally corrects the false native-border pixel in widescreen; the earlier all-native-pixels-unchanged result described the previous candidate.

Validation: **18/18 tests pass**, including native/portable/full-copy paths and mirrored saturated tails at four widths. The focused regression assertion fails before the fix. [80 immutable scene/width comparisons](../../../evidence/owner-eye-line-20261028/comparison.json) change only those false border pixels; the other scenes and every 4:3 image are unchanged. A matched diagnostic camera/position setup reproduces an 87-pixel right stripe; [before/after](../../../evidence/owner-eye-line-20261028/before-after.png). Its ROM-generated enemy/HDMA runs normally, but diagnostic camera/position placement is not an exact replay of the owner's movement. The new built native game has [zero yellow pixels at the former stripe](../../../evidence/owner-eye-line-20261028/built-candidate-check.json). New guarded state load, input, current-build save/load, selector Play/Pause/frozen-frame and screenshots pass.

Candidate binary SHA256: `2a034a88b576bf76ff6654591ef499389b5813953cbc52a8693b3272f0976f49`. The [eye-only review bundle](../../../human-review-eye-line/README.md) has one freshly prepared current-build state and **Launch eye recheck.lnk**, with normal window/audio on launch. Historical failure notes are preserved. After confirming the older selector/game had already closed, the eye-only selector was opened on SM27-02, paused in a normal window with normal WASAPI audio and input overrides released. Owner notes, native saves and settings remain preserved. The owner subsequently confirmed **"sm light is fixed now"**; [acceptance recorded](../../../evidence/owner-eye-line-20261028/owner-acceptance.json). The targeted check is complete. No more routine rechecks or launches.

## Owner-confirmed fixes, 2026-10-28

The owner supplied three annotated screenshots identifying concrete symptoms. Their [feedback and screenshot identities](../../../evidence/owner-review-20261028/feedback.json) are retained separately from the active selector's notes, so its in-memory draft autosave cannot overwrite them. The owner also could not reproduce an abnormality in Bubble Mountain. The owner subsequently reviewed Crocomire and found nothing abnormal. The owner checks for Crocomire and Bubble Mountain are complete and should not be repeated without new evidence; neither is claimed as a source fix. #4's earlier instructions were unclear and have been rewritten.

| Report | Cause and candidate behavior |
| --- | --- |
| SM27-01 | The upper filler screen borders a complete authored `$8210` wall screen below. Continue that uniform wall vertically through the adjacent filler, in margins only. Mixed screens, conflicting wall fills and chains of filler keep their conservative treatment. The lower-route matte from SM27-07 stays unchanged. |
| SM27-02 | Offscreen eye HDMA walkers saturate multiple rows to `$0000`/`$FFFF`, not just the last clamped edge pixel. The renderer rejected those valid runs and fell back to stretching a pinned window edge. Accept the correctly saturated runs while retaining all native hardware windows and using unclipped cone bounds in margins. |
| SM27-03 | Room loading uploads FX starting at VRAM word `$5BE0`; the surface row lies before the `$5C00` page boundary. Permit that row when an FX tilemap is active. Earlier HUD rows retain their opaque-black margin, and native pixels are unchanged. |

Validation: **18/18 tests pass**, including portable/native/full-copy renderer variants. New checks cover saturated cone tails on both sides at four widths, preservation of native cone pixels, active/inactive liquid surface rows, uniform wall continuation and preserving a mixed screen. Existing padding interpolation cases cover three presentation phases. [Sixty immutable scene/width comparisons](../../../evidence/owner-fix-comparisons/results.json) preserve all 49,152 native gameplay pixels per comparison. The six earlier representative captures, five additional active-eye angles and the SM27-07 lower route also have unchanged margins. At width342, the targeted captures change6,880 shaft margin pixels,3,508 cone pixels and99 liquid-surface pixels. [Before/after overview](../../../evidence/owner-fix-comparisons/before-after.jpg).

The shaft capture uses diagnostic positioning while the normal game camera settles at `(256,63)`. The cone capture uses diagnostic positioning/camera setup to isolate a real ROM-generated saturated HDMA tail; its terrain/cache placement is not an exact clip reconstruction. The lava capture runs the current-build room setup normally to its ledge. These are separate muted/private processes; the owner's active review game is not stopped or replaced. [Offline raster draw costs](../../../evidence/owner-fix-comparisons/draw-cost.json) are supporting measurements, not a claim about full-game throughput.

Fix commit: `9e547d7`. Fixed binary SHA256: `ab2020f3cdd05acb643769ef33638db921fae4b061d58916fa43f4eeffeff85c`. All six newly prepared guarded slots pass the selector load/input/release, Play/Pause, frozen-frame and screenshot checks. [Fixed-bundle manifest](../../../human-review-fixed/manifest.json), [selector checks](../../../human-review-fixed/selector-storage-check.json), [scenario overview](../../../human-review-fixed/overview.jpg).

The updated [review bundle](../../../human-review-fixed/README.md) contains fresh states matching the fixed executable; original-build snapshot identity checks remain intact. Start it with `human-review-fixed/Launch review.lnk` when ready to replace the earlier review. The selector now keeps an already paused screenshot on its exact frame. The current review bundle and all owner notes remain preserved. The later owner recheck above supersedes this initial handoff: other fixes accepted with the owner caveat, eye stripe corrected separately. SM27-04, SM27-05 and SM27-06 remain unconfirmed/not reproduced.

## Prepared owner review: original six reports

The owner requested playable setups for all six previously deferred reports. The prepared bundle is [human-review](../../../human-review/README.md), with one current-build state per report and a local selector. Start it with `human-review/Launch review.lnk`; it opens the first room paused in a normal window with normal audio. Select Keyboard or Controller, select a room, then **Load selected (paused)**. **Play**, **Pause** and **Restart** operate the private test game. Switching rooms replaces only this selector's child game.

Use **Bug found**, **Pass (reviewed)**, **Defer** or **Save screenshot + note** to save timestamped notes and a screenshot. Records live in `human-review/observations.json`; images live in `human-review/runtime/notes`. Drafts autosave. Earlier failures remain in history even after a later Pass. These are owner observations, not automatic report closure; read them before deciding follow-up work.

| Scenario | Starting room / review target |
| --- | --- |
| SM27-01 | Plasma Spark `$D340`, upper platforms: jump/drop and inspect camera edges |
| SM27-02 | Morph Ball eye `$9E9F`, cone active: walk/jump near the eye |
| SM27-03 | Cathedral `$A788`, right ledge above lava: jump across platforms |
| SM27-04 | Wave Beam `$ADDE`: exit left into Double Chamber `$ADAD`, run and return |
| SM27-05 | Crocomire `$A98D`: active boss, floor/liquid and camera edges |
| SM27-06 | Bubble Mountain `$ACB3`: middle-left entrance, jumps and drops |

The bundle uses candidate binary SHA256 `1f50d13af577326b4335e003382c23b021717ca2ed8314e01be8f8d2f5e2d7aa`, enhanced 16:9 rendering and 120 FPS presentation. Centered HUD is used for 01/04/05 and edge HUD for 02/03/06, matching the clip presentation targets. Setup grants equipment/health, bypasses reaching doors, and uses the ROM's room loader; 01 also uses diagnostic upper-platform placement followed by normal camera settling. No boss victory flags are set. These are reconstructed review setups, not exact original replay trajectories. Native saves/settings remain isolated.

Automated selector checks passed for all six: guarded current-build load, expected room, frame advancement with input, override release, Play/next-frame Pause, stable paused frame, screenshot capture and atomic JSON storage. All six screenshots were inspected. The eye remains in the earlier room state where its enemy and beam exist; the later awake state removes it. The UI's requested size fits the configured panel. These checks do not establish human visual/audio acceptance. Evidence: [manifest](../../../human-review/manifest.json), [selector checks](../../../human-review/selector-storage-check.json), [six-scenario overview](../../../human-review/overview.jpg). No new runtime fix is claimed for these six reports.

## Replay archive and baseline evidence

Local archive: `E:\Downloads\logs (2).zip`. SHA256: `71dc6d993cbb1bad78c8a6d6e5e9097a363daefa6be425fec7209c83bcc4a9d1`.

There are **1,643 replay segments across six sessions**, all identifying Linux x86_64 0.3.16, game `dc6bdeb054a79ab8ceabae525a4053576c72f644` and engine `246c0d40a8bc5f706bc6ef71d7e8633842f8e42e`. Segments are reproduction data, not additional bug counts. Scan the index and relevant playback failures for additional actionable defects and add any discovered issue explicitly.

| Session | Segments |
| --- | ---: |
| `20261026-155503-21232` | 118 |
| `20261026-161546-22582` | 360 |
| `20261027-025314-24809` | 360 |
| `20261027-045252-26404` | 85 |
| `20261027-051754-26903` | 360 |
| `20261027-162922-28928` | 360 |

Original playback uses the exact published Linux 0.3.16 runtime. Its downloaded ZIP SHA256 `e84d7a1dc19e7d807ad167c008905bb16504233957e9b93887e26264f5b74671` matches published `SHA256SUMS`.

| Selected baseline segment | Candidate scene | Recorded-checkpoint result | Evidence limit |
| --- | --- | --- | --- |
| `20261026-155503-21232-000070` | Eye room `$9E9F` | All 600 executions match | The first captured frame is an elevator/shaft view; it does not validate the cone in SM27-02. |
| `20261026-161546-22582-000170` | Norfair `$A788` | All 600 match | Similar scene/inventory to SM27-03; exact clip timing not established. |
| `20261026-161546-22582-000171` | Norfair `$A788`, later movement | All 600 match | Same limit; motion/presentation need inspection. |
| `20261026-161546-22582-000182` | Bubble Mountain `$ACB3` | All 600 match | Candidate for SM27-06, not proof that its glitch is fixed. |
| `20261026-161546-22582-000325` | Crocomire `$A98D` | All 600 match | Candidate for SM27-05; nine additional sampled capture runs also match all 600 executions. |
| `20261026-155503-21232-000117` | Shaft `$9AD9` | All 600 match | Representative shaft coverage, not a proven match to SM27-01. |

The starting worktree's portable renderer unit suite passes. Six selected captures also preserve every native gameplay pixel in rows 30-223 at widths 256, 342, 448 and 682: **24 comparisons, zero differences among 49,664 native pixels per comparison**. These checks do not certify HUD/margins, intermediate presentations, moving effects, audio, or the seven reported clips.

### Retained local evidence

Evidence is outside the source worktree under `G:\Temp\Codex\supermetroid\replay-bugfix-20261027`:

- [Clip metadata, hashes and contact sheets](../../../evidence/clips.json); sheet names are recorded per clip.
- [Room/inventory/camera replay index](../../../evidence/replay-index.json) and [archive summary](../../../evidence/archive-summary.json).
- [Six original-runtime playback results](../../../evidence/replay-results.json), [Crocomire sample results](../../../evidence/croco-samples.json), and [native pixel comparisons](../../../evidence/native-comparison.json).
- `replays/<scene>/`: selected replay sidecars, muted playback logs and immutable renderer captures.
- `release-original/`: verified original runtime and published checksum list.
- [Resource and cleanup ledger](../../../resource-ledger.json).

These links intentionally refer to this prepared local task directory. Do not commit replay states, videos, ROMs, binaries or large captures into the source repository.

## How to burn this down

1. Work solo in this worktree. Read the current chart, Beads notes and existing fixes before investigating. Start with SM27-05 or SM27-07, and reuse existing captures/tools. Exact clip-to-replay correspondence is still an investigation task.
2. For each row, identify a concrete symptom and timestamp; record the actual original/candidate source and binary identities, room, inputs, renderer/mod settings, viewport, presentation rate and reproduction steps. Prefer the game's debug interface and existing QA tools. Inspect full motion and relevant audio, not only contact sheets. If the intended symptom remains ambiguous after inspection, ask one targeted question and continue an independent row.
3. Reproduce on the original release and current candidate as appropriate. Use the exact original runtime for original-build replay states; do not add cross-version save-state migration or bypass build identity checks. Export diagnostic captures or reconstruct a current-build setup when needed. Keep native SRAM and owner settings intact.
4. Fix the generator/config/runtime source that owns the defect. Never edit generated C. Shared-engine defects belong in a separate isolated engine worktree with an owning engine Bead, linked to this intake. Regenerate fully when generation inputs change. Preserve portable fallbacks and existing working renderer behavior.
5. Validate the actual failing behavior in a built candidate with meaningful focused regression coverage. Preserve native pixel fidelity where applicable; exercise relevant motion, margins, effects, transitions and audio. Check current-build save/load where execution state changes. Reuse representative regression evidence and measure runtime impact when a change could affect frame cost. Passing the old unit suite or merely loading a room is insufficient closure evidence.
6. Update this file and Beads after each meaningful finding, fix and validation result. Record commit IDs and evidence paths. Log reproducible new defects as linked items; preserve individual failures and caveats. A blocked or unverified item stays outstanding.
7. Once automated checks pass, prepare a small bundled owner check only for genuinely remaining visual/control judgments, with a normal window, normal audio, isolated saves and concise steps. Unattended work stays muted and preserves desktop focus. Do not repeat already accepted checks or interrupt an active playtest.

## Closure rules

- **Fixed and validated:** the reported symptom is reproduced or equivalently localized, a concrete source change is built, and the same meaningful check passes on that candidate with retained before/after evidence.
- **Already fixed / verified existing behavior:** retain a direct comparison or current-build reproduction that establishes this conclusion. Explain the result; do not silently dismiss the clip.
- **Duplicate:** link the canonical defect and validate that its resolution covers this clip. Keep both original evidence references.
- **Blocked / unverified:** document the exact missing prerequisite and continue other rows. This does not reduce the remaining count.

On 2026-10-28 the owner authorized deferring reports when their bug cannot be determined. This pass therefore ends with one validated fix and six explicit deferrals. Deferred rows remain unresolved in the burndown; they do not count as fixes or verified existing behavior. Keep their clips, findings and reproduction paths. Commit validated source and this chart; do not merge, publish or release. Preserve useful evidence and clean superseded scratch.

## Findings and completed work

| ID | Concrete symptom / matched timestamp | Root cause / owning issue | Fix commit | Built validation and before/after evidence | Disposition |
| --- | --- | --- | --- | --- | --- |
| SM27-01 | Owner confirms black left cutout when jumping/hugging shaft wall | Full filler screen above a uniform authored wall; extend that wall vertically in margins | `9e547d7` | Same captured camera `(256,63)` removes6,880 black margin pixels at342; four widths, native pixels unchanged | Fixed; owner Pass recorded |
| SM27-02 | Owner confirms extended cone becomes a band | Offscreen HDMA saturated edge runs were rejected | `9e547d7`, `2511720` | Same ROM-generated HDMA tail extends on194 lines instead of0; four widths and both-side regression fixtures | Fixed; owner confirms light is fixed |
| SM27-03 | Owner confirms surface bubbles remain4:3 above wide lava | FX begins at `$5BE0`, one row before the old `$5C00` boundary | `9e547d7` | Same captured surface gains99 margin pixels at342; HUD/native pixels preserved | Fixed; qualified owner acceptance |
| SM27-04 | Wave Beam to Double Chamber transition/run | Undetermined | None | Room identities established; isolated-door phases not demonstrated wrong | Qualified owner recheck reports no further bug; original fault remains unconfirmed |
| SM27-05 | Candidate Crocomire seams; exact presentation pending | Undetermined | None | Two dense original segments and sampled immutable/interpolated captures; seam not reproduced | Owner reviewed and found nothing abnormal; not reproduced |
| SM27-06 | Bubble Mountain movement; exact fault unknown | Undetermined | None | Replays 178/182 each pass 600 checkpoints; full clip movement unmatched | Owner could not reproduce an abnormality; unresolved |
| SM27-07 | Left cross-pattern strip, 6.35-13.98 s; Plasma Spark `$D340`, camera `(256,1280)` | Completely unused 256x256 screens filled with `$83FF` inside a nonrectangular room were rendered as level content; beads-8wg.6.34 | `1f41930` | Fresh built traversal in both directions and current-build save/load pass. Same captured frame: 1,128 cross pixels removed at width 342, zero native/right-margin changes. Four widths and three interpolation phases covered by regression fixtures; 28 real scene/width native comparisons pass; 18/18 CTest. | Fixed and validated |

### 2026-10-28: SM27-07 resolved

The decisive room match used visible foreground geometry from the ROM, rather than palette or minimap guesses. The affected room is **Plasma Spark `$D340`**, entered at its bottom-right door `$83:A750`, not Swiss Cheese or the Beach. The original runtime's later traversal `20261027-025314-24809-000254` also identifies `$D340` and matches all 600 recorded checkpoints; it has different inventory and is not the exact video sequence.

The renderer now mattes a complete 256x256 screen only when all 256 level blocks are the ROM's `$83FF` filler. It leaves native pixels alone and preserves any screen containing even one different block, including hidden content and PLM changes. This follows the raster/interpolated terrain coordinates and clips effects/objects with the padding. It does not change scrolling, collision, enemy AI or save data.

Validation: Windows native-raster Release binary SHA256 `1f50d13af577326b4335e003382c23b021717ca2ed8314e01be8f8d2f5e2d7aa`, game base `e617b65` plus this source change; pinned engine `5ac4b98` and UI `ef13e3f`. Muted private QA used a fresh boot/landing, equipment grants, the normal ROM door loader, then normal left/right/left input; no success flags were set. The setup bypasses reaching the door and is not an unassisted progression claim. Current-build save/load retained room/player/inventory and accepted subsequent input. No old checkpoint identity was bypassed.

Retained evidence:

- [Built traversal, both directions and save/load](../../../diagnostics/plasma-spark-padding-final/result.json), with command journal and 117 screenshots in that directory.
- [Same immutable capture before](../../../evidence/padding-same-capture-before.bmp) and [after](../../../evidence/padding-same-capture-after.bmp): native camera `(256,1280)`, width 342, 1,128 changed left-margin pixels, all left-margin gameplay pixels now black, zero native/right-margin changes.
- [28 scene/width comparisons](../../../evidence/padding-comparisons/results.json): all 49,152 native pixels below the HUD match stock and the baseline in every case; the six other selected scenes have no margin changes.
- [18 passing combined tests](../../../evidence/padding-ctest.log). The renderer fixture covers 256/342/448/682 widths, alpha 0/0.5/1, a moving boundary, preserving native pixels and preserving a filled screen with real content.

Other findings remain open. SM27-01's geometry now points toward Plasma Spark's upper shaft rather than the earlier Watering Hole guess, but its black edge cutouts need a separate matched reproduction. SM27-04 starts in the Wave Beam room `$ADDE`, then Double Chamber `$ADAD`; its transitions remain unverified. Direct Ceres-to-Zebes room setups were invalid because common room graphics had not been initialized; these were replaced by fresh landing-based setups. Similar-room screenshots and passing checkpoint hashes are not dispositions for the remaining reports.

### 2026-10-28: owner-authorized deferrals

The owner answered the targeted question about the Bubble Mountain and eye clips: **"I don't konw. defer if you can't determine"**. The same rule is applied to the other reports whose alleged defect remains unconfirmed after the retained checks. The six deferred rows stay in the unresolved count. No speculative source fixes were made for them.

For SM27-01, foreground matching establishes Plasma Spark, with cameras approximately `(512,20)` at 0.0 s, `(256,105)` at 3.724 s, and `(256,108)` at 5.213 s. The left black cutout ends at screen rows 149/146 respectively: the source world's y256 boundary. Its upper-left screen is 256 blocks of `$83FF`; the screen below is 256 genuine `$8210` wall blocks. The conservative matte preserves that real wall. This explains the shape, but does not establish that this was the intended fault, so the report remains deferred. See [source/clip alignment](../../../evidence/shaft-padding-location.json).

SM27-06 candidate replays 178 and 182 each match all 600 recorded checkpoints. Sampled footage comparisons do not establish the same complete movement; the HUD-corruption guess is not a demonstrated diagnosis. SM27-05's retained original frames include nearest candidates around replay 324 frame 194363 and replay 325 frame 194454, plus immutable raster pairs; they do not establish the suspected seam. Wrong-room guesses and invalid pre-landing setups are disposable diagnostics, not regression findings.

Final pass result: **one source fix (`1f41930`), 18/18 combined tests, 28 native scene/width comparisons, current-build save/load and both-direction Plasma Spark traversal pass; six unresolved reports explicitly deferred.** Beads remains open for those reports. No merge, release or owner-facing game launch was performed.
