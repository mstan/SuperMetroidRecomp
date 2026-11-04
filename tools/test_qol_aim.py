#!/usr/bin/env python3
"""Check held aim through built host transforms, native poses and current-state restore."""
import argparse
import hashlib
import json
import struct
from pathlib import Path
from sm_progression_qa import Session
from test_qol_rom_removal import ROM_SHA


def main():
    p = argparse.ArgumentParser(description=__doc__)
    for name in ('build', 'rom', 'out'):
        p.add_argument('--' + name, type=Path, required=True)
    p.add_argument('--laser', action='store_true', help='check the presented sight against the native muzzle')
    p.add_argument('--wide', action='store_true')
    args = p.parse_args()
    args.out = args.out.resolve()
    args.out.mkdir(parents=True, exist_ok=False)
    image = args.rom.read_bytes()
    assert hashlib.sha256(image).hexdigest() == ROM_SHA
    pose_table = 0x11 * 0x8000 + (0xB629 & 0x7fff)
    results = {}
    for mode in ('stock', 'R', 'L'):
        shoulder = mode.lower() if mode != 'stock' else 'r'
        features = [] if mode == 'stock' else [
            'super-metroid.controls/zero-mission:aim-shoulder=' + mode + ',missile=off']
        if args.laser and mode != 'stock': features.append('super-metroid.map-hud/laser-sight')
        game = Session(args.build/'SuperMetroidSNESRecomp.exe', args.rom,
                       args.out/mode, features=features, capture_profile=False, wide=args.wide)
        observations = {}
        def check(tag, expected, shot=False):
            pose = game.word(0xa1c)
            direction = image[pose_table + pose * 8 + 3]
            assert direction == expected, (mode, tag, hex(pose), direction, expected)
            if shot:
                assert game.word(0xcce) > 0, (mode, tag, 'no shot')
                assert game.word(0xc04) & 15 == expected, (mode, tag, 'wrong shot direction')
            observations[tag] = dict(frame=game.command('frame')['frame'], pose=pose, direction=direction)
            game.snapshot(tag)
            if args.laser and mode != 'stock':
                # Read the actual composed BMP, including the sight overlay.
                bmp=(args.out/mode/(tag+'.bmp')).read_bytes()
                offset=struct.unpack_from('<I',bmp,10)[0]
                width,height=struct.unpack_from('<ii',bmp,18)
                bits=struct.unpack_from('<H',bmp,28)[0]
                assert bits in (24,32) and width>=256
                stride=((width*bits+31)//32)*4
                def red(x,y):
                    row=y if height<0 else height-1-y
                    at=offset+row*stride+x*(bits//8)
                    return bmp[at:at+3]==b'\x30\x30\xff'
                # $90:BA56 standing shot offsets, independent of the overlay.
                ox,oy={1:(13,-13),3:(13,4),6:(-14,4),8:(-19,-19)}[direction]
                sx=game.word(0xaf6)-game.word(0x911)+(width-256)//2
                sy=game.word(0xafa)-game.word(0x915)
                dx=1 if direction in (1,3) else -1
                dy=-1 if direction in (1,8) else 1
                x=sx+ox+6*dx; y=sy+oy-image[pose_table+pose*8+4]+6*dy
                assert all(red(x+a,y+b) for a,b in ((0,0),(-1,0),(1,0),(0,-1))), (mode,tag,'muzzle axis/width',x,y)
                if not red(x,y+1):
                    # Downward aim is immediately above the Landing Site floor.
                    # The sight's lower edge must stop at the actual solid tile.
                    wx=x-(width-256)//2+game.word(0x911);wy=y+1+game.word(0x915)
                    block=game.word(0x10002+2*((wy//16)*game.word(0x7a5)+wx//16))>>12
                    assert block==1 or block>=8,(mode,tag,'missing edge over empty terrain',block)
                observations[tag]['laser_center']=[x,y]
        try:
            game.boot(); game.land()
            game.step(70, 'right'); game.step(30)
            for facing, up, down in (('right', 1, 3), ('left', 8, 6)):
                game.step(4, facing); game.step(12)
                game.step(12, shoulder+'+down'); game.step(30, shoulder)
                expected = up if mode == 'stock' else down
                check(facing+'-neutral', expected)
                game.step(12); game.step(12, shoulder)
                check(facing+'-repress', expected)
                # Add fire without releasing aim; native pose is established.
                game.step(3, shoulder+'+x')
                check(facing+'-fire', expected, True)
                game.step(12, shoulder+'+up'); game.step(12, shoulder)
                check(facing+'-up', up)
                game.step(12)
            if mode != 'stock':
                game.step(12, shoulder+'+down'); game.step(12, shoulder)
                observations['roundtrip'] = game.save_roundtrip()
                game.step(12, shoulder)
                check('after-restore', 3)  # roundtrip's continuation walks right
            results[mode] = observations
        finally:
            game.close()
    (args.out/'results.json').write_text(json.dumps(dict(
        observations=results,
        exe_sha256=hashlib.sha256((args.build/'SuperMetroidSNESRecomp.exe').read_bytes()).hexdigest(),
        input_path='debug controller -> ModTransformInputs -> native game',
        lifetime='local per-seat session preference; activation starts up; loading guest state preserves the preference',
        setup_bypasses=['stock Ceres-to-Landing Site load from existing fixture']), indent=2))
    print('PASS: both shoulders/facings retain aim and fire; stock and current-build restore pass')


if __name__ == '__main__':
    main()
