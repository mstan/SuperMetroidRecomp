# Isolated resource, combo and visor checks, 2026-10-28

These close the four runtime gaps identified during the final acceptance audit.
All use production source `82e17c6`, trace executable SHA-256
`aa0bfec57896ab44337351b4ce0bc82c6ff4f944ff46157a2f2b90222948b419`, engine
`e265a2be`. Saves/settings are private; launches are headless with dummy audio.
The running owner playtest and its executable were not changed. Evidence root:
`G:/Temp/Codex/sm-qol-20261027/`.

## Shinespark drain

`spark-drain-{native,interp}/` covers horizontal, vertical and diagonal flight,
each stock, Half and None. Health199 and a stored spark are prerequisites; real
input initiates the native spark and it ends through collision. No flight,
damage or collision outcome is injected.

Each 12-frame flight sample drains exactly12/6/0 energy. Final health for
stock/half/none is horizontal160/179/199, vertical119/159/199 and
diagonal141/170/199. Every sampled trajectory/state field except the intended
health difference is identical across modes. All finish outside the spark pose;
native SRAM is unchanged. The old Bead note saying Half was unimplemented is
superseded: its three decrement hooks work in actual gameplay.

## Select beam combo without auto charge

`select-combo-held-{native,interp}/` contains six matched cases: stock/mod at
full charge, low charge and no power-bomb ammo. Auto charge is explicitly off.
Normal held X builds native charge; X remains held when Select is pressed.
With enough charge/ammo, the mod spends one PB (5→4), emits four real special
projectiles and leaves HUD weapon0 selected. Stock cycles to missiles. Low
charge/no ammo retain stock observations and complete WRAM snapshots; no extra
PB is spent. Idle charge is0; no auto-charge dependency is being hidden.

The earlier scripted `press` probe released X between commands and never tested
this gesture. Its failed calibration output is retained separately and is not
passing evidence. The durable tool uses exact TCP controller input.

## Save station refills everything

`save-station-sram-{native,interp}/` uses the real Crateria save station in
room `93D5`. Position and depleted/max resources are prerequisites. Native
collision opens the real prompt at tick33; Yes, the station animation and Save
Completed all execute. No station instruction, save-used or result flag is set.

Stock keeps health37, reserves0, missiles1, supers1, PB0. The mod fills
499/300/50/15/10 at the prompt and retains them afterward. The native save-used
flag becomes1 and SRAM changes through actual saving. The saved resource words
are read from the ROM-defined file offset; they equal current resources. The
native sum and its complement, including both mirror headers, all validate.
The full8KB saved SRAM and complete WRAM snapshots match alternate execution.

## Wider/faster X-ray

`xray-options-{native,interp}/` covers stock plus all four width/speed
combinations, facing left and right: ten cases. Equipment/selected visor are
prerequisites; ordinary held B and Up/Down open, aim and close it.

Native half-widths clamp at10/18/26. Five input frames turn5 units at original
speed and10 at2x, then the opposite input returns to the original angle. Both
native end limits match the chosen width; release restores normal time and
zero beam width. Full WRAM and every captured bitmap are identical under
alternate execution. Stock and widest cone images were also inspected side by
side: the wider cone is rendered. Native SRAM remains unchanged.

## Reproduction and execution limits

All four suites compare every recorded observation and full WRAM checkpoint;
actual interpreter work increases in every alternate case. Save SRAM and X-ray
bitmaps also match. Denied targets include feature roots and available compiled
surrounding consumers (notably `A0:8EB6`); this does not invent a compiled variant
where a particular guest routine remains interpreter-authoritative.

```powershell
python tools/test_qol_spark_drain.py --build <trace-build> --rom <stock-rom> --out <fresh-native>
python tools/test_qol_spark_drain.py --build <trace-build> --rom <stock-rom> --out <fresh-interp> --interp --compare <fresh-native>
python tools/test_qol_select_combo.py --exe <trace-exe> --rom <stock-rom> --out <fresh-native>
python tools/test_qol_save_station.py --exe <trace-exe> --rom <stock-rom> --out <fresh-native>
python tools/test_qol_xray.py --exe <trace-exe> --rom <stock-rom> --out <fresh-native>
```

The latter three tools also accept `--interp --compare <native-output>` with a
fresh output directory. Physical controls and subjective feel remain owner
checks. Falling aim-down and suit-animation scope remain pending owner answers.
