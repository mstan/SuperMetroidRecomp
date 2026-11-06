# Exposed widescreen rooms: development handoff

Continue development on `feat/exposed-widescreen-rooms-20261105` and draft PR #52.
The feature remains under validation. Do not integrate or publish until its
correctness, interactive and release checks pass.

## Feature contract

- Display connected rooms and exposed terrain using each room's own graphics,
  palette and animated environmental effects. Continue suitable solid foreground
  into exposed margins without covering authored air, passages or adjacent rooms.
- Keep hatch graphics consistent with collision, ammunition and native locks.
- Neighboring room logic defaults On. Off retains scenery, environmental
  animation and working door bubbles while suppressing neighboring actors.
- Continuous traversal is the default. The selectable Native transition retains
  native scrolling and body movement with a coherent visible world throughout.
- Preserve native combat, progression, actor ownership, collected items and SRAM.
- Include resident and transitional state in current-build save/load, replay,
  rewind, run-ahead, rollback and the agreed netplay profile.
- Fresh packages default to adaptive Fit. Preserve existing explicit settings.

## Implementation and validation

See [docs/exposed-rooms.md](docs/exposed-rooms.md) for the technical architecture.
Use the supported strict regeneration pipeline and pinned framework versions.
Never edit generated C. Keep generated inputs, ROMs and runtime artifacts out of
Git. Bound build parallelism and use the shared capacity helper.

Run the existing CTests and the relevant owned-ROM room, door, renderer and
runtime checks against the combined candidate. Validate actual Fit/120 FPS
presentation, transition motion in both directions, liquid effects, collision,
current-build snapshots, rewind and supported two-peer gameplay. Controller
fixtures must distinguish access setup from the behavior being tested. A loaded
room or a settled screenshot does not establish a traversable route.

Keep independently observed failures open until validated. Preserve private
acceptance records in the established private tracker; repository documentation
contains implementation and reproducible software behavior only.

## Integration and release

After all required checks pass, finish the normal integration and next release.
Confirm the next version against current tags. Use the established four-platform
GitHub build and release workflow, package audits and fresh packaged gameplay,
default-settings and native-save checks. Deliver the verified Windows ZIP to
`G:/Nextcloud/Games/SNES`, replacing the superseded Super Metroid archive only
after verification. Retain one current archive and preserve saves, settings and
unrelated files. Remove disposable task data after retaining necessary evidence.
