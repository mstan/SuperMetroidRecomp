# SM attract-demo crash

Branch: `investigate/sm-0012-blocker` (engine `integ/sm-interp`-based + this repo).
Surfaced by the interpreter-tier gap manifest (`$0FE8B7` → `$0012=$FFFF`).

## ⚠️ CORRECTED ROOT CAUSE (2026-06-18) — suppressed indirect CALL, NOT a DB bug

The "data-bank divergence" below was a **downstream symptom**, not the root.
Proven via a recomp-vs-snes9x **game_state timeline diff** (`_gsdiff.py`):

- **Oracle (real HW):** game_state `0→1→40`(157f)`→41→42` — the demo plays.
- **Recomp:** game_state is **stuck at 40** from f≈2619 to the crash; it never
  reaches 41/42. `InitAndLoadGameData` re-runs every ~68 frames (demo_scene
  marches 0→1→2→3→4→5, re-reading room data) and the garbage door_def_ptr at
  scene≥1 (the "DB=00" reads) is the consequence of those unwanted re-runs.

Root: the recompiler **suppressed** the indirect call `JSR ($0012,X)` at
`$82:817B` (= `CallDemoRoomDataFunc(demo_code_ptr)`, decomp `sm_82.c:63`)
because no `indirect_call_table`/`indirect_dispatch` authorised it
(cfg-required-dispatch-or-kill). The suppressed-call codegen emitted a bare
`return RECOMP_RETURN_NORMAL`, which **dropped the very next instruction
`INC $0998` (++game_state, `$82:817E`)** plus the rest of the function. So
game_state never advanced and the demo state machine spun forever.

**FIX (generator):** authorise the dispatch in `recomp/bank02.cfg`:
```
indirect_dispatch 817b 5 ptrcall targets:891A,8924,8925,892B,8932
```
Targets = the distinct `demo_code_ptr` set across real demo_sets 0–3 (from the
`$82:876C` kDemoRoomData table) == the decomp `CallDemoRoomDataFunc` switch
cases. The authorised dispatch emits a value-switch (default = recorded
`dispatch_oob` no-op that unpops the call frame) that **falls through** to
`++game_state` — so the state machine advances even if a target is unmatched.

Broader class: 115 other `Call indirect SUPPRESSED: JSR (ptr,X)` sites remain
across banks (mostly `JSR ($0FAx,X)` enemy/PLM instruction-list dispatchers);
each silently `return`s on hit. They are future work, surfaced as gameplay
progresses (the gap manifest / abandon table is the worklist).

---
_Original (symptom-level) DB writeup retained below for reference._

# (symptom) data-bank (DB) divergence

## Proven cascade (symptom → root)

```
$0012 = $FFFF            crash: JMP ($0012) in RunDoorSetupCode -> $0F:FFFF garbage
  ← door_setup_code=$FFFF   DoorDef field
  ← get_DoorDef(door_def_ptr) on a garbage door_def_ptr
  ← door_def_ptr garbage    written by LoadDemoRoomData ($82:8679)
  ← LoadDemoRoomData reads get_DemoRoomData(18*demo_scene + kDemoRoomData[demo_set])
    via `LDA $876C,X` / `LDA $0000,X` / `LDA $0002,X`  (absolute,X, DB-relative; ROM opcode $BD)
  ← cpu->DB = $00 instead of $82   ← ROOT
```

## Evidence

- ROM `DemoRoomData[demo_set=0]` door_ptrs vs recomp's observed `door_def_ptr`:
  scene 0 `$896A` ✓ (matches); scenes 1–4 garbage (`$09D2,$2014,$F000,$D00A` vs
  correct `$8EAA,$8DC6,$970E,$9792`). Scene 0 is the only case where `18*demo_scene=0`.
- Brute-force which bank reproduces each garbage door_ptr: scene 0 → `$82`;
  scenes 1–4 → `$00`/`$80`. So `cpu->DB`=$82 for scene 0, $00 for scene ≥1.
- ROM opcode at the `LDA $876C,X` site is `$BD` (absolute,X, DB-relative) — NOT
  long-addressed. The recompiled `LoadDemoRoomData` is FAITHFUL; it relies on
  `DB=$82` on entry and doesn't set it itself.
