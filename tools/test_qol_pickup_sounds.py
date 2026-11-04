#!/usr/bin/env python3
"""Qualify all nine native pickup collectors and independent sound options.

Diagnostic fixture: point one PLM slot at each stock ROM pickup instruction
list in the Landing Site. Acquisition prerequisites are bypassed explicitly;
the native collector, message and sound queues must produce the results.
The ROM and inventory/result flags are never patched. Isolated muted profiles.
Requires TRACE=ON; alternate execution also denies the compiled enemy scheduler.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

from sm_progression_qa import Rom, Session

# name, ROM instruction list, native PLM header, result word, increment/bit
COLLECTORS = (
    ('beam', 0xe17d, 0xeeeb, 0x9a8, 0x1000),
    ('equipment', 0xe14f, 0xeee7, 0x9a4, 0x1000),
    ('varia', 0xe2c3, 0xef07, 0x9a4, 0x0001),
    ('gravity', 0xe2f8, 0xef0b, 0x9a4, 0x0020),
    ('grapple', 0xe388, 0xef17, 0x9a4, 0x4000),
    ('xray', 0xe32d, 0xef0f, 0x9a4, 0x8000),
    ('energy', 0xe0b3, 0xeed7, 0x9c4, 100),
    ('reserve', 0xe43f, 0xef27, 0x9d4, 100),
    ('missile', 0xe0d8, 0xeedb, 0x9c8, 5),
    ('super', 0xe0fd, 0xeedf, 0x9cc, 5),
    ('powerbomb', 0xe122, 0xeee3, 0x9d0, 5),
)
CASES = COLLECTORS + tuple((name+'-repeat', *rest) for name, *rest in COLLECTORS[-3:])
MODES = {
    'control': None,
    'short': 'skip-message=false,no-freeze=false,short-jingle=true,mute-jingle=false',
    'mute': 'skip-message=false,no-freeze=false,short-jingle=true,mute-jingle=true',
    'mute-instant': 'skip-message=false,no-freeze=true,short-jingle=true,mute-jingle=true',
}


def validate(out, compare=None):
    results = {}
    for mode in MODES:
        path = out / mode
        result = json.loads((path/'result.json').read_text())
        for name, *_ in CASES:
            case = result[name]
            short = mode == 'short' and (name in ('energy', 'reserve') or name.endswith('-repeat'))
            mute = mode.startswith('mute')
            assert case['before']['track'] == 5, (mode, name, case['before'])
            assert case['settled']['track'] == (5 if mute or short else 2), (mode, name, case['settled'])
            assert case['sram_unchanged'], (mode, name)
            if mute:
                assert case['settled']['music_read'] == case['settled']['music_write'], (mode, name)
            if name in ('energy', 'reserve') or name.endswith('-repeat'):
                pickup_sfx = {'energy': 2, 'reserve': 2, 'missile-repeat': 3,
                              'super-repeat': 4, 'powerbomb-repeat': 5}[name]
                assert (pickup_sfx in case['sfx2_added']) == short, (mode, name, case['sfx2_added'])
            assert case['settled']['clock'] - case['granted']['clock'] == (60 if mode == 'mute-instant' else 0), (mode, name)
            assert case['result'] == case['expected'], (mode, name, case)
        if compare:
            assert result == json.loads((compare/mode/'result.json').read_text()), mode
            for file in path.glob('*.wram'):
                assert file.read_bytes() == (compare/mode/file.name).read_bytes(), (mode, file.name)
            current = json.loads((path/'execution.json').read_text())['interp_stats']['interp_insns']
            previous = json.loads((compare/mode/'execution.json').read_text())['interp_stats']['interp_insns']
            assert current > previous, (mode, current, previous)
        results[mode] = {name: dict(track=case['settled']['track'], sfx2_added=case['sfx2_added'])
                         for name, case in result.items()}
    return results


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for key in ('exe', 'rom', 'out'):
        parser.add_argument('--'+key, type=Path, required=True)
    parser.add_argument('--interp', action='store_true')
    parser.add_argument('--compare', type=Path)
    args = parser.parse_args()
    rom = Rom(args.rom)
    args.out = args.out.resolve()
    args.out.mkdir(parents=True, exist_ok=False)
    for _, pointer, *_ in COLLECTORS:
        assert rom.word(0x840000 | pointer) == 0x8bdd and rom.byte(0x840000 | pointer+2) == 2

    def run(mode):
        options = MODES[mode]
        features = [] if options is None else ['super-metroid.pacing/item-pickup:'+options]
        game = Session(args.exe, args.rom, args.out/mode, features=features, capture_profile=False,
            interpret_targets=(0xa08eb6, 0x848bdd, 0x8488b0, 0x8488f3, 0x84891a, 0x848941,
                               0x848968, 0x848986, 0x8489a9, 0x8489d2, 0x8489fb, 0x85846d) if args.interp else ())
        results = {}

        def sample():
            sound = game.read(0x600, 0x80)
            clock = game.read(0x9da, 8)
            words = [int.from_bytes(clock[i:i+2], 'little') for i in range(0, 8, 2)]
            return dict(track=sound[0x4c], music_read=int.from_bytes(sound[0x3b:0x3d], 'little'),
                music_write=int.from_bytes(sound[0x39:0x3b], 'little'), sound=sound.hex(),
                clock=words[0]+60*(words[1]+60*(words[2]+60*words[3])))
        try:
            game.boot()
            for name, pointer, header, address, value in CASES:
                game.land()
                game.step(120)
                before_value = game.word(address)
                expected = before_value | value if address in (0x9a4, 0x9a8) else before_value+value
                assert expected != before_value, name
                before = sample()
                sram = game.command('read_sram 0 8192')['hex']
                game.snapshot(name+'_before')
                # Run a real ROM instruction list through the native PLM scheduler.
                for target, word in ((0x1c37, header), (0x1c87, 0), (0x1cd7, 0x86d0),
                                     (0x1d27, pointer), (0xde1c, 1)):
                    game.word(target, word)
                for tick in range(30):
                    game.step(1)
                    if game.word(address) == expected:
                        break
                else:
                    raise AssertionError((mode, name, 'native collector did not grant', game.state()))
                granted = sample()
                game.step(60)
                settled = sample()
                game.snapshot(name+'_settled')
                old = bytes.fromhex(before['sound']); sound = bytes.fromhex(settled['sound'])
                count = (sound[0x47]-old[0x47]) & 15
                added = [sound[0x66+((old[0x47]+index)&15)] for index in range(count)]
                game.dismiss_dialog()
                if name in ('varia', 'gravity'):
                    game.step(720)
                    assert game.word(0xdec) == 0 and game.word(0xa42) == 0xe695, (mode, name, game.state())
                game.step(2)
                assert game.word(0x998) == 8, (mode, name, game.state())
                game.snapshot(name+'_final')
                results[name] = dict(before=before, granted=granted, settled=settled,
                    sfx2_added=added, result=game.word(address), expected=expected,
                    sram_unchanged=sram == game.command('read_sram 0 8192')['hex'])
            (args.out/mode/'result.json').write_text(json.dumps(results, indent=2))
        finally:
            game.close()
    with ThreadPoolExecutor(max_workers=2) as pool:
        list(pool.map(run, MODES))
    result = dict(observations=validate(args.out, args.compare),
        exe_sha256=hashlib.sha256(args.exe.read_bytes()).hexdigest(),
        prerequisites='Native item PLM instruction lists injected; room traversal and item contact bypassed explicitly')
    (args.out/'results.json').write_text(json.dumps(result, indent=2)+'\n')
    print('PASS: nine collectors, both suits, first/repeat ammo, legacy short sound, cut-all and instant-cut combinations')


if __name__ == '__main__':
    main()
