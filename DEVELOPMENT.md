# Super Metroid Recomp — Development Log

Bring-up of Super Metroid as game #4 on the `snesrecomp` static recompiler
(interpreter-tier project: interp816 floor + manifest feedback). This file is
the durable, in-repo development record for the attract-demo bring-up effort.

- **Game repo:** `SuperMetroidRecomp` — branch `investigate/sm-0012-blocker`
- **Engine repo:** `snesrecomp` — branch `investigate/sm-0012-blocker`
  (HEAD `1b3201d`)
- **Decomp (ground truth):** `snesrev/sm` (`F:\Projects\sm`) — `src/sm_XX.c`
  per bank, `src/funcs.h` (`#define fnFoo 0x82xxxx`)
- **ROM:** `Super Metroid (Japan, USA) (En,Ja).sfc` (LoROM; bank `$82` ↔ file
  region `$10000`)
- **Oracle:** `snesrecomp/tools/snesref` (snes9x libretro, headless, per-frame
  WRAM trace) — the sole differential reference.

> Hard rule: fix the **generator** (`recompiler/v2/*.py`) or the **C runtime**,
> never `src/gen/` (gitignored, regenerated). No stubs / dummy returns.
> Loop = fix gen → full-regen → build → measure → commit. Prefer the
> general/complete fix over a narrow per-site patch.

---

## Milestone (2026-06-18): attract-demo "stuck at game_state 40" — FIXED + VERIFIED

### Symptom
Free-running the attract demo, the recomp was pinned at `game_state` (`$0998`)
== 40 forever. `InitAndLoadGameData` re-ran every ~68 frames (`demo_scene`
marched 0→5, re-reading demo room data) instead of advancing 40→41→42 to play
the demo. The oracle advances 0→1→40→41→42 normally.

### Real root cause (corrects the earlier "DB-divergence" diagnosis)
The earlier theory — a data-bank (DB) divergence (`$0012=$FFFF`, `DB=$00` at
`LoadDemoRoomData $82:8679`) — was a **downstream symptom**, not the root. See
`docs/SM_DEMO_DB_DIVERGENCE.md` (corrected at top).

The recompiler **suppressed the indirect call** `JSR ($0012,X)` at `$82:817B`
(= `CallDemoRoomDataFunc(demo_code_ptr)`, decomp `sm_82.c:63`) under its
`cfg-required-dispatch-or-kill` policy, emitting a bare
`return RECOMP_RETURN_NORMAL`. That **dropped the very next instruction**
`INC $0998` at `$82:817E` (`++game_state`) and the rest of the function — so
`game_state` never advanced past 40.

### Fix (generator, one cfg line)
`recomp/bank02.cfg`:

```
indirect_dispatch 817b 5 ptrcall targets:891A,8924,8925,892B,8932
```

`$0012` is a WRAM word holding the demo scene's `demo_code_ptr`, so this is the
pointer-sourced CALL (`ptrcall`) form. The 5 targets are the complete distinct
`demo_code_ptr` set across real demo_sets 0–3 (from the `$82:876C`
`kDemoRoomData` table) == the decomp `CallDemoRoomDataFunc` switch cases
(`sm_82.c:63-72`): `891A` ChargeBeamRoomScroll21, `8924` nullsub_291, `8925`
SetBG2TilemapBase, `892B` SetKraidFunctionTimer, `8932` SetBrinstarBossBits.
The authorized dispatch preserves the fall-through, so `++game_state` now runs.

### Verification (recomp-vs-oracle `game_state` timeline diff)
The diagnostic technique that cracked this: align a single semantic variable
(`$0998` game_state) across the two traces even though whole-WRAM doesn't
align. After the fix:

| state | RECOMP frames | ORACLE frames |
|-------|---------------|---------------|
| gs 0  | 1..151        | 1..272        |
| gs 1  | 151..2619     | 272..2813     |
| gs 40 | 2619..2687 (**68**) | 2813..2970 (**157**) |
| gs 41 | 2687..2689    | 2970..2972    |
| gs 42 | 2689 (crash)  | 2972..3100    |

`build/last_run_report.json`: `unresolved_abandons.total_hits = 0` (was 1 at
`$0FE8B7`); tier2 fired clean. The `$0012=$FFFF` / `DB=00` chain is gone.
gs 40→41→42 now matches the oracle and the demo enters gameplay.

**Committed on `investigate/sm-0012-blocker`** (game repo): the `bank02.cfg`
directive + the `SM_DEMO_DB_DIVERGENCE.md` correction.

---

## Current blocker (2026-06-18): `WriteEnemyOams` infinite loop at first demo-gameplay frame

### Symptom
The demo now reaches `GameState_42_PlayingDemo` → `GameState_8_MainGameplay`,
but crashes at the first gameplay frame (~f2689):
`WriteEnemyOams` spins → `WATCHDOG: Frame 2689 exceeded 5.0s` → SEH
`code=0xC0000028` (`STATUS_BAD_STACK`).

- Crash stack: `WriteEnemyOams_M0X0` ← `DrawSamusEnemiesAndProjectiles_M0X0`
  ← `GameState_8_MainGameplay_M0X0` ← `GameState_42_PlayingDemo_Async_M0X0`.
- Loop blocks `$20:9508 / $20:9538 / $20:9571` (`PB=$A0`; `$20`/`$A0` are LoROM
  mirrors). These fall **inside** `WriteEnemyOams` (`$A0944A`).
- At crash: `X=$DF01`, `Y=$271D`, `DB=$CC`, `m=0`, `x=0` — garbage indices.

### Root cause (confirmed by code reading + the gs40 frame shortfall)
The loop is the **extended-spritemap walk** in `WriteEnemyOams`
(`sm_a0.c:1939-1962`):

```c
int n = *RomPtrWithBank(E->bank, E->spritemap_pointer);  // count byte
do { ... v5 += 8; } while (--n);
```

`E = gEnemyData(cur_enemy_index)`. The enemy slot holds **garbage**
(`extra_properties & 4` set → extended path; `E->bank=$CC`;
`E->spritemap_pointer` garbage → garbage count `n`), so the `do/while(--n)`
never terminates.

The enemy slots are garbage because the **enemy/PLM/eproj instruction-list
interpreter dispatches are SUPPRESSED**:

```
JSR ($0FA8,X) / ($0FAE,X) / ($0FB0,X) / ($0FB2,X)  — banks $22-$2A ($A2-$AA)
```

`$0FA8..$0FB2` are **WRAM** per-object function pointers: in bank `$22` (a
`$00-$3F` bank), `$22:0FB2` mirrors WRAM `$7E:0FB2`, and `JSR (abs,X)` reads the
target pointer from `PB:(operand+X)`. So these are the per-enemy "current AI
handler" pointers — the routines that run each enemy's **init** instructions
(set `spritemap_pointer`, `bank`, `extra_properties`, AI handler, …) and its
per-frame AI.

