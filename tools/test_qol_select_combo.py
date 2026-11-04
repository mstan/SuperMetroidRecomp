#!/usr/bin/env python3
"""Check Select combos with real held charge and auto charge disabled.

TCP input preserves shooting across the charge/Select boundary. The scripted
press command releases between commands, so it cannot establish this gesture.
Equipment/ammo are prerequisites; native input and projectiles produce results.
All profiles are isolated, muted and headless; requires TRACE=ON.
"""
from __future__ import annotations

import argparse
import json
import struct
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

from sm_progression_qa import Session

CASES = tuple(kind+'-'+mode for kind in ('charged', 'uncharged', 'no-ammo') for mode in ('stock', 'mod'))


def validate(out, compare=None):
    observations = {}
    for name in CASES:
        path = out/name
        result = json.loads((path/'result.json').read_text())
        assert result['idle']['charge'] == 0, name
        assert (result['charged']['charge'] >= 60) == (not name.startswith('uncharged')), name
        assert result['charged']['held'] & 0x40, (name, 'shooting was released before Select')
        special = result['special']
        if name == 'charged-mod':
            assert special['pb'] == 4 and special['projectiles'] == 4 and special['hud'] == 0, (name, special)
        else:
            assert special['pb'] == (0 if name.startswith('no-ammo') else 5), (name, special)
            assert special['projectiles'] != 4 and special['hud'] == 1, (name, special)
        if compare:
            assert result == json.loads((compare/name/'result.json').read_text()), name
            for file in path.glob('*.wram'):
                assert file.read_bytes() == (compare/name/file.name).read_bytes(), (name, file.name)
            current = json.loads((path/'execution.json').read_text())['interp_stats']['interp_insns']
            previous = json.loads((compare/name/'execution.json').read_text())['interp_stats']['interp_insns']
            assert current > previous, (name, current, previous)
        observations[name] = result
    for kind in ('uncharged', 'no-ammo'):
        for file in (out/(kind+'-stock')).glob('*.wram'):
            assert file.read_bytes() == (out/(kind+'-mod')/file.name).read_bytes(), (kind, file.name)
    return observations


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for key in ('exe', 'rom', 'out'):
        parser.add_argument('--'+key, type=Path, required=True)
    parser.add_argument('--interp', action='store_true')
    parser.add_argument('--compare', type=Path)
    args = parser.parse_args()
    args.out = args.out.resolve()
    args.out.mkdir(parents=True, exist_ok=False)

    def run(name):
        features = ['super-metroid.combat/beam-combo-select'] if name.endswith('-mod') else []
        game = Session(args.exe, args.rom, args.out/name, features=features, capture_profile=False,
            interpret_targets=(0x90c4e0, 0x90b843, 0x90ccc0, 0xa08eb6) if args.interp else ())

        def sample():
            data = game.read(0x80, 0xc60)
            word = lambda address: struct.unpack_from('<H', data, address-0x80)[0]
            return dict(charge=word(0xcd0), pb=word(0x9ce), hud=word(0x9d2),
                projectiles=word(0xcce), held=word(0x8b), types=[word(a) for a in range(0xc18, 0xc22, 2)])
        try:
            game.boot()
            game.land()
            game.step(70, 'right')
            game.step(30)
            for address, value in ((0x9a6, 0x1001), (0x9a8, 0x1001), (0x9c6, 5), (0x9c8, 5),
                (0x9ce, 0 if name.startswith('no-ammo') else 5), (0x9d0, 5), (0x9d2, 0)):
                game.word(address, value)
            game.step(30)
            saved = game.command('read_sram 0 8192')['hex']
            result = dict(idle=sample())
            game.snapshot('idle')
            game.step(20 if name.startswith('uncharged') else 150, 'x')
            result['charged'] = sample()
            game.snapshot('charged')
            game.step(1, 'x+select')
            game.step(8, 'x')
            result['special'] = sample()
            game.snapshot('special')
            game.step(80)
            result['settled'] = sample()
            game.snapshot('settled')
            assert saved == game.command('read_sram 0 8192')['hex'], name
            assert game.word(0x998) == 8, name
            (args.out/name/'result.json').write_text(json.dumps(result, indent=2))
        finally:
            game.close()
    with ThreadPoolExecutor(max_workers=2) as pool:
        list(pool.map(run, CASES))
    result = dict(observations=validate(args.out, args.compare))
    (args.out/'results.json').write_text(json.dumps(result, indent=2)+'\n')
    print('PASS: held-charge Select combo, no auto charge, native ammo/charge guards')


if __name__ == '__main__':
    main()
