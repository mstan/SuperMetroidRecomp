# Super Metroid replay/video bugfix burndown

Updated: 2026-10-28. Tracker: **beads-8wg.6.34**.

Worktree: `G:\Temp\Codex\supermetroid\replay-bugfix-20261027\source`  
Branch: `fix/replay-bugfixes-20261027`  
Starting game commit: `e617b6573ea090e3bcab2690f4193ce5c2fa4869` (0.3.17).

## Burndown chart

**1 / 7 unique clip reports resolved; 6 remain.**

```mermaid
xychart-beta
    title "Super Metroid: unresolved clip reports"
    x-axis ["Intake", "Padding fix"]
    y-axis "Reports remaining" 0 --> 7
    bar [7, 6]
```

| Checkpoint | Total unique clip reports | Fixed and validated | Verified existing behavior / duplicate of another row | Remaining |
| --- | ---: | ---: | ---: | ---: |
| Intake, 2026-10-27 | 7 | 0 | 0 | 7 |
| Room padding fix, 2026-10-28 | 7 | 1 | 0 | 6 |

Append actual milestones to this table and update the chart. Do not invent dates or predicted completion. The seven rows are evidence-backed intake items, **not seven confirmed independent defects**. Split multiple defects from one clip into linked subitems when demonstrated; preserve the original seven-row accounting. A shared fix may resolve multiple rows only after each row's symptom is checked.

## All supplied clips

Names below are exact files in `E:\Downloads`. Scene descriptions and inspection targets are provisional where the intended symptom is not yet established. The owner has not specified a separate symptom for every clip. Infer and investigate from the footage and logs; do not wait for a priority choice to start the clearest actionable item.

| ID | Work order | Clip | Length | Scene / observed candidate | Status | Next decisive check and closure target |
| --- | ---: | --- | ---: | --- | --- | --- |
| SM27-01 | 6 | [clip_1,793,140,731,658.mp4](E:/Downloads/clip_1,793,140,731,658.mp4) | 8.292 s | Vertical movement through a tall green/brown room. Inspect terrain/background scrolling, room margins, and moving Samus. Specific defect remains unconfirmed. | **Open: identify symptom** | Match footage to room, inventory, camera and motion in the replay index; inspect consecutive frames and audio. Record an exact defect/timestamp before implementing. Validate the same movement on the candidate. |
| SM27-02 | 7 | [clip_1,793,140,528,702.mp4](E:/Downloads/clip_1,793,140,528,702.mp4) | 10.724 s | Blue corridor with an eye/light-cone effect while Samus moves horizontally. Cone geometry, motion, culling and audio are inspection targets, not established failures. | **Open: identify symptom** | Find the actual cone activation and camera movement, rather than an idle capture from the same room. Compare native and widened output through activation/deactivation and inspect audio. Close only the demonstrated symptom. |
| SM27-03 | 4 | [clip_1,793,140,650,331.mp4](E:/Downloads/clip_1,793,140,650,331.mp4) | 9.717 s | Jumps and scrolling through a Norfair room above lava. Check moving terrain/background alignment, lava boundaries and HUD-edge artifacts; root cause unconfirmed. | **Open: reproduce motion** | Start with the matching-inventory `$A788` replay candidates. Match a footage timestamp, capture adjacent simulations/intermediate presentations, then validate terrain, effects and HUD together through the jump. |
| SM27-04 | 3 | [clip_1,793,140,682,203.mp4](E:/Downloads/clip_1,793,140,682,203.mp4) | 16.136 s | Chozo/item room, door transitions and a speed-boosted run over lava. Isolated door images are visible during transitions; whether each is intended needs frame-level comparison. | **Open: reproduce transition** | Locate outgoing/incoming rooms and loader phases; inspect both transition directions, door ownership and the following lava run. Compare retail behavior where needed. Distinguish normal transition blanks from an introduced phantom door or stale room image. |
| SM27-05 | 1 | [clip_1,793,140,699,506.mp4](E:/Downloads/clip_1,793,140,699,506.mp4) | 17.250 s | Crocomire encounter. Apparent discontinuities in terrain/liquid at native-view boundaries are visible in the footage. Exact cause and offending replay frames remain unconfirmed. | **Open: reproduce seams** | Use Crocomire replay `20261026-161546-22582-000325` and nearby segments as candidates. Match the camera/presentation phase; isolate terrain, boss canvas, liquid and color math. Eliminate the demonstrated seams while preserving native pixels, boss behavior and lava. |
| SM27-06 | 5 | [clip_1,793,140,666,141.mp4](E:/Downloads/clip_1,793,140,666,141.mp4) | 3.181 s | Vertical movement in a green Bubble Mountain scene. Inspect background/terrain alignment, sprites and camera motion; thin black/corrupted HUD rows are visible during the jump; exact failing frame is being matched. | **Open: reproduce HUD corruption** | Begin with `$ACB3` replay `20261026-161546-22582-000182`; confirm footage alignment rather than treating a similar screenshot as an exact match. Check consecutive frames and sound, then reproduce and validate the identified fault. |
| SM27-07 | 2 | [clip_1,793,140,718,500.mp4](E:/Downloads/clip_1,793,140,718,500.mp4) | 14.085 s | Plasma Spark room `$D340`, lower route: white cross-pattern strip in the left margin (6.35-13.98 s). | **Fixed and validated: complete unused screens clipped** | Built traversal left, right, then left; current-build save/load; immutable before/after capture at camera `(256,1280)`; native pixels unchanged at four widths. See outcome below. |

