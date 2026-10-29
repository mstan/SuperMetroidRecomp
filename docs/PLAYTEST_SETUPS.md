# Prepared gameplay setups

Use `tools/fixtures/eye_room_playtest.txt` for a general gameplay handoff in
room `$9E9F`. It enters through the game's door loader and retains the resulting
player position and scrolling tilemap. It grants health and Morph Ball but does
not set an effect, enemy, or completion flag.

`eye_beam_debug.txt` is an effect-specific diagnostic. Its final `forcepoke`
holds Samus at the eye without streaming the intervening terrain. A snapshot
made there is unsuitable for judging general terrain rendering: the native
center tilemap can disagree with the decompressed room geometry while the
widescreen margins remain correct. Do not use that diagnostic's snapshot as a
general gameplay save.

Generate disposable states with the current binary. The script interface does
not implement a `savestate` command. Automated setup generation can use the
existing overlay save/load self-test (`SNESRECOMP_OVERLAY_SELFTEST=4000` and
`SNESRECOMP_OVERLAY_SELFTEST_SAVELOAD=1`), which exercises the actual save-state
browser. Check a fresh-process load before handing the state to a player.
Native SRAM and owner settings must remain isolated from these fixtures.

For capture comparisons after loading a state, `SM_CAPTURE_FRAME` counts host
simulation frames, while `SNESRECOMP_FRAME_BMP_FRAME` uses the restored SNES
frame counter. Match the same moment; a different counter origin is not a
renderer mismatch.
