# Current Super Metroid owner playtest, 2026-10-28

2026-10-29 follow-up: the owner marked ten subchecks Pass and two Fail (map
transition flashes and moving auto-charge flare). Both fixes are implemented and
automatically qualified; see [visual-fix evidence](SM_VISUAL_FIXES_20261029.md).
The current selector uses `owner-visual-retest` with just those two failed
subchecks. Earlier Pass observations and this seven-setup profile are preserved.
The historical seven-setup instructions and binary identity follow.

Selector: <http://127.0.0.1:8786>. Open/restart each setup there and save Pass,
Fail or Deferred plus individual notes. The first setup is the native pause map;
press Start to play. Every launch uses a normal window and normal audio.
The selector's Input menu switches between keyboard and Controller 1.

| Setup | Focus |
|---|---|
| Map and aiming | Door/item/one-way readability, pause/world map, zoom and minimap; diagonal aim, missile shoulder and right-stick weapon ring. |
| Movement and Speed Booster sound | Respin, momentum, quick/air morph, speed ball, Space Jump and spark controls; sound stopping and blue palette. |
| Heavy physics comparison | Compare running/jumping against the movement setup. |
| Auto charge and manual reserves | Idle charge then tap to release, rapid normal shots, reserve transfer/cancel feel. |
| X-ray over a respawning item | Scan, turn, release and resume moving around the actual respawning item. |
| Pickup without a jingle or pause | Resume and fall onto a real reserve tank. Music continues with no pickup fanfare, box or gameplay freeze. |
| Ship save confirmation | Resume, press Down at the ship, choose Yes, hear the native refill and dismiss Save Completed. Automated checks already prove the save precedes the message. |

Keyboard: arrows move; Z jumps, A shoots, X dashes, S cancels; C/V are L/R;
Enter is Start; Right Shift is Select. F1 reloads the prepared setup. Exact
map/control steps are also shown beside each selector button. Physical
controller ownership and right-stick feel still need owner validation.

Only the selector's own game process is replaced when switching setups. Each
case has disposable progress and isolated saves/settings. The prior profile and
its observations remain preserved; existing notes were copied with provenance.
Read individual observations before follow-up work; a bundle Pass must not erase
a failed/deferred subcheck.

Prepared root: `G:/Temp/Codex/sm-qol-20261027/owner-playtest-final/`.
`manifest.json` records the binary, ROM, feature selections and paused states;
`notes.json` stores observations and `launched.json` identifies the current
selector-owned game and private diagnostic port. The tested binary is
`sm-ship-build/SuperMetroidSNESRecomp.exe`, SHA-256
`4478f50a86fec4929cf7f2ed1054a4c2cd501ba97fd17f0724e08c69b09c3452`.
It includes the cut-all pickup option and the ship-save correction from
production commit `b7bf82b`; engine/UI/net pins are unchanged.

Preparation is muted/headless and uses the measured local capacity reservation
helper. Each new current-build state is independently restored and resumed before
handoff; evidence is `owner-final-state-restore/results.json`. Ordinary owner
saves/settings and delivery archives are never used or changed.

Falling aim-down is still deferred for the owner's exact input/direction
definition. Instant pickups retain the native Varia/Gravity transformation
animation pending that separate scope decision. Full-hack content packs remain
deferred. No main merge, code push, release or delivery replacement is included.

To prepare a replacement after a future build, use a fresh output directory:

```powershell
python tools/sm_qol_playtest.py prepare --exe <trace-exe> --rom <stock-rom> --out <fresh-profile> --notes-from <prior-profile>/notes.json --jobs 1
python tools/sm_qol_playtest.py serve --out <fresh-profile> --port 8786 --launch maps
```

Read the saved notes first and do not interrupt an owner playtest in progress.
