#!/usr/bin/env python3
"""Save at the actual ship, including a normal quit at Save Completed.

Resource/selected-slot prerequisites only. Native Down/Yes/No, ship refill,
dialog, SaveToSram and host exit must perform the work. No result flags or
SRAM bytes are written by the fixture. Private muted/headless profiles.
"""
from __future__ import annotations

import argparse
import json
import struct
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

from sm_progression_qa import Rom
from test_qol_rom_removal import SOURCE, prefix, read_rows, run_case, tomllib

BUGS = tomllib.loads((SOURCE/'mods/preloaded/packages/super-metroid.balance-fixes/1.0.0/manifest.toml').read_text())
OPTIONS = [option['id'] for option in BUGS['option'] if option['feature'] == 'bug-fixes']
RESOURCE_WORDS = dict(health=0x9c2, reserve=0x9d6, missiles=0x9c6, supers=0x9ca, powerbombs=0x9ce)
FULL = dict(health=499, reserve=300, missiles=50, supers=15, powerbombs=10)
CASES = (('stock', 'yes', 0, False), ('off', 'yes', 0, False), ('fix', 'yes', 0, False),
         ('default', 'yes', 0, False), ('fix', 'yes', 1, False), ('fix', 'yes', 2, False),
         ('stock', 'no', 0, False), ('fix', 'no', 0, False),
         ('stock', 'yes', 0, True), ('fix', 'yes', 0, True))
PROBE = '079B:2,0998:2,0A42:2,0FB2:2,1C1F:2,05F9:2,0952:2,09C2:2,09C4:2,09D6:2'


def name(case):
    mode, answer, slot, early = case
    return f'{mode}-{answer}-{slot}'+('-quit-at-message' if early else '')


def scenario(case):
    mode, answer, slot, early = case
    features = []
    if mode != 'stock':
        values = '' if mode == 'default' else ':'+','.join(option+'='+str(mode == 'fix' and option == 'ship-save').lower() for option in OPTIONS)
        features = ['super-metroid.balance-fixes/bug-fixes'+values]
    script = prefix()+f'poke 952 {slot:02x}00\n'
    for address, value in ((0x9c2, 37), (0x9c4, 499), (0x9c6, 1), (0x9c8, 50),
                           (0x9ca, 1), (0x9cc, 15), (0x9ce, 0), (0x9d0, 10), (0x9d4, 300), (0x9d6, 0)):
        script += f'poke {address:x} {value & 255:02x}{value >> 8:02x}\n'
    script += 'dump before\npress down 1\nuntil16 1c1f == 1c 5000\nwait 90\ndump prompt\n'
    if answer == 'no':
        script += 'press right 8\nuntil16 5f9 == 2 500\n'
    script += 'press a 8\n'
    if answer == 'yes':
        script += 'until16 1c1f == 18 1000\n'
    # Ship message18 is assigned before a 160-frame refill sound lead-in.
    # Wait for the actual confirmation to open, with no dismissal input.
    script += 'wait 400\ndump undismissed\n'
    if not early:
        if answer == 'yes':
            script += 'press a 8\n'
        script += 'wait 600\nuntil16 a42 == e695 2000\nuntil16 fb2 == a9bd 2000\ndump after\n'
    return name(case), features, script+'quit\n', PROBE


