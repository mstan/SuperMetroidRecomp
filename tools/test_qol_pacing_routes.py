#!/usr/bin/env python3
"""Measure real bidirectional Zebes elevators and shot-opened boss gates.

Room/position cheats reach the elevator; no elevator or success flags are set.
Tourian uses natural baby drain and the existing actual traversal fixture.
"""
from __future__ import annotations
import argparse,hashlib,json,re
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path
from test_qol_rom_removal import ROM_SHA,prefix,run_case,read_rows
from test_qol_tourian import scenarios as tourian_scenarios

PROBE='079B:2,0998:2,099C:2,0AF6:2,0AFA:2,0E16:2,0E18:2,0793:2,0795:2'

def scenarios():
    elevator=(prefix()+'poke 78d 428a\npoke 99c 7de1\npoke 998 0900\nuntil16 79b == 962a 3000\nuntil16 998 == 8 3000\nwait 300\n'
              'poke af6 8000\npoke b10 8000\npoke afa a800\npoke b14 a800\npress right 1\npress left 1\nwait 30\ndump ready\n'
              'press down 2\nuntil16 79b == a322 3000\nuntil16 998 == 8 3000\nuntil16 e18 == 0 3000\nwait 60\ndump arrived\n'
              'press right 1\npress left 1\nwait 30\npress up 2\nuntil16 79b == 962a 3000\nuntil16 998 == 8 3000\nuntil16 e18 == 0 3000\nwait 60\ndump returned\nquit\n')
    cases=[]
    for speed in ('control','2x','3x'):
        mods=[] if speed=='control' else ['super-metroid.pacing/elevators:speed='+speed]
        cases.append(('elevator-'+speed,mods,elevator,PROBE))
    _,_,boss,_=tourian_scenarios()[1]
    for speed in ('control','2x','max'):
        mods=['super-metroid.balance-fixes/tourian-exit']
        if speed!='control':mods+=['super-metroid.pacing/doors:speed='+speed]
        cases.append(('boss-gates-'+speed,mods,boss,PROBE))
    return cases

def observations(out):
    result={}
    def word(b,a):return int.from_bytes(b[a:a+2],'little')
    for name,*_ in scenarios():
        rows=read_rows(out/name/'wram.csv')
        if name.startswith('elevator-'):
            snapshots=[(out/name/(tag+'.wram.bin')).read_bytes() for tag in ('ready','arrived','returned')]
            assert [word(b,0x79b) for b in snapshots]==[0x962a,0xa322,0x962a]
            assert all(word(b,0x998)==8 and word(b,0xe18)==0 for b in snapshots)
            assert (out/name/'ready.sram.bin').read_bytes()==(out/name/'returned.sram.bin').read_bytes()
            ready=int(re.search(r'script f=(\d+) dump ready', (out/name/'stderr.log').read_text()).group(1))
            trips=[];started=None
            for row in rows:
                if row['frame']<=ready:continue
                if started is None and row['0e18']==1:started=row['frame']
                elif started is not None and row['0e18']==0 and row['0998']==8:
                    trips.append({'frames':row['frame']-started,'destination':hex(row['079b'])});started=None
            assert len(trips)==2,(name,trips)
            result[name]=trips
        else:
            for tag,target in [('big_exit',0xdc65),('dust_exit',0xdc19),('big_reentry',0xdcb1)]:
                assert word((out/name/(tag+'.wram.bin')).read_bytes(),0x79b)==target
            transitions=[];started=None
            for row in rows:
                if row['0998']==9 and started is None:started=row['frame']
                elif row['0998']==8 and started is not None:
                    transitions.append({'frames':row['frame']-started,'destination':hex(row['079b'])});started=None
            assert [x['destination'] for x in transitions]==['0xdc65','0xdc19'],(name,transitions)
            result[name]=transitions
    for direction in range(2):
        assert result['elevator-control'][direction]['frames']>result['elevator-2x'][direction]['frames']>result['elevator-3x'][direction]['frames']
        assert result['boss-gates-control'][direction]['frames']>result['boss-gates-2x'][direction]['frames']>result['boss-gates-max'][direction]['frames']
    return result

def main():
    p=argparse.ArgumentParser(description=__doc__)
    for key in ('build','rom','out'):p.add_argument('--'+key,type=Path,required=True)
    p.add_argument('--interp-targets',type=Path);p.add_argument('--compare',type=Path)
    a=p.parse_args();a.out=a.out.resolve();a.out.mkdir(parents=True,exist_ok=False)
    assert hashlib.sha256(a.rom.read_bytes()).hexdigest()==ROM_SHA
    def run(c):return run_case(c,build=a.build.resolve(),rom=a.rom.resolve(),out=a.out,interp_targets=a.interp_targets)
    with ThreadPoolExecutor(max_workers=2) as pool:ids=dict(pool.map(run,scenarios()))
    result={'observations':observations(a.out),'identities':ids}
    if a.compare:
        old=json.loads((a.compare/'results.json').read_text())
        for name in ids:
            assert read_rows(a.out/name/'wram.csv')==read_rows(a.compare/name/'wram.csv'),name
            for file in (a.out/name).glob('*.wram.bin'):assert file.read_bytes()==(a.compare/name/file.name).read_bytes(),(name,file.name)
        assert sum(x['execution_share']['interp_insns'] for x in ids.values())>sum(x['execution_share']['interp_insns'] for x in old['identities'].values())
        result['comparison']='All observed frames and full WRAM snapshots identical; actual interpreter work increases'
    (a.out/'results.json').write_text(json.dumps(result,indent=2));print(json.dumps(result['observations'],indent=2))

if __name__=='__main__':main()
