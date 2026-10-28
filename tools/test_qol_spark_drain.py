#!/usr/bin/env python3
"""Qualify stock, half and zero shinespark drain in all three directions.

Stored spark/health are assisted prerequisites. Ordinary input initiates the
native spark; flight drain and eventual collision must happen naturally.
Uses fresh muted headless profiles and never changes cartridge ROM or SRAM.
"""
from __future__ import annotations

import argparse
import json
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

from test_qol_rom_removal import prefix, read_rows, run_case

PROBE = '079B:2,0998:2,0A1C:2,0A1F:1,0A58:2,0AF6:2,0AFA:2,09C2:2,09DA:2'


def scenarios():
    cases = []
    for direction in 'hvd':
        select = {'h': 'press right+a 6\n', 'v': 'wait 1\n', 'd': 'press a+r 6\n'}[direction]
        pose = {'h': 0xc9, 'v': 0xcb, 'd': 0xcd}[direction]
        script = (prefix()+'press right 70\nwait 30\npoke 9c2 c700\npoke 9c4 c700\n'
            'poke a68 b400\npoke acc 0100\npress a 2\n'+select+
            f'until16 a1c == {pose:x} 200\nwait 2\ndump before\nwait 12\ndump flight\nwait 160\ndump settled\nquit\n')
        for mode in ('stock', 'half', 'off'):
            features = [] if mode == 'stock' else ['super-metroid.combat/shinespark-drain:drain='+mode]
            cases.append((direction+'-'+mode, features, script, PROBE))
    return cases


def validate(out, compare=None):
    observations = {}
    for direction in 'hvd':
        reference_rows = None
        for mode, expected_drain in (('stock', 12), ('half', 6), ('off', 0)):
            name = direction+'-'+mode
            path = out/name
            data = {tag: (path/(tag+'.wram.bin')).read_bytes() for tag in ('before', 'flight', 'settled')}
            word = lambda tag, address: int.from_bytes(data[tag][address:address+2], 'little')
            assert word('before', 0xa1c) == {'h': 0xc9, 'v': 0xcb, 'd': 0xcd}[direction], name
            assert word('before', 0x9c2)-word('flight', 0x9c2) == expected_drain, name
            assert word('settled', 0x998) == 8 and word('settled', 0xa1c) not in (0xc9, 0xcb, 0xcd), name
            assert (path/'before.sram.bin').read_bytes() == (path/'settled.sram.bin').read_bytes(), name
            if mode == 'off':
                assert all(word(tag, 0x9c2) == 199 for tag in data), name
            rows = read_rows(path/'wram.csv')
            trajectory = [{k: v for k, v in row.items() if k != '09c2'} for row in rows]
            if reference_rows is None:
                reference_rows = trajectory
            else:
                assert trajectory == reference_rows, (name, 'drain changed native flight/collision')
            if compare:
                assert rows == read_rows(compare/name/'wram.csv'), name
                for file in path.glob('*.wram.bin'):
                    assert file.read_bytes() == (compare/name/file.name).read_bytes(), (name, file.name)
                current = json.loads((path/'identity.json').read_text())['execution_share']['interp_insns']
                previous = json.loads((compare/name/'identity.json').read_text())['execution_share']['interp_insns']
                assert current > previous, (name, current, previous)
            observations[name] = dict(health=[word(tag, 0x9c2) for tag in data], flight_drain=expected_drain,
                                      end_pose=word('settled', 0xa1c))
    return observations


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for key in ('build', 'rom', 'out'):
        parser.add_argument('--'+key, type=Path, required=True)
    parser.add_argument('--interp', action='store_true')
    parser.add_argument('--compare', type=Path)
    args = parser.parse_args()
    args.out = args.out.resolve()
    args.out.mkdir(parents=True, exist_ok=False)
    targets = None
    if args.interp:
        targets = args.out/'targets.txt'
        targets.write_text('90D0AB\n10D0AB\n90D0D7\n10D0D7\n90D106\n10D106\nA08EB6\n208EB6\n')

    def run(case):
        return run_case(case, build=args.build.resolve(), rom=args.rom.resolve(), out=args.out, interp_targets=targets)
    with ThreadPoolExecutor(max_workers=2) as pool:
        identities = dict(pool.map(run, scenarios()))
    result = dict(observations=validate(args.out, args.compare), identities=identities)
    (args.out/'results.json').write_text(json.dumps(result, indent=2)+'\n')
    print('PASS: stock/half/zero drain12/6/0, three directions, same native trajectory/collision')


if __name__ == '__main__':
    main()
