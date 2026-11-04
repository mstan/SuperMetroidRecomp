#!/usr/bin/env python3
"""Qualify optional spark controls and air morph in muted isolated games."""
from __future__ import annotations
import argparse,json,hashlib,os,shutil
from pathlib import Path
from concurrent.futures import ThreadPoolExecutor
from test_qol_rom_removal import ROM_SHA,prefix,run_case,read_rows

PROBE='079B:2,0998:2,0A1C:2,0A1F:1,0A28:2,0A2C:2,0A58:2,0A60:2,0AF6:2,0AFA:2,0B2C:2,0B2E:2,0B36:2,0B3E:2,0A6E:2,008B:2,008F:2,09D2:2'
STEER='super-metroid.movement/shinespark-steering'
QUICK='super-metroid.movement/quick-morph'
ZM='super-metroid.controls/zero-mission'

def spark(direction,button,morph=True):
    select={'h':'press right+a 6\n','v':'wait 1\n','d':'press a+r 6\n'}[direction]
    pose={'h':0xc9,'v':0xcb,'d':0xcd}[direction]
    return (prefix()+'press right 70\nwait 30\n'+
            ('poke 9a2 0400\npoke 9a4 0400\n' if morph else '')+
            'poke a68 b400\npoke acc 0100\npress a 2\n'+select+
            f'until16 a1c == {pose:x} 200\nwait 2\ndump before\npress {button} 1\nwait 2\ndump after\nwait 100\ndump settled\nquit\n')

def air(kind,custom=False):
    move={'jump':'press a 6\nwait 8\n','spin':'press right+a 6\nwait 8\n',
          'left':'press left+a 6\nwait 8\n','fall':'press a 18\nwait 16\n'}[kind]
    return (prefix()+'press right 70\nwait 30\npoke 9a2 0400\npoke 9a4 0400\n'+
            ('poke 9b8 1000\n' if custom else '')+move+
            'dump before\npress l+down 1\nwait 8\ndump after\nwait 100\ndump settled\nquit\n')

def cases():
    result=[]
    for direction in ('h','v','d'):
        for button,option in [('down','down-to-morph'),('a','jump-to-cancel')]:
            result.append((f'spark-{direction}-{option}',[STEER+':'+option+'=true'],spark(direction,button),PROBE))
        result.append((f'spark-{direction}-down-off',[STEER],spark(direction,'down'),PROBE))
    result += [('spark-h-jump-off',[STEER],spark('h','a'),PROBE),
               ('spark-h-no-morph',[STEER+':down-to-morph=true'],spark('h','down',False),PROBE)]
    for kind in ('jump','spin','left','fall'):
        for enabled in (False,True):
            result.append((f'air-{kind}-'+('on' if enabled else 'off'),[QUICK+':air-morph='+str(enabled).lower()],air(kind),PROBE))
    result.append(('air-shoulder-remap',[QUICK+':air-morph=true',ZM+':aim-shoulder=L'],air('jump',True),PROBE))
    return result

def validate(out):
    observations={}
    for case in cases():
        name=case[0]; root=out/name
        def ram(tag): return (root/(tag+'.wram.bin')).read_bytes()
        def word(data,addr): return int.from_bytes(data[addr:addr+2],'little')
        before,after,end=map(ram,('before','after','settled'))
        assert word(end,0x998)==8 and word(end,0x79b)==0x91f8
        assert (root/'before.sram.bin').read_bytes()==(root/'settled.sram.bin').read_bytes()
        if name.startswith('spark-'):
            assert word(before,0xa1c) in (0xc9,0xcb,0xcd), (name,hex(word(before,0xa1c)))
            changed=name.endswith('to-morph') or name.endswith('to-cancel')
            if changed:
                assert after[0xa1f] in ((8,0x13) if name.endswith('to-morph') else (6,)), (name,after[0xa1f],hex(word(after,0xa1c)))
                assert word(after,0xa60)==0xe913 and not word(after,0xb3e) and not word(after,0xa6e)
                assert word(after,0xb36)==2
            else: assert word(after,0xa1c) in (0xc9,0xcb,0xcd),name
        else:
            assert before[0xa1f] in (2,3,6), (name,before[0xa1f])
            changed=not name.endswith('-off')
            assert (after[0xa1f] in (4,8,0x11,0x12,0x13))==changed, (name,after[0xa1f],hex(word(after,0xa1c)))
            if changed: assert word(after,0x9d2)==0, 'air chord armed a weapon'
        observations[name]={'before_pose':word(before,0xa1c),'after_pose':word(after,0xa1c),
                            'after_movement':after[0xa1f],'after_handler':word(after,0xa58),
                            'native_sram_unchanged':True}
    return observations

def main():
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('build','rom','out'):p.add_argument('--'+name,type=Path,required=True)
    p.add_argument('--interp-targets',type=Path);p.add_argument('--compare',type=Path)
    p.add_argument('--reuse',type=Path,help='reuse unchanged cases only when binary, scenario, features and tier targets match')
    a=p.parse_args();a.out=a.out.resolve();a.out.mkdir(parents=True,exist_ok=False)
    assert hashlib.sha256(a.rom.read_bytes()).hexdigest()==ROM_SHA
    binary=a.build/('SuperMetroidSNESRecomp.exe' if os.name=='nt' else 'SuperMetroidSNESRecomp')
    binary_sha=hashlib.sha256(binary.read_bytes()).hexdigest()
    def run(case):
        if a.reuse:
            old=a.reuse.resolve()/case[0]; identity=old/'identity.json'
            if identity.exists():
                info=json.loads(identity.read_text())
                target=str(a.interp_targets) if a.interp_targets else None
                if (info['exe_sha256']==binary_sha and info['features']==case[1] and
                    info['interp_targets']==target and (old/'scenario.txt').read_text()==case[2]):
                    shutil.copytree(old,a.out/case[0]);info['reused_from']=str(old)
                    return case[0],info
        return run_case(case,build=a.build.resolve(),rom=a.rom.resolve(),out=a.out,interp_targets=a.interp_targets)
    with ThreadPoolExecutor(max_workers=2) as pool:identities=dict(pool.map(run,cases()))
    result={'observations':validate(a.out),'identities':identities}
    if a.compare:
        original=json.loads((a.compare/'results.json').read_text())
        for name in identities:
            assert read_rows(a.out/name/'wram.csv')==read_rows(a.compare/name/'wram.csv'),name
            for path in (a.out/name).glob('*.wram.bin'):
                assert path.read_bytes()==(a.compare/name/path.name).read_bytes(),(name,path.name)
        assert sum(x['execution_share']['interp_insns'] for x in identities.values())>sum(x['execution_share']['interp_insns'] for x in original['identities'].values())
        result['comparison']='every observed frame and full WRAM snapshot identical; actual interpreter work increases'
    (a.out/'results.json').write_text(json.dumps(result,indent=2));print(json.dumps(result['observations'],indent=2))

if __name__=='__main__':main()
