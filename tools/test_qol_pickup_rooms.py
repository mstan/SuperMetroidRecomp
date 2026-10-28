#!/usr/bin/env python3
"""Check cut-all jingles on real suit, first-missile and reserve pickups.

Native rooms, item PLMs, shots and collision perform collection. Only position
and Gravity room power are assisted; the Phantoon fight is explicitly bypassed.
No PLM list, inventory, item or music results are injected. All runs are muted,
headless and isolated. Native suit animations must still finish normally.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import struct
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

from sm_progression_qa import Rom, Session

# kind, room, item block x/y, native PLM header
ITEMS = (('varia', 0xa6e2, 7, 9, 0xef5b), ('gravity', 0xce40, 7, 9, 0xef5f),
         ('missile', 0x95ff, 14, 9, 0xeedb), ('reserve', 0x9c07, 11, 7, 0xef7b))


def validate(out, compare=None):
    observations = {}
    for kind, *_ in ITEMS:
        for mode in ('control', 'mute'):
            name = kind+'-'+mode
            path = out/name
            result = json.loads((path/'result.json').read_text())
            assert (2 in result['unique_tracks']) == (mode == 'control'), name
            if mode == 'mute':
                assert result['unique_tracks'] == [result['before']['track']], name
            assert result['final']['stage'] == 0 and result['final']['input'] == 0xe695, name
            if compare:
                previous = json.loads((compare/name/'result.json').read_text())
                for field in ('before', 'first', 'events', 'final', 'unique_tracks'):
                    assert result[field] == previous[field], (name, field)
                for file in path.glob('*.wram'):
                    assert file.read_bytes() == (compare/name/file.name).read_bytes(), (name, file.name)
                current = json.loads((path/'execution.json').read_text())['interp_stats']['interp_insns']
                original = json.loads((compare/name/'execution.json').read_text())['interp_stats']['interp_insns']
                assert current > original, (name, current, original)
            observations[name] = dict(tracks=result['unique_tracks'], first_tick=result['first']['tick'])
    return observations


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for key in ('exe', 'rom', 'out'):
        parser.add_argument('--'+key, type=Path, required=True)
    parser.add_argument('--interp', action='store_true')
    parser.add_argument('--compare', type=Path)
    args = parser.parse_args()
    rom = Rom(args.rom)
    rooms = rom.rooms()
    args.out = args.out.resolve()
    args.out.mkdir(parents=True, exist_ok=False)

    def run(case):
        (kind, room, bx, by, header), mode = case
        name = kind+'-'+mode
        door = next(d for source in rooms.values() for d in source['doors'] if d['destination'] == room)
        features = [] if mode == 'control' else [
            'super-metroid.pacing/item-pickup:mute-jingle=true,short-jingle=true,no-freeze=false,skip-message=false']
        game = Session(args.exe, args.rom, args.out/name, features=features, capture_profile=False,
            interpret_targets=(0xa08eb6, 0x848bdd, 0x8488f3, 0x8489a9, 0x848986, 0x808f0c) if args.interp else ())

        def sample(tick):
            data = game.read(0x600, 0x7ee)
            word = lambda address: struct.unpack_from('<H', data, address-0x600)[0]
            return dict(tick=tick, items=word(0x9a4), max_missiles=word(0x9c8), max_reserve=word(0x9d4),
                track=data[0x4c], stage=word(0xdec), state=word(0x998), input=word(0xa42))
        try:
            game.boot()
            game.land()
            if kind == 'gravity':
                game.write(0xd82b, b'\x01')  # room power only; no boss-defeat claim
            game.transition(door)
            # Ensure this is the native object at the claimed location.
            slots = game.read(0x1c37, 0xa0)
            block = (bx+by*rooms[room]['width']*16)*2
            assert any(struct.unpack_from('<H', slots, index)[0] == header and
                       struct.unpack_from('<H', slots, 0x50+index)[0] == block
                       for index in range(0, 80, 2)), (name, room, header, block)
            x, y = bx*16+8, by*16
            for address, value in ((0xaf6, x-48), (0xb10, x-48), (0xafa, y+24), (0xb14, y+24)):
                game.word(address, value)
            game.step(3, 'right')
            game.word(0xafa, y+24)
            game.word(0xb14, y+24)
            game.step(8, 'x')
            game.step(20)
            for address, value in ((0xaf6, x), (0xb10, x), (0xafa, y-32), (0xb14, y-32)):
                game.word(address, value)
            before = sample(-1)
            game.snapshot('before')
            sram = game.command('read_sram 0 8192')['hex']
            events, first = [], None
            for tick in range(1500):
                game.step(1, 'a' if tick > 400 and tick % 60 < 8 else '0')
                state = sample(tick)
                events.append(state)
                acquired = (state['items'] & (1 if kind == 'varia' else 0x20)) if kind in ('varia', 'gravity') else (
                    state['max_missiles'] if kind == 'missile' else state['max_reserve'])
                if acquired and first is None:
                    first = state.copy()
                    game.snapshot('granted')
                if first and tick-first['tick'] > 800 and state['state'] == 8 and state['stage'] == 0 and state['input'] == 0xe695:
                    break
            assert first is not None, (name, state)
            game.dismiss_dialog()
            game.step(5)
            game.snapshot('completed')
            final = sample(tick)
            assert final['state'] == 8 and final['stage'] == 0 and final['input'] == 0xe695, (name, final)
            assert sram == game.command('read_sram 0 8192')['hex'], name
            result = dict(item=dict(room=room, x=bx, y=by, plm=header), before=before, first=first,
                events=events, final=final, unique_tracks=sorted({v['track'] for v in events}),
                prerequisites='Native room/object/contact; position assistance and Gravity room power only')
            (args.out/name/'result.json').write_text(json.dumps(result, indent=2))
            print(name, result['unique_tracks'], flush=True)
        finally:
            game.close()
    with ThreadPoolExecutor(max_workers=2) as pool:
        list(pool.map(run, [(item, mode) for item in ITEMS for mode in ('control', 'mute')]))
    result = dict(observations=validate(args.out, args.compare),
                  exe_sha256=hashlib.sha256(args.exe.read_bytes()).hexdigest())
    (args.out/'results.json').write_text(json.dumps(result, indent=2)+'\n')
    print('PASS: native suit, first-ammo and reserve pickup jingles')


if __name__ == '__main__':
    main()
