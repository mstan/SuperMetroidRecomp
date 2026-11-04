#!/usr/bin/env python3
"""Reach native Speed Booster sound activation, then stop it normally.

Only equipment is granted. Native running reaches full boost and queues its
sound. This checks the restored boost stage/animation after that call, not
subjective audio quality. Uses isolated muted profiles and an owned stock ROM.
"""
from __future__ import annotations

import argparse
import json
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

from test_qol_rom_removal import prefix, read_rows, run_case

PROBE = '079B:2,0998:2,0AF6:2,0AFA:2,0A1F:1,0B3C:2,0B3E:2,0B40:2,0A96:2,0A94:2,0641:2,0643:2,0645:2'


def scenarios():
    script = prefix() + 'poke 9a2 0020\npoke 9a4 0020\npress left+b 300\nwait 60\ndump final\nquit\n'
    return [(mode, [] if mode == 'control' else ['super-metroid.balance-fixes/bug-fixes'], script, PROBE)
            for mode in ('control', 'mod')]


def validate(out, compare=None):
    observations = {}
    for mode, *_ in scenarios():
        rows = read_rows(out / mode / 'wram.csv')
        boosted = [r for r in rows if r['0b3e'] & 0x400]
        assert boosted and rows[-1]['0998'] == 8, mode
        first = boosted[0]
        assert first['0a1f'] == 1 and first['0b40'] == 1, first
        assert first['0b3e'] == (0x401 if mode == 'control' else 0x402), first
        assert first['0a94'] == (3 if mode == 'control' else 1), first
        assert rows[-1]['0b40'] == rows[-1]['0b3e'] == 0, rows[-1]
        observations[mode] = dict(boosted_rows=len(boosted), first_boost=first, stopped=rows[-1])
        if compare:
            assert rows == read_rows(compare / mode / 'wram.csv'), mode
            for path in (out / mode).glob('*.wram.bin'):
                assert path.read_bytes() == (compare / mode / path.name).read_bytes(), (mode, path.name)
    control = observations['control']['first_boost']
    fixed = observations['mod']['first_boost']
    for key in ('frame', '0af6', '0afa'):
        assert control[key] == fixed[key], key
    return observations


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for key in ('build', 'rom', 'out'):
        parser.add_argument('--' + key, type=Path, required=True)
    parser.add_argument('--interp-targets', type=Path)
    parser.add_argument('--compare', type=Path)
    args = parser.parse_args()
    args.out = args.out.resolve()
    args.out.mkdir(parents=True, exist_ok=False)

    def run(case):
        return run_case(case, build=args.build.resolve(), rom=args.rom.resolve(),
                        out=args.out, interp_targets=args.interp_targets)
    with ThreadPoolExecutor(max_workers=2) as pool:
        identities = dict(pool.map(run, scenarios()))
    result = dict(observations=validate(args.out, args.compare), identities=identities)
    if args.compare:
        old = json.loads((args.compare / 'results.json').read_text())
        for mode in identities:
            assert identities[mode]['execution_share']['interp_insns'] > old['identities'][mode]['execution_share']['interp_insns'], mode
        result['comparison'] = 'Every observed frame/full WRAM snapshot identical; interpreter instructions increase'
    (args.out / 'results.json').write_text(json.dumps(result, indent=2) + '\n')
    print('Native boost/sound-call state and stop assertions PASS', flush=True)


if __name__ == '__main__':
    main()
