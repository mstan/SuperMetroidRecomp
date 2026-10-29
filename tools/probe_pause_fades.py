#!/usr/bin/env python3
"""Live probe of the options panel's fade timelines.

Samples the guest's own fade words (INIDISP, screen_fade_delay,
screen_fade_counter, menu_index, label mode, game state, SFX-1 write
pointer) frame by frame through four scenarios and checks every frame
against the exact sequence the fade laws produce:

  a) open            the host's enter fade: press frame no-step, 15 fast
                     steps to force-blank (the Up marker lands on the 0x80
                     frame), 1 no-step, 15 slow steps back, full 45 frames
                     after the press, 2 black frames
  b) leave (L/R/B)   the ROM's own tab change ridden from the panel: the
                     host's LeaveTo writes counter=1 so the press frame
                     holds its brightness, then the guest's 0/0 fast fade
                     runs from the next frame, 3 blacks, full 46 frames
                     after the press, lands on the other pause tab
  c) mid-fade Start  the host hands the in-flight enter fade to the
                     guest's own unpause on exactly the frame the guest
                     takes the timed-held Start edge (press+5); the
                     guest's 1/1 slow cadence then owns the words
  d) mid-fade        a slot savestate taken mid-fade, then loadstate:
     savestate/load  the restored fade steps one frame past the saved
                     frame and completes on the original press+45

Ground truth is the pinned decompilation: QueueSfx1_Internal,
HandleFadeOut/HandleFadeIn, ReleaseButtonsFilter ($80:8146,
$80:8924/894D), HandlePauseScreenLrInput ($82:A50C),
HandlePauseScreenStart ($82:A5B7), PauseMenu_2..7,
GameState_16-18. Run against the trace build:

    python3 tools/probe_pause_fades.py --exe build-live/SuperMetroidSNESRecomp \
        --rom "Super Metroid (Japan, USA) (En,Ja).sfc" \
        --artifacts /tmp/opencode/pause-fade-probe
"""

import argparse
import json
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from sm_progression_qa import Session


# ── guest words (bank 0 WRAM; provenance in src/variables.h) ────────────
A_INIDISP, A_DELAY, A_COUNTER = 0x0051, 0x0723, 0x0725
A_MENU, A_LABEL, A_SHOULDER, A_HL_TIMER = 0x0727, 0x0753, 0x0751, 0x0729
A_STATE = 0x0998
A_SFX_WP = 0x0646
A_SFX_QUEUE = 0x0656


def sample(session):
    wp = session.read(A_SFX_WP, 1)[0]
    return dict(
        inidisp=session.read(A_INIDISP, 1)[0],
        delay=session.word(A_DELAY),
        counter=session.word(A_COUNTER),
        menu=session.word(A_MENU),
        label=session.word(A_LABEL),
        shoulder=session.word(A_SHOULDER),
        hl_timer=session.word(A_HL_TIMER),
        state=session.word(A_STATE),
        sfx_wp=wp,
        # The queue slot the write pointer just left: the id queued this
        # frame (QueueSfx1_Internal writes the id, then advances wp).
        sfx_id=session.read(A_SFX_QUEUE + ((wp - 1) & 0xF), 1)[0],
    )


def fmt(s):
    return (f"inidisp={s['inidisp']:02x} delay={s['delay']} "
            f"counter={s['counter']} menu={s['menu']} label={s['label']} "
            f"state={s['state']} wp={s['sfx_wp']:02x}")


def run_frames(session, buttons, count):
    """Step `count` frames holding `buttons`; sample after each."""
    out = []
    for _ in range(count):
        session.step(1, buttons)
        out.append(sample(session))
    return out


def check(name, samples, expect):
    """expect: word -> per-frame expected values; every frame must match."""
    failures = []
    for word, exp in expect.items():
        for i, want in enumerate(exp):
            got = samples[i][word]
            if got != want:
                failures.append(f'{word}[{i}] wanted {want:#x} got {got:#x}')
    ok = not failures
    print(f'  [{"PASS" if ok else "FAIL"}] {name} ({len(samples)} frames)')
    for f in failures[:12]:
        print(f'    mismatch {f}')
    if len(failures) > 12:
        print(f'    ... and {len(failures) - 12} more')
    return ok


