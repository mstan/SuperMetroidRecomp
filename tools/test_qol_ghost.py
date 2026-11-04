#!/usr/bin/env python3
"""Exercise a native Wrecked Ship ghost at slot zero and a relocated slot.

The initialized enemy's complete 64-byte record and used extra-RAM records are
relocated before play resumes; this is a constructed nonzero-slot prerequisite,
not a stock room population. The native AI chooses its states and coordinates.
No expected AI result, target coordinate, timer or handler is written.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

from sm_progression_qa import Rom, Session


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for key in ('exe', 'rom', 'out'):
        parser.add_argument('--' + key, type=Path, required=True)
    parser.add_argument('--interp', action='store_true')
    parser.add_argument('--compare', type=Path)
    args = parser.parse_args()
    args.out = args.out.resolve()
    rom = Rom(args.rom)
    base = 0x83a2a0
    door = dict(pointer=base & 0xffff, destination=rom.word(base),
                direction=rom.byte(base + 3), cap_x=rom.byte(base + 4), cap_y=rom.byte(base + 5))
    assert door['destination'] == 0xcccb
    args.out.mkdir(parents=True, exist_ok=False)

    def run(case):
        slot, mode = case
        name = f'slot{slot:02x}-{mode}'
        game = Session(args.exe, args.rom, args.out / name, capture_profile=False,
                       features=[] if mode == 'control' else ['super-metroid.balance-fixes/bug-fixes'],
                       # The ghost roots alone are already LLE-authoritative
                       # in this workload. Also deny the available compiled
                       # enemy scheduler to exercise surrounding execution.
                       interpret_targets=(0xa08eb6, 0xa89bad, 0xa89d36) if args.interp else ())
        try:
            game.boot()
            game.land()
            game.transition(door)
            for address in (0xaf6, 0xb10, 0xafa, 0xb14):
                game.word(address, 128)
            game.word(0x18a8, 0xffff)
            assert game.word(0xf78) == 0xe77f
            if slot:
                assert game.word(0xf78 + slot) == 0
                for base in (0xf78, 0x7000, 0x7800):
                    game.write(base + slot, game.read(base, 0x40))
                    game.write(base, bytes(0x40))
            assert game.word(0xf78 + slot) == 0xe77f
            game.snapshot('before')
            samples = []
            for tick in range(0, 901, 5):
                if tick:
                    game.step(5)
                samples.append(dict(tick=tick, samus_x=game.word(0xaf6), samus_y=game.word(0xafa),
                    ghost_x=game.word(0xf7a + slot), ghost_y=game.word(0xf7e + slot),
                    handler=game.word(0xfa8 + slot), timer=game.word(0xfaa + slot),
                    target_x=game.word(0x780c + slot), target_y=game.word(0x7814 + slot)))
            assert game.word(0xf78 + slot) == 0xe77f
            assert game.word(0x998) == 8
            game.snapshot('after')
            return name, dict(slot=slot, samples=samples,
                assistance='Native room load, Samus position/invulnerability; initialized ghost record relocated to unused slot240')
        finally:
            game.close()

    cases = [(slot, mode) for slot in (0, 0x240) for mode in ('control', 'mod')]
    with ThreadPoolExecutor(max_workers=2) as pool:
        result = dict(pool.map(run, cases))
    # Slot-zero loads are already correctly indexed in stock; relocating a
    # corrected ghost must retain exactly the same relative AI trajectory.
    zero = result['slot00-control']['samples']
    assert zero == result['slot00-mod']['samples']
    assert zero == result['slot240-mod']['samples']
    assert zero != result['slot240-control']['samples']
    assert any(s['target_y'] == s['samus_y'] for s in zero if s['target_y'])
    result['binary_sha256'] = hashlib.sha256(args.exe.read_bytes()).hexdigest()
    if args.compare:
        old = json.loads((args.compare / 'results.json').read_text())
        for slot, mode in cases:
            name = f'slot{slot:02x}-{mode}'
            assert result[name] == old[name], name
            for path in (args.out / name).glob('*.wram'):
                assert path.read_bytes() == (args.compare / name / path.name).read_bytes(), (name, path.name)
            current = json.loads((args.out / name / 'execution.json').read_text())
            prior = json.loads((args.compare / name / 'execution.json').read_text())
            assert current['interp_stats']['interp_insns'] > prior['interp_stats']['interp_insns'], name
        result['comparison'] = 'All sampled native AI fields/full WRAM snapshots identical; actual interpreter work increases'
    (args.out / 'results.json').write_text(json.dumps(result, indent=2) + '\n')
    print('Native ghost zero/nonzero-slot assertions PASS', flush=True)


if __name__ == '__main__':
    main()
