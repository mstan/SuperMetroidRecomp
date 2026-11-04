#!/usr/bin/env python3
"""Verify native ship confirmation presentation and a current-build state reload.

Actual ship/Yes input; observe the original refill sound lead-in and box opening.
Save/load while Saved is visible must retain the native dialog phase and SRAM,
then dismiss and leave the ship normally. Muted/private/headless, TRACE=ON.
"""
from pathlib import Path
import argparse,json

def parse_args():
    p=argparse.ArgumentParser(description=__doc__)
    for key in ('exe','rom','out'):p.add_argument('--'+key,type=Path,required=True)
    p.add_argument('--interp',action='store_true');p.add_argument('--compare',type=Path)
    return p.parse_args()

def validate(out,compare=None):
    for mode in ('stock','fix'):
        path=out/mode;r=json.loads((path/'result.json').read_text())
        assert r['first_fully_open_tick']>=160,(mode,r)
        if mode=='fix':assert r['state']==dict(current_build_roundtrip=True,SRAM_unchanged=True)
        if compare:
            assert r==json.loads((compare/mode/'result.json').read_text()),mode
            for file in list(path.glob('*.wram'))+list(path.glob('*.bmp')):
                assert file.read_bytes()==(compare/mode/file.name).read_bytes(),(mode,file.name)
            current=json.loads((path/'execution.json').read_text())['interp_stats']['interp_insns']
            previous=json.loads((compare/mode/'execution.json').read_text())['interp_stats']['interp_insns']
            assert current>previous,(mode,current,previous)

def main():
    from pathlib import Path
    from concurrent.futures import ThreadPoolExecutor
    import sys,json
    args=parse_args()
    from sm_progression_qa import Session
    from test_qol_ship_save import OPTIONS
    out=args.out.resolve();out.mkdir(parents=True,exist_ok=False)

    def run(mode):
        values=','.join(option+'='+str(option=='ship-save').lower() for option in OPTIONS)
        game=Session(args.exe,args.rom,out/mode,
            features=[] if mode=='stock' else ['super-metroid.balance-fixes/bug-fixes:'+values],capture_profile=False,
            interpret_targets=(0xa2ab1f,0x858080,0xa08eb6) if args.interp else ())
        def sram():return game.command('read_sram 0 8192')['hex']
        try:
            game.boot();game.land()
            for a,v in ((0x9c2,37),(0x9c4,499),(0x9c6,1),(0x9c8,50),(0x9ca,1),(0x9cc,15),(0x9ce,0),(0x9d0,10),(0x9d4,300),(0x9d6,0)):game.word(a,v)
            saved_before=sram();game.step(1,'down');game.step(1)
            for _ in range(150):
                if game.word(0x1c1f)==0x1c:break
                game.step(30)
            else:raise AssertionError((mode,'prompt'))
            game.step(90);game.step(8,'a');game.step(1)
            for _ in range(120):
                if game.word(0x1c1f)==0x18:break
                game.step(1)
            else:raise AssertionError((mode,'saved index'))
            origin=game.command('frame')['frame'];first_full=None;rows=[]
            for tick in range(401):
                radius=game.word(0x5a2)
                if radius==0x1800 and first_full is None:first_full=tick
                if tick%10==0:rows.append(dict(tick=tick,radius=radius))
                if tick in (60,400):game.snapshot('at_'+str(tick))
                if tick<400:game.step(1)
            assert first_full is not None and first_full>=160,(mode,first_full,rows)
            saved_at_message=sram();assert (saved_at_message!=saved_before)==(mode=='fix'),mode
            state=None
            if mode=='fix':
                game.command('savestate 0');game.step(2);game.command('loadstate 0');game.step(2)
                assert sram()==saved_at_message and game.word(0x1c1f)==0x18 and game.word(0xfb2)==0xab1f,game.state()
                game.snapshot('restored');state=dict(current_build_roundtrip=True,SRAM_unchanged=True)
            game.dismiss_dialog();game.step(600);game.dismiss_dialog();game.step(60)
            assert game.word(0xa42)==0xe695 and game.word(0xfb2)==0xa9bd,game.state()
            assert sram()==saved_at_message if mode=='fix' else sram()!=saved_before,mode
            game.snapshot('after');result=dict(origin=origin,first_fully_open_tick=first_full,rows=rows,state=state)
            (out/mode/'result.json').write_text(json.dumps(result,indent=2));print(mode,first_full,state,flush=True);return mode,result
        finally:game.close()
    with ThreadPoolExecutor(max_workers=2) as pool:results=dict(pool.map(run,('stock','fix')))
    (out/'results.json').write_text(json.dumps(results,indent=2))

    validate(out,args.compare)

if __name__=='__main__':main()
