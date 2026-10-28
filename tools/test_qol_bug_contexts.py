#!/usr/bin/env python3
"""Exercise native wall-jump, bomb-block and eye-projectile bug contexts.

Requires a TRACE=ON build. Fresh profiles, muted audio and dummy video are
provided by Session. RAM edits create position/equipment/block prerequisites;
native inputs choose the wall jump. Eye cleanup explicitly bypasses defeating
the door after its projectile has spawned: it is not a door-fight test.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import struct
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

from sm_progression_qa import Rom, Session


BUGS = 'super-metroid.balance-fixes/bug-fixes'


def status(game):
    return dict(pose=game.word(0xa1c), movement=game.read(0xa1f, 1)[0],
                contact=game.word(0xa6e), x=game.word(0xaf6),
                y=game.word(0xafa), animation=game.word(0xa96))


def wall(game, kind):
    game.word(0x9a2, 8)
    game.word(0x9a4, 8)
    for address, value in ((0xaf6, 90), (0xb10, 90), (0xafa, 1160),
                           (0xb14, 1160), (0x911, 0), (0x915, 1048)):
        game.word(address, value)
    for by in range(64, 80):
        index = by * game.word(0x7a5) + 2
        game.word(0x10002 + 2 * index, 0x8052)
        game.write(0x16402 + index, b'\0')
    game.step(2)
    game.step(8, 'left')
    game.step(8, 'left+a')
    game.step(4, 'left')
    spin = status(game)
    for _ in range(4):
        game.word(0xaf6, 53)
        game.word(0xb10, 53)
        game.step(1, 'right')
    for pulse in range(12):
        # Stay beside the wall during the native direction-change animation.
        # Do not write pose, movement, collision or contact-damage outcomes.
        game.word(0xaf6, 53)
        game.word(0xb10, 53)
        game.step(1, 'right+a' if pulse % 2 else 'right')
        if game.read(0xa1f, 1)[0] == 20:
            break
    assert game.read(0xa1f, 1)[0] == 20, status(game)
    game.step(2, 'right+a')
    reached = status(game)
    game.snapshot('walljump')
    result = dict(spin=spin, walljump=reached,
                  assistance='Equipment, solid wall, camera and position only; native inputs select wall jump')
    if kind == 'block':
        bx = (reached['x'] + game.word(0xafe) + 15) // 16
        by = (reached['y'] - 4) // 16
        index = by * game.word(0x7a5) + bx
        game.word(0x10002 + 2 * index, 0xf052)
        game.write(0x16402 + index, b'\0')
        frames = []
        for tick in range(8):
            game.step(1, 'right+a')
            frames.append(dict(tick=tick, tile=game.word(0x10002 + 2 * index), **status(game)))
        result.update(fixture_block=dict(x=bx, y=by, index=index), frames=frames)
        game.snapshot('block_contact')
    return result


def eye(game, rooms, mode):
    door = next(d for room in rooms.values() for d in room['doors']
                if d['destination'] == 0xa56b)
    game.transition(door)

    def pin():
        for address, value in ((0xaf6, 400), (0xb10, 400), (0xafa, 384), (0xb14, 384)):
            game.word(address, value)
        game.write(0xb2c, b'\0' * 4)

    def objects():
        data = game.read(0x1997, 0x240)
        def word(address):
            return struct.unpack_from('<H', data, address - 0x1997)[0]
        return dict(eye=[dict(slot=k, pre=word(0x1a03 + k), ilist=word(0x1b47 + k))
                         for k in range(0, 36, 2) if word(0x1997 + k) == 0xb743],
                    slot4_id=word(0x199f), slot4_ilist=word(0x1b4f))

    pin()
    game.word(0x911, 256)
    game.word(0x18a8, 0xffff)
    game.step(8, 'right')
    game.step(2)
    pin()
    game.word(0x9d2, 0)
    for frame in range(600):
        pin()
        game.step(1, 'x' if frame % 8 < 4 else '0')
        before = objects()
        if any(p['pre'] == 0xb6b9 for p in before['eye']):
            break
    assert before['eye'] and before['slot4_id'] == 0, before
    assert not game.read(0xd8b8, 1)[0] & 0x20
    game.snapshot('live_projectile')
    game.write(0xd8b8, bytes([game.read(0xd8b8, 1)[0] | 0x20]))
    pin()
    game.step(1)
    after = objects()
    game.snapshot('cleanup')
    game.step(8)
    assert game.word(0x998) == 8
    assert after['slot4_ilist'] == (0xb5f3 if mode == 'control' else before['slot4_ilist'])
    slot = before['eye'][0]['slot']
    own = next(p for p in after['eye'] if p['slot'] == slot)
    assert own['ilist'] == (before['eye'][0]['ilist'] if mode == 'control' else 0xb5f9), after
    return dict(before=before, after=after,
                bypassed_prerequisite='Door-open bit45 set after genuine projectile spawn; no fight-defeat claim')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for key in ('exe', 'rom', 'out'):
        parser.add_argument('--' + key, type=Path, required=True)
    parser.add_argument('--interp', action='store_true')
    parser.add_argument('--compare', type=Path)
    args = parser.parse_args()
    args.out = args.out.resolve()
    args.out.mkdir(parents=True, exist_ok=False)
    rooms = Rom(args.rom).rooms()
    targets = (0x90a734, 0x84ce83, 0x86b6b9) if args.interp else ()

    def run(case):
        kind, mode = case
        features = [] if kind == 'wall' and mode == 'control' else [BUGS]
        if kind == 'block' and mode == 'control':
            features = [BUGS + ':bomb-block-screw=false']
        if kind == 'eye' and mode == 'control':
            features = []
        name = kind + '-' + mode
        game = Session(args.exe, args.rom, args.out / name, features=features,
                       capture_profile=False, interpret_targets=targets)
        try:
            game.boot()
            game.land()
            result = eye(game, rooms, mode) if kind == 'eye' else wall(game, kind)
            if kind == 'wall':
                assert result['walljump']['contact'] == (0 if mode == 'control' else 3)
            if kind == 'block':
                assert (result['frames'][-1]['tile'] == 0xf052) == (mode == 'control')
                assert result['frames'][-1]['x'] == (59 if mode == 'control' else 67)
            return name, result
        finally:
            game.close()

    cases = [(kind, mode) for kind in ('wall', 'block', 'eye') for mode in ('control', 'mod')]
    with ThreadPoolExecutor(max_workers=2) as pool:
        results = dict(pool.map(run, cases))
    results['binary_sha256'] = hashlib.sha256(args.exe.read_bytes()).hexdigest()
    if args.compare:
        for kind, mode in cases:
            name = kind + '-' + mode
            for path in (args.out / name).glob('*.wram'):
                assert path.read_bytes() == (args.compare / name / path.name).read_bytes(), (name, path.name)
            current = json.loads((args.out / name / 'execution.json').read_text())
            previous = json.loads((args.compare / name / 'execution.json').read_text())
            assert current['interp_stats']['interp_insns'] > previous['interp_stats']['interp_insns'], name
        results['comparison'] = 'Full WRAM snapshots identical; interpreter instructions increase in every case'
    (args.out / 'results.json').write_text(json.dumps(results, indent=2) + '\n')
    print(json.dumps(results, indent=2))


if __name__ == '__main__':
    main()
