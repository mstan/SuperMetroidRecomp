#!/usr/bin/env python3
"""Qualify combat hooks with actual muted gameplay and isolated saves.

Equipment, position and constructed block/frozen-enemy prerequisites are cheats;
collisions, block reactions, projectiles, pickups and ammo use run normally.
The magnet control also enables screw attack to produce the same genuine drop.
"""
from __future__ import annotations
import argparse,hashlib,json
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path
from test_qol_rom_removal import ROM_SHA,prefix,read_rows,run_case

PROBE='079B:2,0998:2,0AF6:2,0AFA:2,0A6E:2,0CCE:2,0CD2:2,0CD0:2,0FB8:2,0FCC:2,0FDE:2,09CE:2,09C2:2,1441A:2,14BF8:2,14BFA:2,189FD:1,189FE:1'
COMBAT='super-metroid.combat/'

def scenarios():
    base=prefix()
    wall='poke 9a2 0800\npoke 9a4 0800\n'
    for tile,bts in [(0x1441a,0x1860e),(0x1453a,0x1869e),(0x1465a,0x1872e),(0x1477a,0x187be),(0x1489a,0x1884e),(0x149ba,0x188de),(0x14ada,0x1896e),(0x14bfa,0x189fe),(0x14d1a,0x18a8e)]:
        wall+=f'poke {tile:x} 52c0\npoke {bts:x} '+('00' if tile<0x149ba else '04')+'\n'
    wall+='dump before\nwait 2\npress right+a 50\npress right 60\nwait 400\ndump final\nquit\n'
    enemy=('poke d820 01\npoke 79f 0100\npoke 78b 0900\npoke d914 0500\npoke 998 0600\n'
           'until16 998 == 8 3000\nwait 200\npoke 9a2 0800\npoke 9a4 0800\n')
    frozen=enemy+'press left 20\nwait 30\npokefor fba 4005 40\npokefor fbe a002 40\npokefor fde 0004 40\nwait 2\npress left+a 3\npress left 60\nwait 200\ndump final\nquit\n'
    magnet=(enemy+'poke 9a6 0010\npoke 9a8 0010\npoke 9c2 0a00\npress left 20\nwait 30\n'
            'pokefor fba 4005 40\npokefor fbe a002 40\npokefor fde 0004 40\nwait 2\npress left+a 3\npress left 25\n'
            'press right 50\nwait 10\ndump before_charge\npress x 10\ndump charging_10\npress x 10\ndump charging_20\npress x 130\nwait 120\ndump final\nquit\n')
    pb=('poke 9a2 0400\npoke 9a4 0400\npoke 9ce 0100\npoke 9d0 0100\npoke 14bf8 52c0\npoke 189fd 0a\npoke 14bfa 52c0\npoke 189fe 0b\n'
        'press right 30\nwait 20\npress down 2\nwait 20\npress down 2\nwait 40\npress select 2\nwait 10\ndump before\npress x 2\nwait 400\ndump final\nquit\n')
    bomb=('poke 9a2 0410\npoke 9a4 0410\npress right 30\nwait 20\npress down 2\nwait 20\npress down 2\nwait 40\n'+
          'press x 1\nwait 1\n'*64+'wait 200\ndump final\nquit\n')
    result=[]
    for name,script,feature in [('shot-blocks',wall,COMBAT+'screw-attack'),('frozen',frozen,COMBAT+'screw-attack'),('magnet',magnet,COMBAT+'item-magnet'),('pb-reveal',pb,'super-metroid.balance-fixes/power-bomb-reveal')]:
        for enabled in (False,True):
            mods=[COMBAT+'screw-attack'] if name=='magnet' else []
            if enabled:mods.append(feature)
            result.append((name+('-mod' if enabled else '-control'),mods,base+script,PROBE))
    for name,mods in [('control',[]),('ten',[COMBAT+'bomb-limit:bombs=10,interval=2']),('six',[COMBAT+'bomb-limit:bombs=6,interval=2'])]:
        result.append(('bomb-'+name,mods,base+bomb,PROBE))
    return result

def validate(out):
    obs={}
    def ram(case,tag='final'):return (out/case/(tag+'.wram.bin')).read_bytes()
    def w(b,a):return int.from_bytes(b[a:a+2],'little')
    for name,*_ in scenarios():
        end=ram(name);assert w(end,0x998)==8,name
        obs[name]={'x':w(end,0xaf6),'health':w(end,0x9c2)}
    assert w(ram('shot-blocks-control'),0xaf6)==0x4bb
    assert w(ram('shot-blocks-mod'),0xaf6)==0x519
    for name in ('shot-blocks-control','shot-blocks-mod'):
        assert w(ram(name),0x14bfa)==0xc052,'respawning block did not return'
    assert w(ram('frozen-control'),0xfb8)!=0 and w(ram('frozen-control'),0xfde)!=0
    assert w(ram('frozen-mod'),0xfb8)==w(ram('frozen-mod'),0xfcc)==w(ram('frozen-mod'),0xfde)==0
    assert w(ram('magnet-control'),0x9c2)==10 and w(ram('magnet-mod'),0x9c2)==30
    # Same natural drop exists before charging. Only the magnet moves it;
    # the normal-strength diagonal pull advances about 2 pixels per frame.
    for mode in ('control','mod'):
        name='magnet-'+mode;before=ram(name,'before_charge')
        slots=[k for k in range(0,36,2) if w(before,0x1997+k) and w(before,0x1a03+k)==0xefe0]
        assert len(slots)==1,(name,slots)
        k=slots[0];a=ram(name,'charging_10');b=ram(name,'charging_20')
        step=w(b,0x1a4b+k)-w(a,0x1a4b+k)
        assert step==(20 if mode=='mod' else 0),(name,step)
        assert (w(ram(name),0x1997+k)==0)==(mode=='mod')
        obs[name]['drop_x_advance_over_ten_frames']=step
        assert (out/name/'before_charge.sram.bin').read_bytes()==(out/name/'final.sram.bin').read_bytes()
    for name,peak in [('control',4),('ten',10),('six',6)]:
        rows=read_rows(out/('bomb-'+name)/'wram.csv')
        assert max(r['0cd2'] for r in rows)==peak
        assert w(ram('bomb-'+name),0xcd2)==w(ram('bomb-'+name),0xcce)==0
        obs['bomb-'+name]['peak_bombs']=peak
    for name,revealed in [('control',False),('mod',True)]:
        b=ram('pb-reveal-'+name);before=ram('pb-reveal-'+name,'before')
        assert w(before,0x9ce)==1 and w(b,0x9ce)==0,'real PB was not consumed'
        assert w(before,0x14bfa)==0xc052
        assert w(b,0x14bfa)==(0xc09f if revealed else 0xc052)
        obs['pb-reveal-'+name]['final_block']=hex(w(b,0x14bfa))
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
        result['comparison']='All observed frames and full WRAM snapshots identical; actual interpreter work increases'
    (a.out/'results.json').write_text(json.dumps(result,indent=2));print(json.dumps(result['observations'],indent=2))

if __name__=='__main__':main()
