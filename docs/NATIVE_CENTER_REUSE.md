# Native-center provider experiment

`SM_NATIVE_CENTER_REUSE=ON` requires `SM_RASTER_IMPL=NATIVE` and
`SM_NATIVE_ROW_PLAN=ON`. The product default remains OFF until qualification
and the combined owner playtest. The existing full custom compositor and
separate LLE build remain available.

For a discrete presentation (`alpha=1`), a compatible live PPU producer can
provide the finished native center. The custom renderer then prepares and
composes the margins, copying the center instead of decoding and composing it
again. Intermediate animation frames retain full custom reconstruction.

Eligibility is explicit per captured line: the real hardware-capture boundary
must supply it, the original optimized PPU must draw the line, and the line
must be ordinary Mode 1 without mosaic, OAM priority rotation, debug layer
filtering, or unsupported overlay/extra-surface consumers. The first experiment
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

Tracking: game `beads-8wg.6.36`, framework R4 `beads-8wg.2.165`, roadmap
`beads-49cy`, optimization campaign `beads-2wy8.1`. Raw experiments and exact
source/binary identities are retained at
`G:/Temp/Codex/snesrecomp/native-center-reuse-20261028`. The first live pilot
matches the original R2 image, final guest trace and capture, but changing-host
timings and a noisy identical-binary control do not establish a performance
gain. Formal matched measurements and broader effects/current-state checks
remain pending. No original-Xbox qualification is claimed.