The suggested order starts with the clearest visible candidates and may change when measurements identify a shared cause or a more consequential defect.

### Duplicate inputs

| Supplied input | Accounting |
| --- | --- |
| `clip_1,793,140,666,141 (1).mp4` | Byte-identical to SM27-06; SHA256 `bc38b2817da48686c7231a4dd7da241a39056998a7c242527adc6f3f9ecb6664`. Covered by SM27-06, not an eighth defect. |
| Concatenated/repeated `clip_1,793,140,731,658.mp4` path in the original message | One file, covered by SM27-01. |

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

Complete the goal only when all seven clip reports and any distinct defects discovered from this evidence have a validated disposition, the combined candidate regression checks pass, and changes/evidence are recorded. If owner judgment is still required, prepare the concrete check and leave that requirement outstanding. Commit validated source and this chart on the worktree branch; do not merge, publish or release without the owner's instruction. Preserve useful evidence, update the resource ledger, and clean superseded agent-created scratch.

## Findings and completed work

| ID | Concrete symptom / matched timestamp | Root cause / owning issue | Fix commit | Built validation and before/after evidence | Disposition |
| --- | --- | --- | --- | --- | --- |
| SM27-01 | Pending | Pending | None | Intake only | Open |
| SM27-02 | Pending | Pending | None | Intake only | Open |
| SM27-03 | Pending | Pending | None | Baseline candidate replays pass | Open |
| SM27-04 | Pending | Pending | None | Intake only | Open |
| SM27-05 | Candidate seams; exact replay frame pending | Pending | None | Baseline candidate replay/captures pass | Open |
| SM27-06 | Pending | Pending | None | Baseline candidate replay passes | Open |
| SM27-07 | Left cross-pattern strip, 6.35-13.98 s; Plasma Spark `$D340`, camera `(256,1280)` | Completely unused 256x256 screens filled with `$83FF` inside a nonrectangular room were rendered as level content; beads-8wg.6.34 | This branch: `src/sm_renderer.c`, `tests/sm_renderer_test.c` | Fresh built traversal in both directions and current-build save/load pass. Same captured frame: 1,128 cross pixels removed at width342, zero native/right-margin changes. Four widths and three interpolation phases covered by regression fixtures; 28 real scene/width native comparisons pass; 18/18 CTest. | Fixed and validated |

### 2026-10-28: SM27-07 resolved

The decisive room match used visible foreground geometry from the ROM, rather than palette or minimap guesses. The affected room is **Plasma Spark `$D340`**, entered at its bottom-right door `$83:A750`, not Swiss Cheese or the Beach. The original runtime's later traversal `20261027-025314-24809-000254` also identifies `$D340` and matches all 600 recorded checkpoints; it has different inventory and is not the exact video sequence.

The renderer now mattes a complete 256x256 screen only when all 256 level blocks are the ROM's `$83FF` filler. It leaves native pixels alone and preserves any screen containing even one different block, including hidden content and PLM changes. This follows the raster/interpolated terrain coordinates and clips effects/objects with the padding. It does not change scrolling, collision, enemy AI or save data.

Validation: Windows native-raster Release binary SHA256 `1f50d13af577326b4335e003382c23b021717ca2ed8314e01be8f8d2f5e2d7aa`, game base `e617b65` plus this source change; pinned engine `5ac4b98` and UI `ef13e3f`. Muted private QA used a fresh boot/landing, equipment grants, the normal ROM door loader, then normal left/right/left input; no success flags were set. The setup bypasses reaching the door and is not an unassisted progression claim. Current-build save/load retained room/player/inventory and accepted subsequent input. No old checkpoint identity was bypassed.

Retained evidence:

- [Built traversal, both directions and save/load](../../../diagnostics/plasma-spark-padding-final/result.json), with command journal and 117 screenshots in that directory.
- [Same immutable capture before](../../../evidence/padding-same-capture-before.bmp) and [after](../../../evidence/padding-same-capture-after.bmp): native camera `(256,1280)`, width342, 1,128 changed left-margin pixels, all left-margin gameplay pixels now black, zero native/right-margin changes.
- [28 scene/width comparisons](../../../evidence/padding-comparisons/results.json): all 49,152 native pixels below the HUD match stock and the baseline in every case; the six other selected scenes have no margin changes.
- [18 passing combined tests](../../../evidence/padding-ctest.log). The renderer fixture covers 256/342/448/682 widths, alpha0/0.5/1, a moving boundary, preserving native pixels and preserving a filled screen with real content.

Other findings remain open. SM27-01's geometry now points toward Plasma Spark's upper shaft rather than the earlier Watering Hole guess, but its black edge cutouts need a separate matched reproduction. SM27-04 starts in the Wave Beam room `$ADDE`, then Double Chamber `$ADAD`; its transitions remain unverified. Direct Ceres-to-Zebes room setups were invalid because common room graphics had not been initialized; these were replaced by fresh landing-based setups. Similar-room screenshots and passing checkpoint hashes are not dispositions for the remaining reports.
