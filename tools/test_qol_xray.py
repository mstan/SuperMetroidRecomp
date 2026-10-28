#!/usr/bin/env python3
"""Exercise actual X-ray widths and aim speeds, both facings and limits.

Equipment is a prerequisite. Normal B/Up/Down input opens, aims and closes the
native visor. Isolated muted headless profiles, TRACE=ON; ROM/SRAM unchanged.
"""
from pathlib import Path
import argparse,json

def parse_args():
    p=argparse.ArgumentParser(description=__doc__)
    for key in ('exe','rom','out'):p.add_argument('--'+key,type=Path,required=True)
    p.add_argument('--interp',action='store_true');p.add_argument('--compare',type=Path)
    return p.parse_args()

def validate(out,compare=None):
    for mode in ('stock','wide-1x','wide-2x','widest-1x','widest-2x'):
        width=10 if mode=='stock' else 26 if mode.startswith('widest') else 18
        for facing in ('left','right'):
            name=mode+'-'+facing;path=out/name;r=json.loads((path/'result.json').read_text())
            assert r['opened']['width']==width and r['opened']['frozen']==1,name
            assert abs(r['up']['angle']-r['opened']['angle'])==(10 if mode.endswith('2x') else 5),name
            assert r['down']['angle']==r['opened']['angle'],name
            assert r['top']['angle']==(256-width if facing=='left' else width),name
            assert r['bottom']['angle']==(128+width if facing=='left' else 128-width),name
            assert r['closed']['frozen']==r['closed']['width']==0,name
            if compare:
                assert r==json.loads((compare/name/'result.json').read_text()),name
                for file in list(path.glob('*.wram'))+list(path.glob('*.bmp')):
                    assert file.read_bytes()==(compare/name/file.name).read_bytes(),(name,file.name)
                current=json.loads((path/'execution.json').read_text())['interp_stats']['interp_insns']
                previous=json.loads((compare/name/'execution.json').read_text())['interp_stats']['interp_insns']
                assert current>previous,(name,current,previous)

def main():
    from pathlib import Path
    from concurrent.futures import ThreadPoolExecutor
    import sys,json,struct
    args=parse_args()
    from sm_progression_qa import Session
    out=args.out.resolve();out.mkdir(parents=True,exist_ok=False)
    modes={'stock':None,'wide-1x':'width=wide,aim-speed=1x','wide-2x':'width=wide,aim-speed=2x','widest-1x':'width=widest,aim-speed=1x','widest-2x':'width=widest,aim-speed=2x'}
    def run(case):
        mode,facing=case;name=mode+'-'+facing
        game=Session(args.exe,args.rom,out/name,
            features=[] if modes[mode] is None else ['super-metroid.balance-fixes/xray:'+modes[mode]],capture_profile=False,
            interpret_targets=(0x888754,0x8887e0,0x888835,0xa08eb6) if args.interp else ())
        def sample():
            data=game.read(0xa78,16);w=lambda a:struct.unpack_from('<H',data,a-0xa78)[0]
            return dict(frozen=w(0xa78),angle=w(0xa82),width=w(0xa84),subwidth=w(0xa86))
        try:
            game.boot();game.land();game.step(70,'right');game.step(30);game.step(2,facing);game.step(20)
            for addr,value in ((0x9a2,0x8000),(0x9a4,0x8000),(0x9d2,5)):game.word(addr,value)
            before=sample();saved=game.command('read_sram 0 8192')['hex'];game.snapshot('before')
            game.step(90,'b');opened=sample();game.snapshot('opened');game.step(5,'b+up');up=sample();game.snapshot('up')
            game.step(5,'b+down');down=sample();game.snapshot('down');game.step(120,'b+up');top=sample();game.step(120,'b+down');bottom=sample()
            game.step(90);closed=sample();game.snapshot('closed');assert game.word(0x998)==8 and closed['frozen']==0,(name,closed,game.state())
            assert saved==game.command('read_sram 0 8192')['hex'],name
            result=dict(before=before,opened=opened,up=up,down=down,top=top,bottom=bottom,closed=closed)
            assert opened['width']==(10 if mode=='stock' else 26 if mode.startswith('widest') else 18),(name,result)
            assert abs(up['angle']-opened['angle'])==(10 if mode.endswith('2x') else 5),(name,result)
            assert down['angle']==opened['angle'],(name,result)
            (out/name/'result.json').write_text(json.dumps(result,indent=2));print(name,flush=True);return name,result
        finally:game.close()
    with ThreadPoolExecutor(max_workers=2) as pool:results=dict(pool.map(run,[(mode,facing) for mode in modes for facing in ('left','right')]))
    (out/'results.json').write_text(json.dumps(results,indent=2))

    validate(out,args.compare)
    print("PASS: native visor widths10/18/26, aim steps1/2, both facings and clamps")

if __name__=='__main__':main()
