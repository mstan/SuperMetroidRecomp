#!/usr/bin/env python3
"""Collect a real reserve tank and check independent pickup pacing options.

Uses a native room/orb, normal shots and position assistance. Does not inject
PLM instructions or write inventory, item bits, music, freeze or timer results.
Requires TRACE=ON. Every profile is isolated and all launches are muted/headless.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import struct
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

from sm_progression_qa import Rom, Session

OPTIONS = {
    'control': None,
    'skip': 'skip-message=true,no-freeze=false,short-jingle=false',
    'short': 'skip-message=false,no-freeze=false,short-jingle=true',
    'instant': 'skip-message=false,no-freeze=true,short-jingle=false',
    'combined': 'skip-message=true,no-freeze=true,short-jingle=true',
}


def game_clock(data, offset=0):
    frame, second, minute, hour = struct.unpack_from('<4H', data, offset)
    return frame + 60 * (second + 60 * (minute + 60 * hour))


def validate(out, compare=None):
    observations = {}
    for mode in OPTIONS:
        path = out / mode
        result = json.loads((path / 'result.json').read_text())
        assert result['first_grant']['tick'] == 16, mode
        assert result['first_grant']['max_reserve'] == 100
        assert result['freeze_clock_delta'] == (60 if mode in ('instant', 'combined') else 0), mode
        before = bytes.fromhex(result['before']['sound'])
        sound = bytes.fromhex(result['settled']['sound'])
        short = mode in ('short', 'combined')
        assert before[0x4c] == 3, (mode, before[0x4c])
        assert sound[0x4c] == (3 if short else 2), (mode, sound[0x4c])
        added = [sound[0x66 + (index & 15)] for index in range(before[0x47],
                 before[0x47] + ((sound[0x47] - before[0x47]) & 15))]
        assert (2 in added) == short, (mode, added)
        start = json.loads((path / 'before.json').read_text())
        final = json.loads((path / 'final.json').read_text())
        elapsed_clock = game_clock((path / 'final.wram').read_bytes(), 0x9da) - result['before']['clock']
        frozen = final['frame'] - start['frame'] - elapsed_clock
        assert frozen == {'control': 425, 'skip': 79, 'short': 425, 'instant': 0, 'combined': 0}[mode], (mode, frozen)
        observations[mode] = dict(first_grant_tick=16, frozen_frames=frozen,
                                  music_track=sound[0x4c], new_sfx2_ids=added)
        if compare:
            assert result == json.loads((compare / mode / 'result.json').read_text()), mode
            for file in path.glob('*.wram'):
                assert file.read_bytes() == (compare / mode / file.name).read_bytes(), (mode, file.name)
            current = json.loads((path / 'execution.json').read_text())['interp_stats']['interp_insns']
            previous = json.loads((compare / mode / 'execution.json').read_text())['interp_stats']['interp_insns']
            assert current > previous, (mode, current, previous)
    return observations


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for key in ('exe', 'rom', 'out'):
        parser.add_argument('--' + key, type=Path, required=True)
    parser.add_argument('--interp', action='store_true')
    parser.add_argument('--compare', type=Path)
    args = parser.parse_args()
    rom = Rom(args.rom)
    base = 0x838d5a
    door = dict(pointer=base & 65535, destination=rom.word(base), direction=rom.byte(base + 3),
                cap_x=rom.byte(base + 4), cap_y=rom.byte(base + 5))
    assert door['destination'] == 0x9c07
    args.out = args.out.resolve()
    args.out.mkdir(parents=True, exist_ok=False)

    def run(mode):
        values = OPTIONS[mode]
        features = [] if values is None else ['super-metroid.pacing/item-pickup:' + values]
        game = Session(args.exe, args.rom, args.out / mode, features=features, capture_profile=False,
                       interpret_targets=(0xa08eb6, 0x848bdd, 0x85846d) if args.interp else ())

        def sample():
            return dict(clock=game_clock(game.read(0x9da, 8)), max_reserve=game.word(0x9d4),
                        sound=game.read(0x600, 0x80).hex(), box=game.word(0x1c1f))
        try:
            game.boot()
            game.land()
            game.transition(door)
            for address, value in ((0xaf6, 136), (0xb10, 136), (0xafa, 136), (0xb14, 136)):
                game.word(address, value)
            game.step(3, 'right')
            game.word(0xafa, 136)
            game.word(0xb14, 136)
            game.step(8, 'x')
            game.step(20)
            for address, value in ((0xaf6, 184), (0xb10, 184), (0xafa, 80), (0xb14, 80)):
                game.word(address, value)
            game.snapshot('before')
            before = sample()
            before_sram = game.command('read_sram 0 8192')['hex']
            events = [dict(tick=-1, **before)]
            first = None
            for tick in range(120):
                game.step(1)
                state = sample()
                events.append(dict(tick=tick, **state))
                if state['max_reserve'] == 100:
                    first = dict(tick=tick, **state)
                    game.snapshot('granted')
                    break
            assert first is not None, game.state()
            game.step(60)
            settled = sample()
            game.snapshot('settled')
            game.step(8, 'right')
            game.step(2)
            input_state = sample()
            game.snapshot('input')
            game.dismiss_dialog()
            game.step(2)
            game.snapshot('final')
            assert game.word(0x9d4) == 100 and game.word(0x998) == 8
            assert before_sram == game.command('read_sram 0 8192')['hex']
            result = dict(before=before, first_grant=first, settled=settled, after_input=input_state,
                events=events, freeze_clock_delta=settled['clock'] - first['clock'],
                input_clock_delta=input_state['clock'] - settled['clock'],
                prerequisites='Native reserve room/orb shot, position assistance only; no item flags, collector instructions or freeze outcomes written')
            (args.out / mode / 'result.json').write_text(json.dumps(result, indent=2))
        finally:
            game.close()

    with ThreadPoolExecutor(max_workers=2) as pool:
        list(pool.map(run, OPTIONS))
    result = dict(observations=validate(args.out, args.compare),
                  binary_sha256=hashlib.sha256(args.exe.read_bytes()).hexdigest())
    (args.out / 'results.json').write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps(result['observations'], indent=2))


if __name__ == '__main__':
    main()