Under `cfg-required-dispatch-or-kill` the decoder severs the fall-through and
codegen emits only a comment → the function silently returns `NORMAL`. So:
- enemy init never runs → enemy fields stay garbage → `WriteEnemyOams` crash;
- and gs 40 runs **68 frames vs the oracle's 157** (the skipped enemy
  processing during `StartGameplay_Async` is most of the missing ~89 frames).

### Scope of the suppressed-dispatch class
`111` `Call indirect SUPPRESSED` sites across 17 banks in `src/gen/`:

```
bank06:12  bank22:12  bank26:11  bank04:9   bank28:8   bank10:8
bank23:7   bank02:8   bank33:6   bank0f:6   bank11:6   bank2a:5
bank29:4   bank25:3   bank32:3   bank24:1   bank0b:2
```

Most are `JSR ($0FAx,X)` enemy/PLM/eproj instruction interpreters; also
`$02:8C09/$02:8C2B`, `$0F:E89D/$0F:E8C8`, etc. Each emits a silent
`return RECOMP_RETURN_NORMAL` on hit (no recorded abandon → invisible in the
report). The targets are **runtime WRAM pointers** — they CANNOT be statically
enumerated, so the existing `indirect_dispatch … ptrcall targets:…` (enumerated
value-switch) form cannot express them.

---

## Fix design (next session) — runtime indirect dispatch

The runtime already has the complete machinery in
`runner/src/cpu/cpu_state.c`:

- **`cpu_dispatch_pc_from(cpu, pc24, entry_s_for_miss_restore, source_pc24)`** —
  binary-searches `g_dispatch_table` for the function entry at `pc24`, calls the
  correct `(m,x)` variant, falls back to the LoROM bank-mirror, and on miss
  restores `cpu->S` and returns `NORMAL` (controlled unwind). This is exactly a
  true runtime indirect call.
- **`g_dispatch_log`** — an always-on 1024-entry ring recording every dispatch
  `(pc24, source_pc24, func_name, mx_idx, found, mirror, frame)`. Queryable via
  `cpu_dispatch_log_count()` / `cpu_dispatch_log_at(i)`.

**Plan:** route the (currently-suppressed) **reachable** indirect-call sites
through a runtime dispatch instead of suppressing them:

1. **Decoder:** for a reachable `JSR (abs,X)` whose pointer base is a WRAM/DP
   address (operand `< $2000`) and which has no static target table, **preserve
   the fall-through** and mark a new `dispatch_runtime` form (no enumerated
   entries). Keep the phantom-SMC suppression for non-WRAM / garbage operands
   (the boundary guarded by `tests/v2/test_decoder_smc_phantom_suppression.py`
   must stay green).
2. **Codegen:** at such a site, read the pointer word from WRAM at
   `operand + X` at runtime, then
   `return`/fall-through via `cpu_dispatch_pc_from(cpu, (bank<<16)|ptr, _entry_s, site_pc24)`.
   This handles the open target set, records every call in `g_dispatch_log`,
   and unwinds cleanly on a miss.

Two shapes were considered:
- **(A) per-site cfg** — a new `indirect_dispatch <site> runtime ptrcall` form
  (no targets), ~111 lines. Explicit, low risk, but manual and not robust to new
  banks.
- **(B) general decoder auto-policy** — auto-authorize WRAM-pointer-base
  `JSR (abs,X)` as a runtime dispatch. **Recommended** per the
  "always pick the most complete option" rule: it covers every site (present and
  future) with one change. Gate on `operand < $2000` to exclude phantoms.

**Also recommended (observability, completeness):** dump `g_dispatch_log` (and a
suppressed-hit counter, if any remain) into `build/last_run_report.json`
alongside `dma_events` / `trace_recent`. The TCP debug server is unusable for SM
(the process dies in ~30s before a socket lands), so the post-mortem report is
the only always-on ring we can read after a crash. The ring (1024 entries) only
covers the last window before the crash; size it up or window it if gs40-era
dispatches need to be inspected.

---

## Reproduce / tooling

```bash
cd /f/Projects/snesrecomp/SuperMetroidRecomp
export PATH=/c/msys64/mingw64/bin:$PATH        # required for ALL SM builds

# Regen (ONLY if cfg/codegen changed) — FULL regen (never --banks; partial
# breaks cross-bank variant refs), then sync funcs.h:
python3 snesrecomp/tools/v2_regen.py --rom "Super Metroid (Japan, USA) (En,Ja).sfc" --cfg-dir recomp --out-dir src/gen
python3 snesrecomp/tools/v2_sync_funcs_h.py --cfg-dir recomp --out recomp/funcs.h

# Build (incremental):
cmake --build build -j 8

# Free-run the attract demo (RULE 0: no pause/step). Crashes ~f2689 now:
SNESRECOMP_WRAM_TRACE_FILE="$(pwd)/sm_wram_fix.jsonl" ./build/SuperMetroidSNESRecomp.exe "Super Metroid (Japan, USA) (En,Ja).sfc"
#   build/last_run_report.json = SEH post-mortem (cpu, recomp_stack, abandons,
#   tier2_coverage, stack_balance, dma_events, trace_recent, …)

# Oracle (snes9x libretro) headless capture for diffing:
cd /f/Projects/snesrecomp/snesrecomp/tools/snesref
SNESREF_FRAMES=3100 SNESREF_TRACE_FILE="/f/Projects/snesrecomp/SuperMetroidRecomp/_oracle_demo.jsonl" \
  ./snesref.exe snes9x_libretro.dll "/f/Projects/snesrecomp/SuperMetroidRecomp/Super Metroid (Japan, USA) (En,Ja).sfc"

# game_state / semantic-var timeline diff (THE technique):
cd /f/Projects/snesrecomp/SuperMetroidRecomp && python3 _gsdiff_fix.py
```

Scratch (all `_`-prefixed → gitignored; `*.jsonl` traces also gitignored):
`_gsdiff_fix.py` / `_gsdiff.py` (game_state timeline), `_align.py` (landmark
align), `_demoptrs.py` (ROM demo-table reader), `_rep.py` / `_rep2.py` /
`_inspect_report.py` (report inspectors). Traces: `sm_wram_fix.jsonl` (recomp
post-fix), `_oracle_demo.jsonl` (oracle, 3100 frames).

---

## Milestone (2026-06-21): runtime-indirect-dispatch fix SHIPPED + enemy init fixed; crash advanced to a DB-divergence

### What was built (generator + runtime, NOT src/gen)
Implemented the general runtime-pointer `JSR (abs,X)` dispatch (the
suppressed-dispatch class fix). A reachable `JSR (abs,X)` whose pointer-table
base is in WRAM (`< $2000`) is now recovered as a true runtime dispatch instead
of being suppressed:
- **`recompiler/v2/decoder.py`** — UNAUTHORISED `JSR (abs,X)` with operand
  `< $2000` → marked `dispatch_runtime`, fall-through PRESERVED (vs severed).
  ROM-operand phantoms (`>= $2000`, e.g. `$EA1D`) stay suppressed; the
  `test_decoder_smc_phantom_suppression` boundary is intact.
