#!/usr/bin/env python3
"""Exercise English intro skipping, its Japanese guard and new-game Ceres skip.

Headless, muted isolated profiles; requires TRACE=ON. Ending starts at native
game state26 with final battle explicitly bypassed. Intro language is a fixture
prerequisite. No cinematic pointer, skip result or item/SRAM outcome is written.
Alternate execution includes compiled input/music consumers; cinematic roots
may remain interpreter-authoritative. Compare complete WRAM and actual counts.
"""
from pathlib import Path
import argparse, json

def parse_args():
    parser=argparse.ArgumentParser(description=__doc__)
    for key in ('exe','rom','out'):parser.add_argument('--'+key,type=Path,required=True)
    parser.add_argument('--interp',action='store_true')
    parser.add_argument('--compare',type=Path)
    return parser.parse_args()

def validate(out,compare=None):
    if not compare:return
    for path in out.iterdir():
        if not path.is_dir():continue
        assert json.loads((path/'result.json').read_text())==json.loads((compare/path.name/'result.json').read_text()),path.name
        for file in path.glob('*.wram'):
            assert file.read_bytes()==(compare/path.name/file.name).read_bytes(),(path.name,file.name)
        current=json.loads((path/'execution.json').read_text())['interp_stats']['interp_insns']
        previous=json.loads((compare/path.name/'execution.json').read_text())['interp_stats']['interp_insns']
        assert current>previous,(path.name,current,previous)

def main():
    from pathlib import Path
    from concurrent.futures import ThreadPoolExecutor
    import sys,json
    args = parse_args()
    from sm_progression_qa import Session
    out=args.out.resolve();out.mkdir(parents=True,exist_ok=False)

    def run(mode):
        mods=[] if 'control' in mode else ['super-metroid.pacing/'+('ceres-skip' if 'ceres' in mode else 'intro-skip')]
        game=Session(args.exe,args.rom,out/mode,features=mods,capture_profile=False,
            interpret_targets=(0x809459,0x808f0c,0x8ba35b,0x82edcc) if args.interp else ())
        def state():return dict(frame=game.command('frame')['frame'],state=game.word(0x998),room=game.word(0x79b),fn=game.word(0x1f51),japanese=game.word(0x9e2))
        try:
            if 'ceres' in mode:
                game.boot();result=dict(final=state());assert result['final']['room']==(0xdf45 if 'control' in mode else 0x91f8),result
            else:
                game.step(210)
                for buttons,frames,wait in [('start',8,120),('start',8,360),('start',12,240)]:game.step(frames,buttons);game.step(wait)
                for _ in range(80):
                    if game.word(0x998)==0x1e:break
                    game.step(8,'a');game.step(30)
                assert game.word(0x998)==0x1e,state()
                # Language is a prerequisite only; do not manufacture a skip result.
                game.word(0x9e2,int('japanese' in mode))
                for _ in range(400):
                    if game.word(0x1f51)==0xaeb8:break
                    game.step(30)
                assert game.word(0x1f51)==0xaeb8,state()
                before=state();game.snapshot('story_page');game.step(1,'start');pressed=state();game.step(1);game.step(30);after=state();game.snapshot('after_start')
                if mode=='english-mod':assert pressed['fn']==0xb72f,(before,pressed,after)
                else:assert pressed['fn']!=0xb72f,(before,pressed,after)
                for _ in range(80):
                    if game.word(0x998)==8:break
                    game.step(8,'a');game.step(300)
                final=state();assert final['state']==8 and final['room']==0xdf45,final
                game.snapshot('gameplay');result=dict(before=before,pressed=pressed,after=after,final=final)
            (out/mode/'result.json').write_text(json.dumps(result,indent=2));return mode,result
        finally:game.close()
    with ThreadPoolExecutor(max_workers=2) as pool:result=dict(pool.map(run,('english-control','english-mod','japanese-control','japanese-mod','ceres-control','ceres-mod')))
    (out/'results.json').write_text(json.dumps(result,indent=2))

    validate(out,args.compare)
    print('PASS: intro English/Japanese guard and native Ceres skip')

if __name__=='__main__':main()
