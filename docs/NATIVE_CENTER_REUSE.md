# Native-center reuse

`SM_NATIVE_CENTER_REUSE=ON` requires `SM_RASTER_IMPL=NATIVE` and
`SM_NATIVE_ROW_PLAN=ON`. The product default remains OFF by owner direction;
qualification does not promote it. The existing full custom compositor and
separate LLE build remain available.

For a discrete presentation (`alpha=1`), a compatible live PPU producer can
provide the finished native center. The custom renderer then prepares and
composes the margins, copying the center instead of decoding and composing it
again. Intermediate animation frames retain full custom reconstruction.

Eligibility is explicit per captured line: the real hardware-capture boundary
must supply it, the original optimized PPU must draw the line, and the line
must be ordinary Mode 1 without mosaic, OAM priority rotation, debug layer
filtering, or unsupported overlay/extra-surface consumers. This candidate
requires NewRenderer and NoSpriteLimits together. The custom renderer draws all
OAM entries; a hardware-limited image is not a compatible replacement. That
profile retains the previous deferred/full-custom path. No guest status flags
or settings are changed to obtain a provider.

Provenance is private presentation data. Reset, a new source frame and capture
loading invalidate it. An arbitrary stock image or diagnostic capture does not
grant reuse permission. Native save data, guest serialization and protocol
behavior are unchanged. Source/selector identity remains part of current-build
snapshot and replay identity; no old-state or cross-build compatibility is
claimed.

The renderer fixtures force the experiment on independently of product defaults.
They cover trusted versus placeholder providers, the real default layer mask
(`0xff`), filtered layers, sprite limits, rotation, mosaic, intermediate
presentation, and complete randomized rows with independent margin oracles.
`SM_NATIVE_CENTER_REPORT=<path>` with `SM_CAPTURE_FRAME=<frame>` records the
selected discrete frame's native-center count in a built game.

Automated qualification covers Windows and Linux renderer fixtures, 512
independent scalar rows, live grapple/power-bomb/rotation comparisons, and
extra presentations with matching simulation traces and captured render inputs.
The exact production binary passes same-process save/load, fresh-process load
after an intentional health mutation, and matched run-ahead in stock/wide
modes. The fresh load restores the subsequent trace, final image/capture and
live native-center provider. Interactive rewind and final visual/gameplay
approval remain part of the combined owner handoff.

Five balanced 7,200-frame Eye pairs per profile on an i5-4670 measured these
median reductions in process CPU against the already-qualified R2 row planner:

| Profile | NoSpriteLimits | CPU reduction | Disposition |
| --- | --- | --- | --- |
| Custom 4:3 | 1 | 16.28% | Five positive pairs |
| 16:9 | 1 | 12.36% | Five positive pairs |
| 32:9 | 1 | 7.95% | Five positive pairs |
| 16:9 fallback | 0 | +0.34%, mixed | Neutral/inconclusive |
| Stock | 0 | -0.37%, mixed | Neutral/inconclusive |

The sprite-limit setting is matched within every pair and is not changed to
obtain a provider. The R2 campaign used NoSpriteLimits=0, so these packages
and percentages must not be conflated or added. Background host cost is
recorded. Local pilot and identical-binary timings were noisy and are excluded
from accepted gains. An early inactive-provider-mask prototype is also excluded.
Exact measured runtime source is `914d518`; later qualification documentation
does not change its runtime files. Text grows 1,280 bytes, read-only data
64 bytes and private provenance BSS 448 bytes; there is no new guest state.

This is an explicitly authorized existing title adapter. Owner direction on
2026-10-28 puts future game-agnostic shared engine mechanisms first. Reuse or
elevate common operations with actual consumers, preserve title policy in
adapters, and leave substantial new title adoption until the end of the
shared-engine burndown. This result is not an engine-wide gain.

Tracking: game `beads-8wg.6.36`, framework R4 `beads-8wg.2.165`, roadmap
`beads-49cy`, optimization campaign `beads-2wy8.1`. Raw paired measurements,
source/binary/runtime identities, feature settings, correctness and costs are
indexed by `campaign-results.json` under
`G:/Temp/Codex/snesrecomp/native-center-reuse-20261028`. No original Xbox or
cross-build/cross-host netplay qualification is claimed. Merge awaits the
combined owner playtest; new options remain OFF by default.