- **`recompiler/v2/codegen.py`** `_emit_runtime_dispatch` + **`emit_function.py`**
  routing — reads the live WRAM pointer + calls `cpu_dispatch_call_pc`.
- **`runner/src/cpu/cpu_state.c`** `cpu_dispatch_call_pc` — pushes the 2-byte JSR
  frame, dispatches the live `(m,x)` AOT body (paired host-call), else falls to
  the interpreter tier (`interp_tier_run_call` in `interp_bridge.c`). Always
  balanced; logged in `g_dispatch_log`.
- **`snes65816.py`** — `dispatch_runtime` slot. New test
  `tests/v2/test_decoder_runtime_dispatch.py`.
- **Observability:** engine `CpuDispatchLogDumpJson` (dispatch ring) + SM
  `post_mortem.c` `sm{}` section (game_state, cur_enemy_index, per-slot enemy
  data, live DB/X/Y) → `build/last_run_report.json`.

Regen result: **`Call indirect SUPPRESSED` 111 → 9** (the 9 survivors are
ROM-operand boss/specific-enemy dispatches off the demo path — e.g.
`$24:89E8` is inside `Crocomire_Func_25`). 105 runtime-dispatch sites emitted,
incl. the SM enemy/PLM/eproj `JSR ($0FA8/$0FAE/$0FB0/$0FB2,X)`. Build clean
(0 undefined refs). **UNCOMMITTED** as of this writeup.

### Effect (measured, free-run → post-mortem report; no pause/step)
The demo now reaches **`game_state` 42 (PlayingDemo) → MainGameplay →
DrawSamusEnemiesAndProjectiles**; enemy/PLM dispatches fire (71 AOT hits + 97
interp-tier). Enemy slots now hold **VALID** data (was garbage `bank=$CC`
before; now `bank=$A2`, valid `spritemap_pointer`, 3 populated slots). **Enemy
initialization is fixed.**

### Remaining crash (NEW root cause — a DB-divergence, NOT garbage data)
Still STATUS_BAD_STACK in `WriteEnemyOams_M0X0` at f2689, but the cause changed.
WriteEnemyOams reads ALL enemy fields **DB-relative** (`cur_enemy_index` via
`cpu_read16(cpu, cpu->DB, 0x0E54)`; `extra_properties` from `DB:$0F88+X`; the
extended-spritemap count from `DB:spritemap_ptr`). At the crash **`DB=$CC`** (a
ROM bank that does NOT map WRAM) → every enemy read returns ROM garbage →
`extra_properties & 4` spuriously set → wrong (extended) branch → garbage loop
count → infinite loop → watchdog.

DB/PB ring (`dbpb_recent`) shows the mechanism precisely: WriteEnemyOams sets DB
via `LDA enemy_bank,X (DB-relative); PHA; PLB; PLB` at `$2094 52/53`. For the 3
real enemies (`X=0/0x40/0x80`) this yields `DB=$00` (WRAM) → else-branch
`DrawSpritemapWithBaseTile` → fine. A **4th** call (`X=0xC0`, no real enemy)
reads `$CC8A` → `DB=$CC` → loop. DB is already wrong (`$74`, ROM) *entering*
the draw path, so the bank field itself is read from ROM. Stack pointer is
balanced across all four ($1FF0/$1FF1) — it's the stack/DB *values* (and/or a
draw loop iterating one enemy too many) that are wrong.

Pre-existing (the handoff listed "WriteEnemyOams loop" as the next bug *before*
this dispatch work); the dispatch fix corrected enemy DATA but the enemy
draw-path **DB-divergence** remains. Lead: find why `DrawSamusEnemiesAndProjectiles`
→ `WriteEnemyOams` runs with `DB=$74/$CC` instead of a WRAM-mapping bank, and/or
why a 4th enemy is drawn. Repro: free-run, read `sm{}` + `dbpb_recent` +
`dispatch_log` in `build/last_run_report.json` (scratch parsers `_rpt*.py`).

## Milestone (2026-06-22): WriteEnemyOams f2689 freeze FIXED (interp-tier AOT-bounce paired-ABI)

### Root cause (measured, not inferred)
The `DB=$CC` in WriteEnemyOams was downstream of a **+2 `cpu->S` over-pop** that
leaked `DB=$74` into the enemy-queue draw phase. Chain:
`DrawSamusEnemiesAndProjectiles ($A0:884D)` sets `DB=$A0`, then at phase 3 calls
`DrawSamusAndProjectiles ($90:EB35)`. Inside it, `SamusDrawSprites ($90:EB4B)`
tail-dispatches the Samus draw handler via `JMP (samus_draw_handler)` at
`$90:EB4E` — an **unresolved IndirectGoto → interpreter tier**
(`interp_tier_dispatch_balanced` → `interp_bridge_run_ex`). The bridge
**AOT-bounced** the handler's sub-calls via `cpu_dispatch_pc` (**dispatch ABI,
hrv=0**), whose callee RTS **re-dispatches on the popped return address**. The
first such address — `$90:EB55`, immediately after `HandleChargingBeamGfxAudio`'s
JSR — is ALSO a registered function entry (`sub_90EB55`), so the dispatch HIT it
and ran the next routine as part of the callee's "return", over-popping `cpu->S`
by 2 (bridge probe: `sp_pre=$1FEB → sp_post=$1FEF`, should be `$1FED`). Every op
after ran 2-low; `$90:EB48 PLB` then read the JSL return-low byte `$74` instead
of the PHB'd `$A0`. (The stack-balance auditor mis-pointed at `Samus_ShootCheck`
because it counts the return-frame pop; the always-on S-boundary probe settled it.)

### Fix (general, runtime-only, NO regen)
- **`runner/src/cpu/cpu_state.c`** — new `cpu_dispatch_pc_paired(cpu, pc24, frame_size)`:
  the interp already pushed the return frame, so run the target with
  `host_return_valid = frame_size` and let its RTS/RTL **host-return to the
  bridge** (frame popped, S restored) instead of re-dispatching on the popped
  return address. Logged in `g_dispatch_log`.
- **`runner/src/snes/interp_bridge.c`** — the `interp_bridge_run_ex` AOT-bounce
  now calls `cpu_dispatch_pc_paired` (frame = JSL?3:2) instead of
  `cpu_dispatch_pc`. A non-NORMAL return (NLR that unwound past the call) stops
  the bridge instead of force-resuming at `ret`. Fixes the over-pop for ALL
  interp-tier AOT-bounces, not just this site.
- **Env-gated probes added (reusable, default off):** `SNESRECOMP_SBOUND=lo-hi`
  (logs `cpu->S`+DB at every block in a pc24 range — cpu_trace.c);
  `SNESRECOMP_IBRWATCH=lo-hi` (per-step interp-bridge trace incl. AOT-bounce
  sp/return — interp_bridge.c).