def in_pairs(start, n):
    """Slow-fade value pattern: each level held 2 frames, step first.

    Matches a delay-1 fade whose window opens on a step frame (0x80->1,
    1->2, ...): each increment lands on an odd offset, held one frame."""
    out = []
    level = start
    for _ in range(n // 2):
        out += [level, level]
        level = (level + 1) & 0xF
    return out


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--exe', required=True)
    parser.add_argument('--rom', required=True)
    parser.add_argument('--artifacts', required=True)
    args = parser.parse_args()

    root = Path(args.artifacts).resolve()
    session = Session(args.exe, args.rom, root / 'profile', capture_profile=False)
    results = {}
    try:
        session.boot()
        session.land()
        session.command('set_controller 0')
        session.step(60)

        # ── pause, and settle the filter ─────────────────────────────────
        session.step(1, 'start')
        session.step(3, '0')
        session.step(120, '0')
        base = sample(session)
        assert base['state'] == 15, f'not paused: {fmt(base)}'
        print(f'paused: {fmt(base)}')
        if (base['inidisp'], base['delay'], base['counter']) != (0x0F, 0, 0):
            print(f'  note: non-rest baseline inidisp={base["inidisp"]:02x} '
                  f'delay={base["delay"]} counter={base["counter"]} (want 0f/0/0)')
        open_btn = 'l' if base['menu'] == 0 else 'r'   # open trigger direction
        wp0 = base['sfx_wp']

        # ── (a) open: the host's enter fade ──────────────────────────────
        print(f'\n(a) open with {open_btn.upper()}')
        session.step(1, open_btn)
        s = [sample(session)] + run_frames(session, '0', 46)
        tab = base['menu']
        results['a_open'] = check('open enter-fade', s, dict(
            inidisp=[0x0F] + [0x0F - i for i in range(1, 15)] + [0x80, 0x80]
                     + in_pairs(1, 30),
            delay=[1] * 15 + [2] * 30 + [0] * 2,
            counter=[0] * 15 + [1] + [0] + [1, 0] * 14 + [0, 0],
            menu=[tab] * 47,
            label=[base['label']] * 47,
            state=[15] * 47,
        ))
        print('   ' + ' '.join(f"{x['inidisp']:02x}" for x in s))
        results['a_sfx'] = s[0]['sfx_wp'] == (wp0 + 1) % 16 and \
            s[0]['sfx_id'] == 0x38 and \
            all(x['sfx_wp'] == s[0]['sfx_wp'] for x in s)
        print(f'  [{"PASS" if results["a_sfx"] else "FAIL"}] open SFX queued '
              f'once (id {s[0]["sfx_id"]:02x}), ring pointer stable')

        # ── (b) leave: the ROM's own tab change ───────────────────────────
        # L leaves toward Map, R toward Equipment (the ROM's directions);
        # from the current tab that is the cross-tab leave.
        leave_btn = 'r' if base['menu'] == 0 else 'l'
        target = 1 if base['menu'] == 0 else 0
        source = 2 if base['menu'] == 0 else 5
        print(f'\n(b) leave with {leave_btn.upper()} toward '
              f'{"Equipment" if target == 1 else "Map"}')
        session.step(1, leave_btn)
        s = [sample(session)] + run_frames(session, '0', 48)
        results['b_leave'] = check('leave tab-change', s, dict(
            inidisp=[0x0F] + [0x0F - i for i in range(1, 15)] + [0x80] * 3
                     + in_pairs(1, 28) + [0x0F] * 3,
            delay=[0] * 16 + [1] * 30 + [0] * 3,
            counter=[0] * 16 + [1, 0] * 15 + [0] * 3,
            menu=[source] * 15 + [source + 1] + [source + 2] * 30 + [target] * 3,
            label=[2 if target == 1 else 0] * 49,
            shoulder=[2 if target == 1 else 1] * 49,
            hl_timer=[4, 3, 2, 1, 0] + [0] * 44,
            state=[15] * 49,
        ))
        print('   ' + ' '.join(f"{x['inidisp']:02x}" for x in s))
        results['b_sfx'] = s[0]['sfx_wp'] == (wp0 + 2) % 16 and \
            s[0]['sfx_id'] == 0x38 and \
            all(x['sfx_wp'] == s[0]['sfx_wp'] for x in s)
        print(f'  [{"PASS" if results["b_sfx"] else "FAIL"}] leave SFX queued '
              f'once (id {s[0]["sfx_id"]:02x}), ring pointer stable')
        session.step(10, '0')
        s = sample(session)
        assert (s['inidisp'], s['menu'], s['label']) == (0x0F, target,
                                                         2 if target == 1 else 0), \
            f'leave landing: {fmt(s)}'

        # ── (c) mid-fade Start: the handoff ──────────────────────────────
        # SyncToGuest closed the panel when the tab change reached the Load
        # state, so we sit on the other tab with the panel down. A fresh
        # open uses that tab's own trigger (R on Equipment, L on Map) and
        # starts a clean enter fade to press Start into.
        open2 = 'r' if s['menu'] == 1 else 'l'
        print(f'\n(c) fresh open with {open2.upper()} on the landed tab, '
              f'hold START mid-fade')
        session.step(1, open2)
        session.step(8, '0')
        s = sample(session)
        assert (s['inidisp'], s['delay'], s['state']) == (7, 1, 15), \
            f'mid-fade setup: {fmt(s)}'
        wp_before = s['sfx_wp']
        s = run_frames(session, 'start', 12) + run_frames(session, '0', 28)
        # s[0] is the press frame. The timed-held edge lands 5 frames after
        # the press (ReleaseButtonsFilter: stable press frame, release-reset
        # frame, 3 decay frames, underflow frame) and the guest unpauses
        # on that same frame; the host's handoff clears the marker then,
        # and the guest's own 1/1 slow cadence owns the words afterwards.
        results['c_handoff'] = check('mid-fade start handoff', s, dict(
            inidisp=[6, 5, 4, 3, 2, 1, 1, 0x80, 0x80, 0x80]
                     + in_pairs(1, 28) + [0x0F] * 2,
            state=[15] * 5 + [16, 16] + [17] + [18] * 30 + [8] * 2,
            delay=[1] * 7 + [0] + [1] * 30 + [0] * 2,
        ))
        print('   ' + ' '.join(f"{x['inidisp']:02x}" for x in s[:20]) + ' ...')
        # The host clears its marker on the edge frame, but the guest's own
        # HandlePauseScreenStart rewrites delay=1 on that same frame, so no
        # sample ever shows (delay 0, state 16). The observable invariant is
        # the first state-16 sample: the guest unpauses on press+5.
        handoff = next((i for i, x in enumerate(s)
                        if x['state'] == 16), None)
        results['c_frame_exact'] = handoff == 5
        print(f'  [{"PASS" if results["c_frame_exact"] else "FAIL"}] handoff '
              f'frame = press+{"5" if results["c_frame_exact"] else handoff}')
        # The guest's HandlePauseScreenStart queues 0x38 on the edge frame
        # (wp +1). On resume, QueueSamusMovementSfx queues SFX-1 0x41 iff
        # flare_counter == 16, so wp may advance once more; allow either and
        # require stability afterwards.
        wp1 = (wp_before + 1) % 16
        wp2 = (wp_before + 2) % 16
        results['c_sfx'] = (
            all(x['sfx_wp'] == wp_before for x in s[:5]) and
            s[5]['sfx_wp'] == wp1 and s[5]['sfx_id'] == 0x38 and
            all(x['sfx_wp'] in (wp1, wp2) for x in s[6:10]) and
            all(x['sfx_wp'] == s[10]['sfx_wp'] for x in s[10:]))
        print(f'  [{"PASS" if results["c_sfx"] else "FAIL"}] unpause SFX '
              f'queued by the guest on press+5 (wp {wp_before:02x} -> '
              f'{s[5]["sfx_wp"]:02x}), movement SFX on resume tolerated')
        session.step(30, '0')
        s = sample(session)
        assert s['state'] == 8, f'back in gameplay: {fmt(s)}'

        # ── (d) mid-fade savestate / loadstate convergence ───────────────
        print('\n(d) pause, open, savestate mid-fade, loadstate')
        session.step(1, 'start')
        session.step(3, '0')
        session.step(120, '0')
        s = sample(session)
        assert s['state'] == 15, f'not paused: {fmt(s)}'
        open_btn = 'l' if s['menu'] == 0 else 'r'
        session.step(1, open_btn)
        session.step(10, '0')
        snap = sample(session)
        assert (snap['inidisp'], snap['delay'], snap['counter']) == (5, 1, 0), \
            f'mid-fade snapshot: {fmt(snap)}'
        session.command('savestate 5')
        session.step(12, '0')
        before_load = sample(session)
        session.command('loadstate 5')
        # A pending load is consumed by the host's main loop only while it
        # is running frames: parked, the loop blocks inside
        # debug_server_wait_if_paused, past the consume block, so the load
        # would wait forever. One step unparks it; the load lands first,
        # then the restored state runs one frame, so the fade sits one step
        # past the snap. Poll until those words appear: the live fade is on
        # the Up marker (delay 2) and can never produce the Down-marker
        # (delay 1) snap level again.
        session.step(1, '0')
        first_inidisp = (snap['inidisp'] - 1) if snap['inidisp'] > 1 else 0x80
        deadline = time.monotonic() + 10
        while time.monotonic() < deadline:
            s0 = sample(session)
            if (s0['inidisp'], s0['delay'], s0['counter']) == \
                    (first_inidisp, snap['delay'], snap['counter']):
                break
            time.sleep(0.05)
        else:
            raise AssertionError(f'loadstate did not land: {fmt(s0)}')
        s = [s0] + run_frames(session, '0', 35)
        # The enter timeline from the press, offset by the saved frame 10
        # plus the one settling frame: s[0] is enter[11].
        enter_inidisp = ([0x0F] + [0x0F - i for i in range(1, 15)] +
                         [0x80, 0x80] + in_pairs(1, 30))
        enter_delay = [1] * 15 + [2] * 30 + [0] * 2
        enter_counter = [0] * 15 + [1] + [0] + [1, 0] * 14 + [0, 0]
        results['d_converge'] = check('mid-fade savestate/load', s, dict(
            inidisp=enter_inidisp[11:47],
            delay=enter_delay[11:47],
            counter=enter_counter[11:47],
            menu=[snap['menu']] * 36,
            state=[15] * 36,
        ))
        print('   ' + ' '.join(f"{x['inidisp']:02x}" for x in s))
        results['d_jump'] = before_load['inidisp'] != s[0]['inidisp'] and \
            (s[0]['inidisp'], s[0]['delay'], s[0]['counter']) == \
            (first_inidisp, snap['delay'], snap['counter'])
        print(f'  [{"PASS" if results["d_jump"] else "FAIL"}] load restored '
              f'the mid-fade words ({before_load["inidisp"]:02x} -> '
              f'{s[0]["inidisp"]:02x}, delay {s[0]["delay"]}, counter '
              f'{s[0]["counter"]})')

    finally:
        session.close()

    (root / 'probe.json').write_text(json.dumps(results, indent=2) + '\n')
    ok = all(results.values())
    print()
    for name, passed in results.items():
        print(f'  {"PASS" if passed else "FAIL"}  {name}')
    print(f'\n{"PASS: all fade timelines byte-exact" if ok else "FAIL: see above"}')
    print(root / 'probe.json')
    return 0 if ok else 1


if __name__ == '__main__':
    raise SystemExit(main())
