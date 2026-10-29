#!/usr/bin/env python3
"""Ammo capacity is pause-only; gameplay pixels and native state stay stock."""
from pathlib import Path
import argparse,hashlib,json
from sm_progression_qa import Session

def main():
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('exe','rom','out'):p.add_argument('--'+name,type=Path,required=True)
    a=p.parse_args();a.out=a.out.resolve();a.out.mkdir(exist_ok=False)
    rows={}
    for mode in ('off','on'):
        game=Session(a.exe,a.rom,a.out/mode,capture_profile=False,
                     features=[] if mode=='off' else ['super-metroid.map-hud/ammo-totals'])
        try:
            game.boot();game.land()
            for address,value in ((0x9c6,5),(0x9c8,99),(0x9ca,2),(0x9cc,30),(0x9ce,1),(0x9d0,20)):
                game.word(address,value)
            before=game.command('read_sram 0 8192')['hex']
            game.step(5);game.snapshot('gameplay')
            game.step(8,'start');game.step(120)
            assert game.word(0x998)==15
            game.snapshot('pause')
            assert game.command('read_sram 0 8192')['hex']==before
            rows[mode]=game.state()
        finally:game.close()
    for tag in ('gameplay','pause'):
        assert (a.out/'off'/f'{tag}.wram').read_bytes()==(a.out/'on'/f'{tag}.wram').read_bytes(),tag
    assert (a.out/'off/gameplay.bmp').read_bytes()==(a.out/'on/gameplay.bmp').read_bytes(),'Gameplay HUD changed'
    assert (a.out/'off/pause.bmp').read_bytes()!=(a.out/'on/pause.bmp').read_bytes(),'Pause capacity missing'
    result=dict(gameplay_pixels='stock identical',guest_WRAM='stock identical in gameplay and pause',
                pause_capacity='visible, MAX M 99 S 30 P 20; current counts remain native',SRAM='unchanged',
                binary_sha256=hashlib.sha256(a.exe.read_bytes()).hexdigest(),states=rows)
    (a.out/'results.json').write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps(result,indent=2))

if __name__=='__main__':main()