### Verified
Demo runs **f2689 → 4086+ with no crash** (was a hard freeze at f2689). Stack
balanced: `$90:EB3E` S=`$1FEF` (was `$1FF1`); `DB` stays `$A0` through the draw
loop. Samus drawn every frame (6195 probe samples), process healthy. **UNCOMMITTED.**
Caveat: `stack_balance` in `last_run_report.json` counts the frame pop (balanced
JSR=+2, JSL=+3) — not a true-leak signal; use the S-boundary probe.

## Milestone (2026-09-11): Start at title -> file select crash FIXED (scheduler-frame watermark)

### Symptom
Pressing Start on the title screen: `[interp_cap] entry=$809589 last=$808573`
(NMI handler spinning in `InvalidInterrupt_Crash $80:8573`), then
`entry=$5C0080` garbage PCs, `sp=$0002/$FFF7/...`, `Warning! DMA from addr
0x950795`, `[sm_rtl] LLE loop bailed`. All downstream.

### Root cause (measured)
Headless repro (`SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy --script`, Start
for 8 frames at frame ~600 = title) with `SNESRECOMP_INTERP_TRACE=1`:
`[interp_bridge] yield-mode NLR exit (non-unwind) _air=1 target=$8091A9
frame=633 site=$818DED sp_pre=$1FF0 sp=$1FF6 s_enter=$1FEC`.
Game state 1 -> 4 (FileSelectMenus). `FileSelectMenu_0_FadeOutConfigGfx`
($81:944E) calls `WaitUntilEndOfVblankAndClearHdma` -> (JSR) `WaitForNMI`;
the whole-program LLE frame resumes INSIDE that WaitForNMI at S=$1FEC, so
`s_exit`/`s_interp_owner_exit_s` = $1FEC. WaitForNMI returns, and
`LoadInitialMenuTiles` ($81:8DDB, interpreted, M1X1 after `SEP #$30`) does
`JSL SetupDmaTransfer` ($80:91A9) at S=$1FF0. SetupDmaTransfer is compiled for
M1 and rewrites its return address (+8 inline parameter bytes); its RTL took
`interp_tier_dispatch_rewritten_return`, whose `crossed_into_compiled_ancestor`
saw post-return S ($1FF0..) above the owner watermark ($1FEC) and concluded the
rewrite belonged to a compiled ancestor. It ran the continuation in a NESTED
tier frame and returned SKIP_1; the yield-mode bounce site then took the
"non-unwind NLR" exit, reported the frame as complete, and the next host frame
injected NMI at the stale resume PC over a half-unwound stack -> garbage ->
BRK/COP -> `$80:8573`.

The watermark is meaningless for a scheduler/whole-program frame: it entered at
whatever S the previous frame yielded from, and its program legitimately runs
above that S every frame (the RTS watermark was already disabled for
`yield_pc` mode for the same reason; the three owner-crossing checks were not).

### Fix (class, runtime-only, NO regen) — `snesrecomp/runner/src/snes/interp_bridge.c`
- `s_interp_owner_is_scheduler` recorded per bridge frame (`yield_pc != 0`);
  `interp_owner_crossed(post_s)` is the single predicate, false for scheduler
  owners. Used by `interp_tier_dispatch_rewritten_return`,
  `interp_bridge_return_targets_owner`, and the bounce-site `_skip_crossed_owner`.
- A scheduler frame that still receives an unconsumable SKIP_N now bails
  contained (`return 0`) instead of reporting a completed frame; `sm_rtl.c`
  honours `g_game_done` in the LLE path so a bail stays stopped (it used to
  re-run frames from the stale PC every frame, executing garbage).
- Diagnostics kept: the low-exec tripwire dumps the always-on 8192-step ring
  (the 64-entry local window was all `$FF` padding); the yield-mode NLR log now
  carries frame/site/sp/s_enter.
- Regression test `S8d` in `tests/interp816/bridge_test.c` (harness now models
  `g_recomp_stack_top` like the real `RecompStackPush`, without which the
  crossing branch was untestable). Fails without the fix (aot_called=124),
  passes with it: 104/104.
- `src/post_mortem.c`: creates `build/` and reports a failed report write —
  `build-release/` had no `build/` subdir, so the "always-on" post-mortem had
  been silently writing nothing.

### Verified (instrument state; on-screen verdict pending Alex)
Headless 1250-frame run, Start at 602/810/1010: game_state 1 -> 4 (632) -> 2
(890) -> $1E (1020), zero caps / low-exec / NLR / bails / DMA warnings.
WRAM low-8K trace pre- vs post-fix identical through frame 632 except stack
bytes and one 2-frame-cadence counter ($064B) that pre-fix stepped a frame
early (pre-fix also spent a 2M-step cap in frame 0: boot now 0.6 s vs 2.7 s).
Not verified: pixels (no headless capture path; earlier windowed runs stole
focus and ate keystrokes — use the dummy SDL drivers for repro runs).
Framework v2 suite 399/402 (3 pre-existing `test_emit_function_smoke`
failures, emitter-side, untouched). **UNCOMMITTED.**

## 2026-09-12 — the host is the framework's; main.c is a shim

`src/main.c` (2,849 lines: launcher flow, ROM resolution, config, window,
SDL/GL presenters, audio, gamepads, overlays, pacing clock, crash pipeline,
scripted input) moved up into snesrecomp as `runner/src/desktop/host_main.c`
(+ `host_clock.c`, the former SmClock with the rate as a parameter), linked
by `snesrecomp_target_desktop_host(<target> [TIER2])`. main.c is now 250
lines: a `SnesDesktopHostGame` descriptor with identity from
`snesrecomp_rom_identity.h` and SM's hooks (custom renderer via
prepare_frame/draw_frame, presentation rate, door-transition pacing debt,
SPC player, Mods provider, SM_AUDIO_PROBE). The new-project templates
(`main.c.in`, `host_contract.c.in`, `CMakeLists.txt.in`) produce the same
shape, so the second on-disk scaffold (`SuperMetroidSNESRecomp`) rendered
file select, options and the intro on the first build against this host —
it had presented a black frame before because the working host lived only
here.

Verified: guest WRAM trace byte-identical to the pre-change binary over a
1,900-frame scripted boot to game_state 8 (`multi.txt`, dummy drivers);
identical again with the save-state browser opened, saved, loaded and closed
at frame 700 (SNESRECOMP_OVERLAY_SELFTEST) — panel dumped at 512x448;
screenshots at frames 700/1300 identical pixel counts between this repo and
the scaffold; windowed SDL and OpenGL presenters both initialize and present
(x11); ctest 8/8, framework v2 401/404 (the 3 pre-existing
`test_emit_function_smoke` failures), new `test_host_clock` registered.
Headless screenshots: `SNESRECOMP_SCREENSHOT=<ppm> SNESRECOMP_SCREENSHOT_FRAME=<n>`.

## 2026-09-12 — overlays with a controller: "the save-state menu freezes the game"

