#!/usr/bin/env python3
"""Use a real save station, verify all refills and the actual native SRAM file.

Position and depleted/max resources are assisted prerequisites. Station contact,
the Yes prompt, Save Completed and SRAM checksums must be produced by native
code. Only private saves/settings are used; all launches are muted/headless.
Requires TRACE=ON. No station instructions or result flags are injected.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import struct
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

from sm_progression_qa import Rom, Session

INITIAL = ((0x9c2, 37), (0x9c4, 499), (0x9c6, 1), (0x9c8, 50), (0x9ca, 1),
           (0x9cc, 15), (0x9ce, 0), (0x9d0, 10), (0x9d4, 300), (0x9d6, 0))
RESOURCES = dict(health=0x9c2, reserve=0x9d6, missiles=0x9c6, supers=0x9ca, powerbombs=0x9ce)


def validate(out, compare=None):
    results = {}
    for mode in ('stock', 'refill'):
        path = out/mode
        result = json.loads((path/'result.json').read_text())
        expected = dict(health=499, reserve=300, missiles=50, supers=15, powerbombs=10) if mode == 'refill' else result['before']
        assert result['prompt'] == result['after'] == result['saved_resources'] == expected, mode
        assert result['save_used'] == 1 and result['sram_changed'] and result['checksum_valid'], mode
        if compare:
            assert result == json.loads((compare/mode/'result.json').read_text()), mode
            for file in list(path.glob('*.wram')) + list(path.glob('*.sram.bin')):
                assert file.read_bytes() == (compare/mode/file.name).read_bytes(), (mode, file.name)
            current = json.loads((path/'execution.json').read_text())['interp_stats']['interp_insns']
            previous = json.loads((compare/mode/'execution.json').read_text())['interp_stats']['interp_insns']
            assert current > previous, (mode, current, previous)
        results[mode] = result
    return results


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for key in ('exe', 'rom', 'out'):
        parser.add_argument('--'+key, type=Path, required=True)
    parser.add_argument('--interp', action='store_true')
    parser.add_argument('--compare', type=Path)
    args = parser.parse_args()
    rom = Rom(args.rom)
    rooms = rom.rooms()
    door = next(d for source in rooms.values() for d in source['doors'] if d['destination'] == 0x93d5)
    args.out = args.out.resolve()
    args.out.mkdir(parents=True, exist_ok=False)

    def run(mode):
        game = Session(args.exe, args.rom, args.out/mode,
            features=[] if mode == 'stock' else ['super-metroid.balance-fixes/save-station'], capture_profile=False,
            interpret_targets=(0xa08eb6, 0x848cf1, 0x818000, 0x858080) if args.interp else ())

        def inventory():
            data = game.read(0x9c2, 0x16)
            return {name: struct.unpack_from('<H', data, address-0x9c2)[0] for name, address in RESOURCES.items()}
        try:
            game.boot()
            game.land()
            game.transition(door)
            for address, value in INITIAL:
                game.word(address, value)
            for address, value in ((0xaf6, 96), (0xb10, 96), (0xafa, 120), (0xb14, 120)):
                game.word(address, value)
            before = inventory()
            before_sram = bytes.fromhex(game.command('read_sram 0 8192')['hex'])
            game.snapshot('before')
            for tick in range(400):
                game.step(1)
                if game.word(0x1c1f) == 0x17:
                    break
            else:
                raise AssertionError((mode, 'native prompt not reached', game.state()))
            prompt = inventory()
            game.snapshot('prompt')
            game.step(90)
            game.step(8, 'a')
            game.step(60)
            game.dismiss_dialog()
            game.step(360)
            # The station animation precedes Save Completed; wait for that
            # message too, rather than treating game_state8 as readiness.
            for _ in range(12):
                game.dismiss_dialog()
                game.step(60)
                if game.word(0x1e75) == 1:
                    break
            game.snapshot('completed')
            after = inventory()
            saved = bytes.fromhex(game.command('read_sram 0 8192')['hex'])
            slot = game.word(0x952)
            assert 0 <= slot < 3, slot
            base = rom.word(0x81812b+2*slot)
            # SaveToSram copies 09A2..0A00 to D7C0 and sums the native
            # D7C0..DE1B file into main/mirrored checksum+complement headers.
            size = 0xde1c-0xd7c0
            checksum = sum(struct.unpack_from('<'+str(size//2)+'H', saved, base)) & 65535
            word = lambda offset: struct.unpack_from('<H', saved, offset)[0]
            valid = all(word(offset+2*slot) == value for offset, value in
                        ((0, checksum), (0x1ff0, checksum), (8, checksum ^ 65535), (0x1ff8, checksum ^ 65535)))
            stored = {name: word(base+address-0x9a2) for name, address in RESOURCES.items()}
            (args.out/mode/'before.sram.bin').write_bytes(before_sram)
            (args.out/mode/'saved.sram.bin').write_bytes(saved)
            result = dict(before=before, prompt=prompt, after=after, prompt_tick=tick,
                save_used=game.word(0x1e75), sram_changed=saved != before_sram,
                saved_resources=stored, checksum_valid=valid, sram_sha256=hashlib.sha256(saved).hexdigest())
            (args.out/mode/'result.json').write_text(json.dumps(result, indent=2))
        finally:
            game.close()
    with ThreadPoolExecutor(max_workers=2) as pool:
        list(pool.map(run, ('stock', 'refill')))
    result = dict(observations=validate(args.out, args.compare))
    (args.out/'results.json').write_text(json.dumps(result, indent=2)+'\n')
    print('PASS: native station/contact/Yes/save, five resources, exact SRAM values and checksums')


if __name__ == '__main__':
    main()