def saved_resources(rom, data, slot):
    base = rom.word(0x81812b+2*slot)
    word = lambda offset: struct.unpack_from('<H', data, offset)[0]
    size = 0xde1c-0xd7c0
    checksum = sum(struct.unpack_from('<'+str(size//2)+'H', data, base)) & 65535
    for offset, expected in ((0, checksum), (0x1ff0, checksum), (8, checksum ^ 65535), (0x1ff8, checksum ^ 65535)):
        assert word(offset+2*slot) == expected, (slot, offset, 'native checksum')
    return {label: word(base+address-0x9a2) for label, address in RESOURCE_WORDS.items()}


def validate(out, rom, compare=None):
    observations = {}
    for case in CASES:
        mode, answer, slot, early = case
        path = out/name(case)
        tags = ('before', 'prompt', 'undismissed') if early else ('before', 'prompt', 'undismissed', 'after')
        ram = {tag: (path/(tag+'.wram.bin')).read_bytes() for tag in tags}
        sram = {tag: (path/(tag+'.sram.bin')).read_bytes() for tag in tags}
        word = lambda tag, address: struct.unpack_from('<H', ram[tag], address)[0]
        assert word('prompt', 0x1c1f) == 0x1c and word('prompt', 0x5f9) == 0, name(case)
        assert {label: word('prompt', address) for label, address in RESOURCE_WORDS.items()} == FULL, name(case)
        fixed = mode in ('fix', 'default')
        changed_early = sram['undismissed'] != sram['before']
        assert changed_early == (fixed and answer == 'yes'), (name(case), changed_early)
        if answer == 'yes':
            assert word('undismissed', 0x1c1f) == 0x18, name(case)
        else:
            assert word('undismissed', 0x5f9) == 2 and word('undismissed', 0x1c1f) == 0x1c, name(case)
        final_tag = 'undismissed' if early else 'after'
        final = sram[final_tag]
        if answer == 'no' or (early and not fixed):
            assert final == sram['before'], name(case)
        else:
            assert saved_resources(rom, final, slot) == FULL, name(case)
            base = rom.word(0x81812b+2*slot)
            allowed = set(range(base, base+0xde1c-0xd7c0))
            for offset in (0, 8, 0x1ff0, 0x1ff8):
                allowed.update((offset+2*slot, offset+2*slot+1))
            assert all(a == b or i in allowed for i, (a, b) in enumerate(zip(sram['before'], final))), (name(case), 'unselected save changed')
        if fixed and answer == 'yes' and not early:
            assert sram['undismissed'] == final, (name(case), 'saved data changed after confirming completion')
        # This is an actual clean host exit, not TerminateProcess: verify the
        # battery file flushed by the same path as closing the game normally.
        assert (path/'saves/save.srm').read_bytes() == final, (name(case), 'normal quit did not persist SRAM')
        if not early:
            assert word('after', 0xa42) == 0xe695 and word('after', 0xfb2) == 0xa9bd, name(case)
        if compare:
            assert read_rows(path/'wram.csv') == read_rows(compare/path.name/'wram.csv'), name(case)
            for file in list(path.glob('*.wram.bin'))+list(path.glob('*.sram.bin')):
                assert file.read_bytes() == (compare/path.name/file.name).read_bytes(), (name(case), file.name)
            current = json.loads((path/'identity.json').read_text())['execution_share']['interp_insns']
            previous = json.loads((compare/path.name/'identity.json').read_text())['execution_share']['interp_insns']
            assert current > previous, (name(case), current, previous)
        observations[name(case)] = dict(changed_before_dismissal=changed_early, persisted_on_normal_quit=True,
            selected_slot=slot, native_file_valid=answer == 'yes' and (not early or fixed))
    for file in (out/'stock-yes-0').glob('*.wram.bin'):
        assert file.read_bytes() == (out/'off-yes-0'/file.name).read_bytes(), ('option-off', file.name)
    return observations


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for key in ('build', 'rom', 'out'):
        parser.add_argument('--'+key, type=Path, required=True)
    parser.add_argument('--interp', action='store_true')
    parser.add_argument('--compare', type=Path)
    args = parser.parse_args()
    rom = Rom(args.rom)
    args.out = args.out.resolve()
    args.out.mkdir(parents=True, exist_ok=False)
    targets = None
    if args.interp:
        targets = args.out/'targets.txt'
        targets.write_text('A2AB1F\n22AB1F\n858080\n058080\nA08EB6\n208EB6\n')

    def run(case):
        return run_case(scenario(case), build=args.build.resolve(), rom=args.rom.resolve(), out=args.out, interp_targets=targets)
    with ThreadPoolExecutor(max_workers=2) as pool:
        identities = dict(pool.map(run, CASES))
    result = dict(observations=validate(args.out, rom, args.compare), identities=identities)
    (args.out/'results.json').write_text(json.dumps(result, indent=2)+'\n')
    print('PASS: ship Yes/No, default/on/off, all three slots, actual quit at Saved message and native file integrity')


if __name__ == '__main__':
    main()
