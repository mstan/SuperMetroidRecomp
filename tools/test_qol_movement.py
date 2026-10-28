#!/usr/bin/env python3
"""Actual-game checks for the original movement toggles, with isolated saves."""
from __future__ import annotations
import argparse,hashlib,json
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path
from test_qol_rom_removal import ROM_SHA,prefix,run_case,read_rows

PROBE='079B:2,0998:2,0A1C:2,0A1F:1,0AF6:2,0AFA:2,0B2C:2,0B2E:2,0B36:2,0B42:2,0B44:2,0B46:2,0B48:2,0A56:2,0B3A:2,0A68:2,0B3E:2,0C86:2'

def scenarios():
    base=prefix()+'press right 70\nwait 30\n'
    scripts={
        'respin':base+'press a 18\nwait 16\ndump before\npress right+a 1\nwait 1\ndump after\nwait 80\ndump final\nquit\n',
        'space':base+'poke 9a2 0002\npoke 9a4 0002\npress right 12\npress right+a 8\npress right 1\ndump before\npress right+a 1\nwait 1\ndump after\nwait 80\ndump final\nquit\n',
        'momentum':prefix()+'press right+b 100\nwait 20\ndump final\nquit\n',
        'ball':base+'press left 40\nwait 10\npoke 9a2 0420\npoke 9a4 0420\npress down 2\nwait 20\npress down 2\nwait 30\ndump before\npress right+b 80\ndump after\nwait 10\ndump final\nquit\n',
        'morph':base+'poke 9a2 0400\npoke 9a4 0400\ndump before\npress down 30\ndump after\nwait 60\ndump final\nquit\n',
        'bomb':base+'poke 9a2 0410\npoke 9a4 0410\npress down 2\nwait 20\npress down 2\nwait 30\ndump before\npress down+x 2\npress down 90\ndump after\nwait 30\npress x 2\nwait 100\ndump final\nquit\n',
        # Boost is a setup cheat. Both actual crouches, the expired timer and
        # subsequent spark input run normally; the stored-result flag is not set.
        'store':base+'poke b3e 0004\npress down 2\nwait 30\npress up 2\nwait 20\npress down 2\nwait 20\npress up 2\nwait 400\ndump before\npress a 2\nwait 120\ndump final\nquit\n',
    }
    features={'respin':'respin','space':'space-jump','momentum':'landing-momentum','ball':'speed-ball','morph':'quick-morph','bomb':'bomb-control','store':'store-shinespark'}
    cases=[(name+'-'+mode,[] if mode=='control' else ['super-metroid.movement/'+features[name]],script,PROBE) for name,script in scripts.items() for mode in ('control','mod')]
    cases.append(('space-anytime',['super-metroid.movement/space-jump:window=anytime'],scripts['space'],PROBE))
    return cases

def validate(out):
    def ram(name,tag='final'):return (out/name/(tag+'.wram.bin')).read_bytes()
    def w(b,a):return int.from_bytes(b[a:a+2],'little')
    obs={}
    for name,*_ in scenarios():
        b=ram(name);assert w(b,0x998)==8 and w(b,0x79b)==0x91f8,name
        obs[name]={'x':w(b,0xaf6),'y':w(b,0xafa),'pose':w(b,0xa1c)}
    a,b=ram('respin-control','after'),ram('respin-mod','after')
    assert a[0xa1f]==2 and b[0xa1f]==3
    assert a[0xafa:0xafc]==b[0xafa:0xafc] and a[0xb2c:0xb30]==b[0xb2c:0xb30]
    assert a[0xb36:0xb38]==b[0xb36:0xb38]==b'\x02\x00'
    for name in ('space-mod','space-anytime'):
        before=ram(name,'before');after=ram(name,'after')
        assert before[0xa1f]==3 and w(before,0xb36)==2
        assert w(before,0xb2e)==0,'fixture must be outside the stock fall-speed window'
        assert w(after,0xafa)<w(ram('space-control','after'),0xafa)
    assert w(ram('momentum-mod'),0xaf6)-w(ram('momentum-control'),0xaf6)==47
    for name,distance in [('control',235),('mod',441)]:
        before,after=ram('ball-'+name,'before'),ram('ball-'+name,'after')
        assert w(after,0xaf6)-w(before,0xaf6)==distance
    assert ram('morph-control','after')[0xa1f]==5 and ram('morph-mod','after')[0xa1f]==4
    assert w(ram('bomb-control','after'),0xafa)<w(ram('bomb-control','before'),0xafa)
    assert w(ram('bomb-mod','after'),0xafa)==w(ram('bomb-mod','before'),0xafa)
    assert ram('bomb-mod')[0xa1f]==8,'normal bomb jumping after releasing Down must still work'
    assert w(ram('store-control','before'),0xb3a)==0 and w(ram('store-mod','before'),0xb3a)==1
    assert w(ram('store-mod','before'),0xa68)&0x8000,'stored spark must outlive the normal timer'
    assert w(ram('store-mod'),0xb3a)==0 and w(ram('store-mod'),0xa1c)==0xcb
    return obs

def main():
    p=argparse.ArgumentParser(description=__doc__)
    for key in ('build','rom','out'):p.add_argument('--'+key,type=Path,required=True)
    p.add_argument('--interp-targets',type=Path);p.add_argument('--compare',type=Path)
    a=p.parse_args();a.out=a.out.resolve();a.out.mkdir(parents=True,exist_ok=False)
    assert hashlib.sha256(a.rom.read_bytes()).hexdigest()==ROM_SHA
    def run(c):return run_case(c,build=a.build.resolve(),rom=a.rom.resolve(),out=a.out,interp_targets=a.interp_targets)
    with ThreadPoolExecutor(max_workers=2) as pool:ids=dict(pool.map(run,scenarios()))
    result={'observations':validate(a.out),'identities':ids}
    if a.compare:
        previous=json.loads((a.compare/'results.json').read_text())
        for name in ids:
            assert read_rows(a.out/name/'wram.csv')==read_rows(a.compare/name/'wram.csv'),name
            for file in (a.out/name).glob('*.wram.bin'):assert file.read_bytes()==(a.compare/name/file.name).read_bytes(),(name,file.name)
        assert sum(v['execution_share']['interp_insns'] for v in ids.values())>sum(v['execution_share']['interp_insns'] for v in previous['identities'].values())
        result['comparison']='Every observed frame and full WRAM snapshot identical; actual interpreter work increases'
    (a.out/'results.json').write_text(json.dumps(result,indent=2));print(json.dumps(result['observations'],indent=2))

if __name__=='__main__':main()