Report (DualSense, both repos): the browser opened by Select+R and then
nothing happened; rewind never opened. Causes, both in the host (now
framework-owned, so one fix): (1) the overlay modal pumps handled quit and
keyboard events only, so the pad's d-pad/B/shoulders never reached the panel
-- a keyboard player never saw it and the word-injecting self-test enters
below the event layer, so it never saw it either; (2) nothing bound rewind:
the framework left SaveStateMenu/Rewind unbound (F7/F8 collide with LoadState
slots), this repo's config.ini did not bind them, and the host had no pad
gesture. Fixes: one HandleDeviceEvent for the main loop and both pumps;
`[Controller] RewindGesture` (Select+R3 default) parsed by the host; default
keys F11/F12 in the framework config; pad-bound system commands dropped while
a panel is up; buttons held when a panel closes masked until released (the
closing B leaked a jump). Test: `SNESRECOMP_OVERLAY_SELFTEST_PAD=<frame>`
attaches a virtual gamepad and drives both panels through SDL events; the
first version of it passed vacuously (a real pad took player 1) and then
pressed the SNES A instead of B -- both caught by the guest trace, which must
be byte-identical with the test armed.

## 2026-09-12 — a fresh scaffold did not boot: rendered from the wrong wizard

Studio scaffolded Super Metroid again from scratch; it pinned snesrecomp main
(host unit, shim templates) but its main.c/game_rtl.c/host_contract.c were
the OLD templates: Studio ran the wizard from the sibling `~/GitHub/snesrecomp`
checkout, which sits on a months-old branch, and rendered from that copy while
the submodule came from the remote. The old game_rtl.c never delivers NMI, so
game_state never left 0 (black frame; screenshot + WRAM trace). Fixes:
snesrecomp bdcd4f5 (the wizard re-renders every template from the submodule
it just pinned, takes the recomp-ui ref from there too, and a wizard older
than the framework it pins fails with the token named); Studio 5077ed5 +
ede4e80 (re-vendored wizard; new projects scaffold from the vendored copy
unless SNESRECOMP_ROOT is set). Verified with a deliberately stale wizard copy
(shim rendered, pinned main) and by re-rendering the on-disk scaffold from its
pinned framework: boots to file select at frame 700.

## 2026-09-12 — one frame of full-screen garbage: HDMA ran twice per HBlank

`~/Videos/flicker.mp4`, 9.8 s of the Ceres intro: at 8.15 s a single frame
replaces the whole play field with a repeating pink tile pattern while the HUD
stays intact. Everything else in the clip is clean — the background aligns to a
pure scroll between consecutive frames, the play area geometry never moves, and
mean luma is flat apart from that one frame (68.9 against neighbours at 31.6).

### What the picture was

The Ceres shaft is drawn in **mode 7 below a mode-1 HUD**, the switch being
HDMA channel 3 writing `$2105` at scanline 31. On the bad frame that write
never took, so mode-7 VRAM — interleaved tile and map bytes — was rendered as a
mode-1 tilemap. That is the pink pattern.

### Root cause (framework)

Two HDMA engines were running. This port's `SmDrawPpuFrame` walks the real HDMA
tables per line (`SimpleHdma_*`), and the framework's beam ALSO ran
`dma_doHdma` from `snes_advance_beam`. The framework has a gate for exactly
this (`snes_set_hdma_beam_enabled`, snes.h documents it), but the gated call was
added *beside* the ungated one it was meant to replace (snesrecomp fd173d2,
2026-08-28) instead of replacing it, so the gate gated nothing and, with the
beam owning HDMA, every table was consumed **twice per HBlank**.

The second pass re-ran channel 3's table from the frame's first entry (mode
`$09`) while this host's raster loop had already advanced to the second (mode
`$07`). It fires from inside guest register writes — `STY $4209` in the IRQ
vector syncs the master clock, the beam advances, and the beam runs HDMA — so
it lands only when that write happens to cross an HBlank: about one frame in
eighty. A host backtrace at a trapped `$2105` write named the whole chain:
`bank_80_9870_M0X0 → cpu_write16 → WriteRegWord → WriteReg →
snes_sync_master_clock → snes_advance_beam → dma_doHdma → ppu_write`.

### Fix

- snesrecomp: delete the two ungated pre-gate calls in `snes_advance_beam`
  (`dma_doHdma` per HBlank and `dma_initHdma` at field wrap) so the gate works
  and beam HDMA runs once. Regression test in `tests/dma/hdma_timing_test.c`:
  two single-line entries writing different values to one register must leave
  the FIRST after one HBlank, and `hdmaBeamOff` must leave the register
  untouched. It fails on the pre-fix engine and passes after.
- `src/sm_rtl.c`: `snes_set_hdma_beam_enabled(g_snes, false)` at the top of
  `SmDrawPpuFrame` — this loop is the frame's HDMA engine. Set every frame,
  not once at boot: a save-state load restores the `Snes` struct it lives in.
- snesrecomp: `SimpleHdma_Init` now infers that declaration, because calling
  it IS the declaration. Eight ports drive HDMA from their own raster loop
  (`MegaManX`, `MetalWarriors`, `StarFox`, `SuperMarioWorld`, `SuperSmashWorld`,
  `ZeldaAlttP`, the `SuperMetroidSNESRecomp` scaffold and this one) and none of
  them had said so, so all eight had the same exposure. The explicit call above
  is kept as documentation; removing it renders identically.

### Verified

Reproduced headlessly and deterministically (`fuzz0` script, frame 2198): the
present dump shows the garbage frame between two clean ones, luma 113.1 against
52.7/52.5. After the fix that frame's CRC equals its successor's and every
other frame in the range is byte-identical, so the change touches the corrupt
frame and nothing else. Across ~70k scripted frames the class is gone; the
anomalies that remain are `hdmaen=00` frames where the guest itself changes
BGMODE for one frame during a door transition.

### Tooling added

- Framework: `SNESRECOMP_SCREENSHOT_DIR` (+ `_FROM`/`_TO`) dumps every
  **present** as `present_NNNNNN.ppm` with a `presents.csv` of present, frame,
  interpolation weight, CRC32 and mean luma; `SNESRECOMP_PRESENT_LOG` writes
  the CSV alone, so a long session can be scanned for the one present that is
  wrong before dumping any pictures. A flicker is a claim about the relation
  between consecutive presents, and a per-simulated-frame dump hides exactly
  the pair that differs.
- `SM_RASTER_PROBE=<path>`: the shape of each rendered frame (IRQ schedule,
  TM/BG3SC left behind, HDMA mask armed, per-line BG-mode run-length), written
  on change plus a line for any frame differing from BOTH neighbours.
- `tools/snesref` now builds and runs on Linux (dlopen instead of
  LoadLibrary), so the oracle's frame dumps are available beside the recomp's.

## 2026-09-12 — run-ahead: the picture now comes from the speculation

Asked to check whether run-ahead did anything, it did not. Over 1,195 frames
the presented picture with `RunAhead = 1` was byte-identical to the picture
with it off, on the same frame; a working run-ahead shows the frame AFTER.
Meanwhile it cost 0.7 ms and a 324 KB snapshot save+load per frame, and left
guest state changed on 21 frames in 1,200.

### Three defects, in order of discovery

