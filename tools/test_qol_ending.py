#!/usr/bin/env python3
"""Exercise native credits skip, preserved ending pose and final-screen return.

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
    import sys,json,struct
    args = parse_args()
    from sm_progression_qa import Session
    out=args.out.resolve();out.mkdir(parents=True,exist_ok=False)
    def run(mode):
        mods=[]
        if mode in ('credits','both'):mods.append('super-metroid.pacing/credits-skip')
        if mode in ('return','both'):mods.append('super-metroid.pacing/return-to-title')
        game=Session(args.exe,args.rom,out/mode,features=mods,capture_profile=False,
            interpret_targets=(0x809459,0x808f0c,0x8bd443,0x8ba35b,0x82edcc) if args.interp else ())
        events=[]
        def state():
            data=game.read(0x1997,0x5bc);word=lambda a:struct.unpack_from('<H',data,a-0x1997)[0]
            return dict(frame=game.command('frame')['frame'],state=game.word(0x998),fn=word(0x1f51),list=word(0x19f7),glow=word(0x19ff),var=word(0x1997))
        def record(st):
            if not events or events[-1]['fn']!=st['fn'] or events[-1]['state']!=st['state']:events.append(st.copy())
        try:
            game.boot();game.word(0x998,0x26) # explicit final-battle bypass; native ending init runs
            initial=state();limit=initial['frame']+22000
            while True:
                game.step(30);st=state();record(st)
                if st['state']==0x27 and st['glow']&0x8000 and 0xd91b<=st['list']<0xdc97:break
                assert st['frame']<limit,st
            staff=st.copy();game.snapshot('staff_roll')
            game.step(1,'start');st=state();record(st)
            rolled=st.copy() if st['fn']==0xe0f4 else None
            if rolled:game.snapshot('roll_end')
            game.step(1);st=state();record(st)
            while rolled is None:
                if st['fn']==0xe0f4:
                    rolled=st.copy();game.snapshot('roll_end');break
                game.step(1 if st['list']>=0xdc00 else 30);st=state();record(st)
                assert st['frame']<limit,st
            origin=rolled['frame'];pose_snapshots=set();pose_input=False
            while True:
                st=state();record(st);elapsed=st['frame']-origin
                if st['fn']==0xe7ba and st['var']>=0x8000 and st['var']-65536 < -79:break
                for mark in (500,1400):
                    if elapsed>=mark and mark not in pose_snapshots:
                        game.snapshot('pose_'+str(mark));pose_snapshots.add(mark)
                if elapsed>=500 and not pose_input:
                    # Start must not invoke the final-screen return during the pose sequence.
                    game.step(1,'start');game.step(1);guard=state();record(guard)
                    assert guard['state']==0x27 and guard['fn']!=0x9a47,guard
                    pose_input=True
                else:game.step(5)
                assert st['frame']<limit,st
            assert pose_snapshots=={500,1400},(st,pose_snapshots)
            final=st.copy();game.snapshot('final_screen');saved=game.command('read_sram 0 8192')['hex']
            game.step(8,'start');seen=[]
            for _ in range(120):game.step(1);end=state();seen.append(end['state'])
            if mode in ('return','both'):assert any(v in (1,2,3,4) for v in seen) and end['state']!=0x27,(mode,seen)
            else:assert end['state']==0x27,(mode,end)
            assert saved==game.command('read_sram 0 8192')['hex']
            game.snapshot('after_final_start')
            result=dict(initial=initial,staff=staff,rolled=rolled,final=final,end=end,events=events,post_final_states=seen,
                        post_roll_frames=final['frame']-origin,prerequisite='Game state26 starts native ending; final battle explicitly bypassed, no credits pointer/pose/result flags written')
            (out/mode/'result.json').write_text(json.dumps(result,indent=2));return mode,result
        finally:game.close()
    with ThreadPoolExecutor(max_workers=2) as pool:results=dict(pool.map(run,('control','credits','return','both')))
    (out/'results.json').write_text(json.dumps(results,indent=2))

    validate(out,args.compare)
    print('PASS: staff-roll skip, preserved pose and final-screen return guard')

if __name__=='__main__':main()
