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
`runner/src/cpu_state.c`:

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
- **`runner/src/cpu_state.c`** `cpu_dispatch_call_pc` — pushes the 2-byte JSR
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
- **`runner/src/cpu_state.c`** — new `cpu_dispatch_pc_paired(cpu, pc24, frame_size)`:
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

## Owner-gated (do NOT do without explicit decision)
Merging `investigate/sm-0012-blocker` or the multi-tier branches to main;
releasing any game; reconciling the multi-tier branches; editing `src/gen/`.