1. **The picture was redrawn after the rewind.** `snes_runahead_run_frame`
   speculated and rewound inside the guest step, but this host draws
   afterwards, in `draw_ppu_frame` from whatever state the guest is then in --
   the rewound state. The speculative rendering was thrown away. Proof at the
   time: raster time per frame was unchanged with the feature on (2.663 vs
   2.666 ms), so the picture was rasterised exactly once, after the rewind.
   Run-ahead now takes a capture callback from the host and calls it on the
   last speculative frame, before rewinding.

2. **The real frame lost its raster side effects.** Moving the capture into
   the speculation was not enough: this port's raster pass RUNS GUEST CODE --
   the HUD/room split dispatches the game's own raster IRQ handlers, which
   write WRAM, and during a door transition move the camera and Samus. With
   only the speculative pass running, the rewind took those writes away with
   the speculation. The callback now runs twice: once on the real frame for
   its side effects (picture discarded, before the snapshot so the writes are
   inside it), once on the speculated frame for the picture.

3. **The SPC700 was never rewound.** `Apu.portClock` and the port anchors sit
   deliberately AFTER the region `apu_saveload` serialises -- host-side lead,
   not machine state -- so a rollback does not put them back. Every
   speculative frame therefore advanced the audio chip permanently: measured,
   the SPC had executed 73% more cycles after 232 frames, and the game's
   sound-effect queue stepped a frame early. Fixed upstream by not driving the
   APU on a speculative frame at all (`rtl_sync_apu_frame_boundary` is gated
   on `!g_rtl_speculative_frame`), which is both correct -- that audio is
   discarded regardless -- and cheaper.

### And the execution position, which had to move too

A guest snapshot holds the machine, not where the game is in its own code.
This port has two execution models and neither was in a rollback: the LLE
bridge resume PC (`g_lle_resume_pc`, a static here) and, in the recompiled
modes, the whole C call chain on the game fiber. New `RtlGameInfo.exec_state_*`
hooks carry both in the rollback blob (in-process, in-memory, this build only
-- never a file); `FiberSnapshotSave/Load` in the framework's fiber_compat
copies a suspended ucontext fiber's live stack and context back to the same
addresses, so every interior pointer stays valid. Win32 fibers are opaque and
an Android fiber is a real thread: both report unsupported and run-ahead
declines there rather than rewinding the machine out from under a fiber that
stays put. `sm_spc_player`'s ports and APU RAM image ride along, being host
state the guest talks to.

### Verified

Over a 1,200-frame script, with `RunAhead = 1`: the presented picture equals
the no-run-ahead run's NEXT frame on 1,196 of 1,198 comparable frames (the
last frames have no successor), and the per-frame guest state -- WRAM CRC plus
the CPU register file -- is IDENTICAL on all 1,200. Same at 2 and 4 frames of
look-ahead (N+2 on 1,196; N+4 on 1,187), and in the `on` and `force` execution
modes, where the fiber snapshot is the one doing the work and its size tracks
the guest's call depth. Cost, unpaced: 3.67 ms/frame to 5.73 ms/frame, wall
clock 5.63 s to 8.44 s for 1,200 frames. That is the honest price of one frame
of look-ahead -- two guest frames, two raster passes and a ~1.1 MB snapshot
per displayed frame -- against a 16.6 ms budget.

`snesrecomp/tests/host/fiber_snapshot_test.c` covers the fiber half: a fiber
suspended several frames deep in a recursion is snapshotted, run forward,
restored, and must carry on from the restored point.

## 2026-09-12 — "Warning! DMA from addr 0x9a0000" is the game, wrapping a bank

Printed on every run, and benign. At frame 1020 Super Metroid programs a 16 KB
VRAM upload from `$9A:D200` through `SetupDmaTransfer` ($80:91A9), from a
record that is literal ROM data at `$82:8319` (`01 18 00 D2 9A 00 40`). Bank
`$9A` has 11,776 bytes left from that address, so the transfer wraps at the
bank boundary and spends its last 4,608 bytes at `$9A:0000` -- which on this
LoROM cartridge is the WRAM mirror. A DMA's A-bus address wraps within its
bank and never carries into the next one, so hardware reads the same bytes,
and the recompilation is faithful.

The framework's check sat in the per-byte transfer loop, asking whether a
`$80+` bank was being read below `$8000` -- exactly what an authentic wrap
looks like partway through, which is why the reported size (4,608) was the
remainder rather than the programmed 16 KB. Fixed upstream by asking once,
where the channel is armed, from the address the game programmed.

Two things this cost while it stood: the report was on stdout while the host's
breadcrumbs are on stderr, so in a merged log it appeared beside "first frame
simulated" for something that happened at frame 1020; and it set `g_fail`,
the latch that also gates the off-rails ROM-pointer report, so one false
positive silenced a real diagnostic for the rest of the session.