- Env-gated DB-trace (`SNESRECOMP_DBTRACE="lo-hi"`, added to
  `cpu_trace_func_entry`): `I_NMI` DB per frame =
  `$82`(f2615-19) → `$80`(2620-59) → `$84`(2660-79) → **`$00`(2680-87)** → `$80`(2688+).
  At the scene-1 demo-load frames (2680-87) DB=$00; the whole
  `InitAndLoadGameData_Async → InitIoForGameplay → … → LoadDemoRoomData` chain
  inherits $00. Nothing re-establishes $82 (the title→demo path left $82 for scene 0).

NOT a tier bug (interp ran faithfully), NOT a hardware-wait, NOT the demo
desyncing (demo_scene advances 0→5 correctly). It is a recomp DB-tracking
divergence — the project's known-hard class.

## How it was observed (ring-discipline, no racing)

The live debug-server `wram_writes_at`/socket queries were unusable — SM crashes
in ~30s and the process dies before a connection lands (the "time/attach"
anti-pattern). What worked: always-on, env-gated, file-backed traces that
survive the crash —
- `SNESRECOMP_WRAM_TRACE_FILE` (per-frame changed low-WRAM `$0000-$1FFF`) →
  `door_def_ptr`/`demo_scene` value histories.
- `SNESRECOMP_DBTRACE` windowed DB-trace at the function-entry hook → DB per
  function entry / per frame.

## Next step (the fix)

### Update (deeper trace, same session)

- **The DB instrumentation is partly blind.** `cpu_trace_db_change` / the dbpb
  ring only catch ~10 periodic NMI/vblank DB writes per frame; the main-thread
  PLBs that set `$84`/`$00` are emitted INLINE and bypass it. So the DB-tripwire
  tooling can't see this corruption. A **block-boundary DB shadow** (compare
  `cpu->DB` at each block to the previous) catches every change — added to
  `cpu_trace_block` (`SNESRECOMP_DBTRACE` window, `[dbs]` lines).
- **`DB→$00` is normal and constant** during gameplay (`$00` is the bank for
  hardware/low-RAM access). There is NO single "wrong write" — the demo-load
  just *runs at a `$00` moment*. So the fix is NOT "stop a bad write"; it's
  "ensure `DB=$82` is (re)established for the demo-load," which the recomp does
  for scene 0 (inherited from the title) but not scene ≥1.
- **Dispatch structure:** `RunOneFrameOfGameInner` (`sm_82.c:657`, a *manual
  coroutine*) calls `kGameStateFuncs[game_state]()`; index 40
  (`kGameState_40_TransitionToDemo`) = `InitAndLoadGameData_Async`. The whole
  handler chain inherits the dispatcher's DB. Dispatcher DB = `$82` at scene 0,
  `$00` at scene 1.

### Prime suspect + next step

The dispatcher is a coroutine that yields (`WaitForNMI`) and resumes across
frames. Leading hypothesis: **the coroutine/fiber resume (or the game-state
dispatch) does not re-establish `DB=$82`** the way real hardware does — the
project's known fiber-serialization weak spot ([[super-metroid-port-bringup]]:
"fibers not serialized"). Next, oracle-free:
1. Read the recompiled `RunOneFrameOfGameInner` + the `kGameStateFuncs[]`
   indirect-dispatch emission and the ASM around it for a `PHK:PLB` (set
   `DB=PB=$82`) that the recomp drops or mis-orders across the coroutine yield.
2. If needed, get real-hardware DB at `$82:8679` by extracting the DBR byte from
   a snes9x libretro save-state blob (find its offset once) — the only way to
   turn `snesref` into a DB oracle, since libretro doesn't expose CPU registers.
Then fix the GENERATOR (recompiler/v2/* or runtime), never `src/gen`.

## Investigation artifacts (scratch, this branch)

`_read_demo.py` (ROM DemoRoomData reader), `_diag_db.py` (opcode + brute-force DB),
`_tcp_query.py` (debug-server client). DB-trace hook in
`snesrecomp/runner/src/cpu_trace.c` (env-gated, zero-cost off).
