# Optional movement extras — 2026-10-28

Shinespark steering now has independent `down-to-morph` and `jump-to-cancel`
options. Both default off, preserving Down steering. A new press stops an
active horizontal, vertical or diagonal spark; Down requires Morph Ball and
selects ball fall (Spring Ball when equipped), while Jump selects normal fall.
Stock transition C2 commits the pose, aligns it and restores normal movement
and input. The hook clears spark speed/contact/echo state and queues the native
SFX3 stop command with the game's queue rules. There is no cartridge write.

Quick morph adds an optional physical **L + Down in the air** gesture. It
produces the configured item-cancel/Down pad word before recording/exchange.
The shoulder plugin consumes the raw gesture before remapping, including when
item cancel is rebound to a shoulder. Simulation hooks read only recorded
guest input. Player two, menus, missing Morph Ball and non-air movement pass
through. Existing ground/hold morph behavior remains unchanged.

New spark hooks are `$90:D0AB`, `$90:D0D7`, `$90:D106`, redirecting to their
own RTS. Air morph reuses `$91:81A9`. All behavior is scoped to the selected
feature/options; neither requires new engine APIs or host-only simulation state.

`tools/test_qol_movement_extras.py` runs 20 isolated, muted cases: both exits
in all three spark directions, option-off and missing-item controls, jumping,
spinning, left-facing and descending air morph, plus shoulder remapping with
item cancel rebound to R. Actual native ball/fall poses and landing are
observed; no result pose or movement handler is poked. All preserve SRAM.

Every observed frame and full WRAM snapshot matches between normal and alternate
execution denying the transition lookup, spark handlers and C2, including their
LoROM aliases. Interpreter work increases in all 20 cases (horizontal morph:
4,366,048 to 4,478,931 instructions). Evidence:
`G:/Temp/Codex/sm-qol-20261027/movement-qualified-aot-final/results.json` and
`movement-qualified-interp/results.json`; QA executable SHA-256
`a72daf6ae2ff5adcabd032d87a70498e723f109220cb90108e2ad95246209e08`.
The normal result reuses 18 identical-binary/identical-script cases from
`movement-qualified-aot`; their original execution identities remain recorded.
Only the two corrected descending fixtures were rerun.

Two failures were caught and retained: C1 intentionally skips committing the
pose, leaving the first spark-cancel prototype stuck; use C2 instead. The first
descending fixture waited until Samus had landed and was corrected to hold a
longer jump. Neither failed run counts as passing evidence. The native movement
test checks remapping/guest-read-only input, option and item guards, every spark
entry/facing, sound-ring wrap and ABI preservation. Windows tests pass; native
movement tests also pass Ubuntu GCC. Full regeneration is identical twice.
Owner control/feel review remains pending. No merge, push or release.
