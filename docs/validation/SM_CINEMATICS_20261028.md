# Cinematic skips and ending return, 2026-10-28

The current runtime gates pass on production source `82e17c6` (trace executable
SHA-256 `aa0bfec57896ab44337351b4ce0bc82c6ff4f944ff46157a2f2b90222948b419`).
Evidence is under `G:/Temp/Codex/sm-qol-20261027/`. All runs use isolated native
saves/settings and muted dummy video/audio. The owner's running build was not
restarted or modified.

## Intro and Ceres

`cinematic-intro-{native,interp}/results.json` contains six matched cases:

| Case | First gameplay frame | Room |
| --- | ---: | --- |
| English, stock | 9304 | Ceres `DF45` |
| English, intro skip | 3452 | Ceres `DF45` |
| Japanese, stock | 10536 | Ceres `DF45` |
| Japanese, intro skip enabled | 10536 | Ceres `DF45` |
| New game, stock | 9274 | Ceres `DF45` |
| New game, Ceres skip | 1882 | Landing Site `91F8` |

The intro test waits for native story function `AEB8`, then presses Start.
English skip selects `B72F`; stock and Japanese do not. Japanese language is a
fixture prerequisite, not evidence of menu selection. Japanese stock/mod
observations and every complete WRAM snapshot are identical. New-game Ceres
skip uses actual file/menu input and the native loading path.

## Staff roll, pose and final screen

`cinematic-ending-native/` and `cinematic-ending-interp-input/` contain stock,
credits-only, return-only and combined cases. Native ending initialization is
started with game state `26`: **the final battle is explicitly bypassed**. No
credits pointer, pose or completion result is manufactured.

Start at the staff roll leaves 8,306 frames of roll in stock/return-only, versus
one frame with credits skip. Both paths still execute approximately 2,092 frames
of the native pose/final sequence; the one-frame sampling difference is retained
in JSON. Captured pose at +500 shows Samus; the later white flash and final item
percentage screen also remain. Start during the pose does not return to title.
Start on the eligible final screen returns to native title state1 only when
return-to-title is enabled. The full 8KB SRAM is unchanged by that return.

## Execution paths and reproduction

Every observed field and complete WRAM snapshot matches alternate execution.
The cinematic callback roots themselves remain interpreter-authoritative in
this workload; denying only those roots did not change execution counts and is
not presented as compiled-cinematic coverage. Alternate tests additionally deny
available compiled input/music consumers `80:9459` and `80:8F0C`. Actual
interpreter work increases in every case. Examples: English stock 2,425,296 to
2,739,282 instructions; credits-only 4,126,275 to 4,599,489. Full checks are in
`cinematic-intro-validated.json` and `cinematic-ending-validated.json`.

```powershell
python tools/test_qol_intro.py --exe <trace-exe> --rom <stock-rom> --out <fresh-native>
python tools/test_qol_intro.py --exe <trace-exe> --rom <stock-rom> --out <fresh-interp> --interp --compare <fresh-native>
python tools/test_qol_ending.py --exe <trace-exe> --rom <stock-rom> --out <fresh-ending>
python tools/test_qol_ending.py --exe <trace-exe> --rom <stock-rom> --out <fresh-ending-interp> --interp --compare <fresh-ending>
```

The durable tools preserve the measured inputs/prerequisites. This qualifies
these cinematic contracts; owner presentation judgment remains separate.