Found alongside a regression of mine in the same log: run-ahead's rollback was
reading as a timeline jump, so the host tore down and rebuilt its 17.5 MB
rewind ring every frame (that is the repeated "[snes_rewind] 60 snapshots
every 6 frames"). Rewind kept no history at all while run-ahead was on. Also
fixed upstream, by carrying the state generation across a rollback.

## 2026-09-12 — CI is the shared release workflow now, and this port can build a setup host

`.github/workflows/release.yml` was a per-repo file that built nothing
releasable: with no ROM in CI there is no `src/gen`, so it compiled two
translation units with `cc -fsyntax-only` and published a release with notes
and no artifacts. It is now the wizard's `templates/release.yml.in` rendered
for this game (`Super Metroid`, `SuperMetroidSNESRecomp`, `supermetroid`),
byte-identical to what `tools/new_project` emits — human-triggered only, four
platforms, version resolution from tags or `VERSION`, tag-after-build, and
setup packs with SHA256SUMS.

Two things had to follow it, because a workflow that calls scripts this port
does not have is not a deliverable:

- `scripts/package_release.sh` is the template's too. The old one packaged the
  *game* executable from `build/` and took no arguments; the workflow passes a
  build directory, a platform tag and `--embed-toolchain`, and expects a setup
  pack named `supermetroid-<version>-<platform>.zip`. The only local edit is
  the absence of `--runtime-dir mods`: this port has no mod catalog (its
  presentation mods are compiled in, `src/sm_mods.c`), and the staging tool
  fails loudly on a named directory that is not there.

- The port can now build with `-DSNESRECOMP_SETUP_HOST=ON`, which is what the
  workflow builds. `CMakeLists.txt` routes `src/gen` through
  `snesrecomp_target_generated_code()` instead of globbing it into the source
  list, so the framework owns both outcomes; `src/gen_stubs.c` joins the build
  only when there is generated code to go with it. `src/sm_rtl.c` was the one
  file coupled to the generated tree — it includes `funcs.h` and calls
  `I_RESET` / `I_NMI` / `I_IRQ` — and those are behind
  `#if defined(SNESRECOMP_SETUP_HOST)`, replaced by bodies that name the entry
  point and abort. Unreachable by construction: `SnesInit()` refuses to boot a
  guest in a setup host, so the only path through that binary is the
  launcher's Generate & rebuild.

Verified locally end to end: the setup host configures, builds and packages
(`dist/supermetroid-0.1.0-linux-x64.zip`, 16.6 MB, no `.sfc`, no `src/gen`, no
`funcs.h` in it); `-DSNESRECOMP_SETUP_HOST=ON` against a tree that still has
generated C is refused by the framework, as it should be; and the ordinary
build is unchanged — ctest 8/8 and a 1,200-frame guest trace byte-identical to
before the change.

## 2026-09-19 — three bugs behind one cfg generator, and the renderer this port never ran

A day driven from live play on the TCP debug server rather than from scripts:
Ceres escape rendering, then framerate, then the Morph Ball freeze. Two of the
three trace back to the same generator defect; the third was a dead config
gate. The Morph Ball freeze is diagnosed but NOT fixed — see Open items.

### `ingest_sm_decomp.py` ends every function at the next symbol

`tools/ingest_sm_decomp.py:270`:

```python
next_addr = filtered[i + 1][0] if i + 1 < len(filtered) else 0x10000
section_lines.append(f"func {name} {addr:04x} end:{next_addr:04x}")
```

Every `end:` is the next harvested decomp symbol, and `0x10000` for the last in
a bank. The script has no notion of where a function actually ends, so every
gap between symbols — data tables, inline call arguments, padding — is
swallowed into the function above it and translated as code. An audit over all
5,668 `func` declarations found **105 spanning >= 0x400 bytes**, worst cases
`$93:834D-$10000 DrawBombAndProjectileExplosions` (0x7CB3) and
`$89:ACC3-$10000 RoomCode_CeresElevatorShaft` (0x533D).

Two of today's bugs are instances. Both fixes are **hand-declared overrides
placed ABOVE the `# >>> AUTO-INGESTED` markers**, which is the mechanism the
script supports (`hand_pcs` suppresses ingested entries at the same PC). Edits
*inside* the markers are lost on the next ingest, and worse, make the script
refuse the whole bank with `SKIP ...: move them OUTSIDE the markers`. Both
corrections were first written inside the block and had to be relocated.

### Ceres escape: the room rendered as a filler quilt for ~95 frames — FIXED

Re-entering the Ceres elevator shaft during the escape showed a repeating
chevron field instead of the room, for 92-97 frames across every run, then
snapped to the correct room and the tilt animated normally.

The evidence ruled out the renderer twice over: per-line PPU state was
identical between a broken frame and a correct one (225 lines, 0 differences),
Mode 7 VRAM was identical (0/16384 character bytes, 4/16384 tilemap), and both
Mode 7 samplers — `ppu.c:PpuDrawBackground_mode7` and
`ppu_legacy.c:ppu_prepare_mode7`/`ppu_sample_mode7` — were proven equivalent
offline over ~300k inputs (identity, scroll and centre sweeps, 200k random
rotation matrices, all four flip combinations).

Tracing `$211B`-`$2120` gave the matrix: **A=256, B=0, C=0, D=112, X=128,
Y=1008**, against `DoorCode_CeresElevatorShaft` (`$8F:E4E0`,
`refs/snesrev-sm/src/sm_8f.c:605`) which specifies **D=256**. Five of six
values matched the ROM exactly; D was wrong, and D alone moves the sampled band
from map y 512-735 (inside the room, which spans 0-767) to y 791-889 — 100%
filler tiles 152-155.

The writer, from a WRAM watch on `$7E:007E` (`reg_M7D`): a 16-bit store of
`X = 0x0070` over the correct `0x0100`, from block `$88:D845`. That block
exists because `$88:D865` is `JSL SpawnHdmaObjectToSlot0xA` followed by four
bytes of **inline argument data** (`43 11 d0 d8`, the `SpawnHdmaObject_Args` in
`sm_88.c:2221`) that the callee consumes off the return address. Decoded as
code those bytes read `EOR $11` / `BNE $D845`, and that phantom branch stored X
into direct page `$7E` and jumped to `$88:4915` — not a ROM address under
LoROM, which is why it surfaced as an unresolved goto in the very first minute
of the session and was set aside for hours.

Fix in `recomp/bank08.cfg`: `SpawnBG3ScrollHdmaObject` ends at `d869`,
`data_region 88 d869 d86d`, and `HdmaobjPreInstr_WaterBG2XScroll_Func1` ends at
`c645` (it is 0x0F bytes, declared as 0x122F). Verified: `L_D845_M0X0` and
`0x884915` both now appear 0 times in `src/gen/bank88_v2.c`, `unresolved_goto`
reports 0 hits at runtime, and the tilt renders correctly from the first frame.

Note for the next person: `data_region` alone does NOT stop linear decode. It
is consumed by the dispatch-table reader and the auto-promote pass
(`decoder.py:569`); bounding `end:` is what keeps the decoder out of the data.

### Crateria framerate: `RunAhead = 3`, and 66% of CPU in the reference rasteriser

30 fps on the Zebes surface. Measured 4.01 guest simulations per presented
frame — three once-per-frame NMI functions each ran 1775 times against 443
presented frames — which is exactly `RunAhead = 3` (N+1 simulations). The
tracked `config.ini` ships `RunAhead = 0` with a warning; the live copy beside
the executable had 3. Setting it to 0 restored 60 fps and resolved the report.

`perf record` on the live process then showed where a frame actually goes:
**`ppu_resolve_pixel` 35.2%, `SmRendererDraw` 24.0%,
`ppu_draw_whole_line_legacy` 6.4%** — about two thirds of all CPU in pixel
rendering, and 41.6% of it in the *reference* per-pixel rasteriser.

Cause: `host_main.c:510` read
`uint32 flags = g_game->native_widescreen ? g_ppu_render_flags : 0;`. Neither
render flag is widescreen-specific — `kPpuRenderFlags_NewRenderer` selects the
span renderer, `kPpuRenderFlags_NoSpriteLimits` lifts the sprite cap, and
widescreen is carried by `PpuSetExtraSpace` — so gating the whole word on
`native_widescreen` silently discarded both on every non-widescreen port. For
this title that meant `config.ini`'s `NewRenderer = 1`, the `ToggleRenderer`
hotkey and `no_sprite_limits` all resolved to a value the PPU never saw. The
per-line `renderer` field added today confirms it: all 224 lines reported
`legacy`.

Fixed to pass `g_ppu_render_flags` through. Measured over the same 900-frame
headless run: **3448.90 ms -> 2653.53 ms task-clock, -23%**, with
`ppu_resolve_pixel` and `ppu_draw_whole_line_legacy` gone from the profile
entirely (replaced by `ppu_runLine` at 9.0%). Fidelity: **103 of 103 presented
frames byte-identical**; the only differing file was the timing CSV. Wall time
was identical in both runs because a headless run paces to the simulation
clock — it sleeps rather than saturating, so only CPU time shows the win.

This is a framework change and affects every port that is not natively
widescreen: all of them have been running the reference rasteriser.

### Morph Ball freeze: diagnosed to a wrong M flag — NOT FIXED

Picking up the Morph Ball hangs the game. Full chain, each link traced:

1. Bank-`$84` PLM code executes with **M=1 where the ROM requires M=0**.
   Decoding `$84:88FF` both ways and matching the interpreter's instruction
   ring step-for-step against the m=1 column proves it: the real
   `LDA #$0168` (3 bytes) runs as `LDA #$68` (2 bytes), the real
   `JSL $82E118` is never executed, and `AND #$00FF` on an 8-bit accumulator
   leaves `$FF`.
2. `JSL $85:8080` (`DisplayMessageBox_Async`) is entered with `A=$00FF`, and
   `STA $1C1F` at `$85:8086` writes one byte over `message_box_index`, which
   the correct path had set to 9.
3. `InitializeMessageBox` (`$85:8241`) reads 255 and computes
   `(255-1)*6 = $05F4`.
4. `JSR ($8264,X)` at `$85:8250` fetches a pointer `$05F4` past its table ->
   `$85:703B`, outside ROM.
5. Execution sleds ~170 `$00` bytes, crosses into real ROM, `RTL`s on a junk
   stack to `$50:8513`, hits a `COP`, and lands in `InvalidInterrupt_Crash`
   (`$80:8573` = `JML $808573`, the target of the COP/BRK/ABORT vectors).
6. The interpreter burns its 2,000,000-step cap there, returns 0, and
   `src/sm_rtl.c:338` sets `g_game_done`. The game stops.

Only the origin of step 1 is unknown: where M becomes 1. The framework's M/X
detectors (`mx_claim_check_arm`, `mx_async_check_arm`) were armed with
`SNESRECOMP_TRACE=ON` and **did not fire** on this path, which points away from
an AOT block claiming a wrong M/X — consistent with this port running ~100%
interpreted — and toward the flag being architecturally real by then: something
earlier set M=1 without restoring it, or a `PLP` restored a P its `PHP` never
pushed. Next step is to trace the last `SEP`/`REP`/`PLP`/`RTI` before
`$84:8905`.

`recomp/bank05.cfg` was fixed along the way — `RestorePpuForMessageBox` ends
with RTS at `$85:869A` (0x81 bytes) and was declared `end:10000` (0x79E6),
swallowing `kMessageBoxDefs` at `$85:869B` and ~31 KB of message-box tables.
Real bug, same class as the Ceres one, but **not** the cause of this freeze.

### Instruments added, and the gaps that cost the most time

New, all in `snesrecomp/` and all debug-only:

- **WRAM write-site PC.** A watch event recorded only `cpu->PB << 16`, so every
  capture read `PC ~$xx:????`. `g_cpu_trace_write_pc24` is published per step
  by the interpreter while a watch is armed and cleared on AOT block entry.
  This is what turned "somewhere in `InitializePpuForMessageBoxes`" into
  `PC=$858086`, and without it the Morph Ball chain could not have been
  followed past step 2.
- **`SNESRECOMP_INTERP_CATCH_PC` / `_NTH` / `_OFFROM`.** The bail-time ring
  dump is useless for a PC the guest then spins on — all 256 slots hold the
  spin. These dump the ring at the *arrival*. `_OFFROM` fires on the first
  execution in `$2000-$7FFF` outside banks `$7E`/`$7F` and caught the fault at
  step 37 instead of step 9215; `_NTH` catches the nth visit, needed when a PC
  is reached legitimately before it is reached wrongly.
- **Per-line `bgmode`** in `ppu_lines`, plus `drawn_mode`/`renderer` recorded
  from both renderer branches.

Gaps worth fixing on their own account:

- **`BRK` does not trap.** `interp816.c:934` treats `$00` as an inert one-byte
  marker when `brkHookEnabled`. On hardware the first `$00` of the sled would
  have vectored straight to `$80:8573`. Instead execution wandered ~170 bytes,
  crossed into real code and corrupted the stack before anything trapped,
  destroying the evidence of the original bad jump every single time.
- **`get_reg_trace` truncates oldest-first** at its 512 KB buffer with no
  marker, while reporting the true `entries` count. Two consecutive dumps came
  back byte-identical and both missed the window being chased. `nostack` fits
  far more rows; resetting the ring at the event of interest is better.
- **`get_cpu_state` returns all zeros** for this port — `g_snes_cpu` is not the
  live CPU here.
- The debug server is **single-client, newest-wins** (`debug_server.c:8596`).
  Any second tool silently steals the socket; a capture loop must expect it.

## Open items

1. **Next attract blocker** — the f2689 freeze is fixed and the demo now plays
   well past it; free-run further to find the next blocker (if any) deeper into
   attract / save-menu / new-game. (was task #1 — WriteEnemyOams — DONE)
2. **Suppressed-dispatch sweep** — DONE for the WRAM class (111→9). The 9
   ROM-operand survivors are off the demo path; revisit if a later room hits
   one. Dispatch ring + tier-2 manifest are the worklists. (task #2)
3. **Two divergent multi-tier base branches** (engine
   `feat/multi-tier-interp-fallback` vs `integ/sm-interp`) — reconciliation
   open, owner-gated.
4. **Morph Ball freeze — where does M become 1?** Everything downstream is
   traced (2026-09-19). Bank-`$84` PLM code runs 8-bit where the ROM needs
   16-bit, which feeds `$FF` to `message_box_index` and ends in
   `InvalidInterrupt_Crash`. The M/X claim/async detectors do not fire, so the
   flag is architecturally real by then. Next: trace the last
   `SEP`/`REP`/`PLP`/`RTI` before `$84:8905`. **This is the live blocker — the
   game hangs on the Morph Ball pickup.**
5. **cfg boundary class — 105 declarations >= 0x400 bytes.** Two fixed as
   instances (`bank08.cfg`, `bank05.cfg`). The generator is
   `tools/ingest_sm_decomp.py:270`, which ends every function at the next
   decomp symbol; fixing it (stop at the terminating RTS/RTL, or emit
   `data_region` for the gaps) retires the class instead of the instances.
6. **`BRK` does not trap** (`interp816.c:934`). An off-rails run sleds through
   blank memory and corrupts the stack before anything traps, destroying the
   evidence of the original bad jump. Fixing this makes every future off-rails
   bug stop at its culprit.
7. **Instrument gaps** — `get_reg_trace` truncates oldest-first with no marker;
   `get_cpu_state` returns zeros for this port. Both cost real time on
   2026-09-19.
8. **AOT carries zero cycles** — `aot_cycle_pct: 0.0`, every guest cycle
   interpreted, ~2.6 ms per simulated frame. This is what makes run-ahead
   expensive at any N; the renderer fix helped the fixed per-frame cost, not
   the multiplier.

## Owner-gated (do NOT do without explicit decision)
Merging `investigate/sm-0012-blocker` or the multi-tier branches to main;
releasing any game; reconciling the multi-tier branches; editing `src/gen/`.
