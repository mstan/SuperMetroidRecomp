#!/usr/bin/env python3
"""Actual pause fades and auto-charge arm animation, muted/private/headless.

Baseline records the two owner-reported presentation defects. The candidate
must replace every visible native map-fade header and remove only moving idle
charge flares, retaining charge/palette/audio and native held-fire rendering.
"""
from pathlib import Path
import argparse,hashlib,json,struct
from sm_progression_qa import Rom,Session

def word(data,a):return int.from_bytes(data[a:a+2],'little')
def bitmap(path):
    data=path.read_bytes();offset=struct.unpack_from('<I',data,10)[0]
    width,height=struct.unpack_from('<ii',data,18);bits=struct.unpack_from('<H',data,28)[0]
    assert bits in (24,32) and width==256
    pitch=((width*bits+31)//32)*4;rows=[]
    for y in range(abs(height)):
        pos=offset+(abs(height)-1-y if height>0 else y)*pitch
        rows.append([tuple(data[pos+x*bits//8:pos+x*bits//8+3]) for x in range(width)])
    return rows

def map_case(a):
    game=Session(a.exe,a.rom,a.out/'map',capture_profile=False,
        features=['super-metroid.map-hud/pause-map','super-metroid.map-hud/world-map'],
        interpret_targets=(0x8290c8,0x8290e8,0x829324,0xa08eb6) if a.interp else ())
    rows=[]
    try:
        game.boot();game.land();sram=game.command('read_sram 0 8192')['hex']
        def phase(name,buttons,frames):
            for tick in range(frames):
                game.step(1,buttons if tick<8 else '0')
                ram=game.read(0,0xe00)
                state=word(ram,0x998);mode=word(ram,0x763);level=0 if ram[0x51]&128 else ram[0x51]&15
                row=dict(phase=name,tick=tick,state=state,menu=word(ram,0x727),mode=mode,level=level,
                         frame=game.command('frame')['frame'])
                if 14<=state<=16 and mode==0:
                    file=game.output/f'{name}-{tick:03}.bmp'
                    game.command('screenshot '+file.as_posix());row['image']=file.name
                rows.append(row)
            game.snapshot(name)
        phase('open','start',80)
        assert game.word(0x998)==15 and game.word(0x727)==0
        reference=bitmap(game.output/'open.bmp')[:36]
        phase('to-equipment','r',90)
        assert game.word(0x727)==1
        phase('to-map','l',90)
        assert game.word(0x727)==0
        phase('close','start',80)
        assert game.word(0x998)==8
        phase('reopen','start',80)
        assert game.word(0x998)==15
        game.step(1,'x');game.step(2);game.snapshot('world-reference')
        world_reference=bitmap(game.output/'world-reference.bmp')[:36]
        phase('close-world','start',80)
        assert game.word(0x998)==8
        assert sram==game.command('read_sram 0 8192')['hex']
        bad=[];coverage={}
        for row in rows:
            if 'image' not in row:continue
            header=bitmap(game.output/row['image'])[:36]
            page=world_reference if row['phase']=='close-world' else reference
            expected=[[tuple(c*row['level']//15 for c in pixel) for pixel in line] for line in page]
            key=row['phase'];coverage[key]=coverage.get(key,0)+1
            if header!=expected:bad.append({k:row[k] for k in ('phase','tick','state','menu','level','image')})
        assert set(coverage)=={'open','to-equipment','to-map','close','reopen','close-world'},coverage
        if not a.baseline:assert not bad,bad[:6]
        else:assert any(x['phase']=='open' for x in bad) and any(x['phase']=='close' for x in bad),bad
        result=dict(rows=rows,coverage=coverage,native_map_flash_frames=bad,SRAM_unchanged=True)
        (game.output/'results.json').write_text(json.dumps(result,indent=2)+'\n')
        return result
    finally:game.close()

def flare_tiles(rom):
    tiles=set()
    for i in range(66):
        at=0x930000|rom.word(0x93a1a1+2*i)
        count=rom.word(at);assert count<128
        for j in range(count):tiles.add(rom.word(at+2+5*j+3)&0x3fff)
    return tiles

def charge_case(a):
    tiles=flare_tiles(Rom(a.rom))
    game=Session(a.exe,a.rom,a.out/'charge',capture_profile=False,
        features=['super-metroid.combat/auto-charge'],
        interpret_targets=(0x90b80d,0x90bafc,0x90bbe1,0xa08eb6) if a.interp else ())
    rows=[]
    try:
        game.boot();game.land();sram=game.command('read_sram 0 8192')['hex']
        game.word(0x9a6,0x1000);game.word(0x9a8,0x1000)
        game.step(70,'right');game.step(150)
        def phase(name,buttons,frames):
            for tick in range(frames):
                game.step(1,buttons)
                ram=game.read(0,0xe00)
                count=sum(1 for i in range(128) if ram[0x370+4*i+1]<224 and
                          word(ram,0x370+4*i+2)&0x3fff in tiles)
                rows.append(dict(phase=name,tick=tick,pose=word(ram,0xa1c),movement=ram[0xa1f],
                    charge=word(ram,0xcd0),held=word(ram,0x8b),shoot=word(ram,0x9b2),
                    x=word(ram,0xaf6),y=word(ram,0xafa),flare_sprites=count,
                    flare_timers=ram[0xcd6:0xce2].hex(),sound=ram[0x600:0x680].hex(),
                    projectiles=ram[0xc18:0xc22].hex()))
            game.snapshot(name)
        phase('idle-right','0',8)
        phase('run-right','right',18)
        phase('jump-right','right+a',12)
        game.step(90);phase('idle-landed','0',8)
        phase('run-left','left',18)
        game.step(45);phase('idle-left','0',8)
        phase('held-fire-run','left+x',90)
        idle=[x for x in rows if x['phase'].startswith('idle')]
        moving=[x for x in rows if x['movement']!=0 and not x['held']&x['shoot'] and x['charge']>=60]
        held=[x for x in rows if x['phase']=='held-fire-run' and x['charge']>=60]
        assert idle and moving and held
        assert all(x['charge']==120 and x['flare_sprites']>0 for x in idle),idle
        assert all(x['flare_sprites']>0 for x in held),held[:4]
        assert all(x['projectiles']=='00'*10 for x in moving),moving[:4]
        if not a.baseline:assert not any(x['flare_sprites'] for x in moving),moving[:4]
        else:assert any(x['flare_sprites'] for x in moving),'Baseline did not reproduce detached flare'
        assert sram==game.command('read_sram 0 8192')['hex']
        result=dict(rows=rows,moving_idle_samples=len(moving),held_fire_samples=len(held),
                    flare_tile_attributes=sorted(tiles),SRAM_unchanged=True)
        (game.output/'results.json').write_text(json.dumps(result,indent=2)+'\n')
        return result
    finally:game.close()

def main():
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('exe','rom','out'):p.add_argument('--'+name,type=Path,required=True)
    p.add_argument('--baseline',action='store_true');p.add_argument('--interp',action='store_true')
    p.add_argument('--compare',type=Path);p.add_argument('--mechanics-baseline',type=Path)
    a=p.parse_args();a.out=a.out.resolve();a.out.mkdir(parents=True,exist_ok=False)
    results={name:fn(a) for name,fn in (('map',map_case),('charge',charge_case))}
    if a.compare:
        for name in results:
            assert results[name]==json.loads((a.compare/name/'results.json').read_text()),name
            for file in list((a.out/name).glob('*.wram'))+list((a.out/name).glob('*.bmp')):
                assert file.read_bytes()==(a.compare/name/file.name).read_bytes(),(name,file.name)
            now=json.loads((a.out/name/'execution.json').read_text())['interp_stats']['interp_insns']
            before=json.loads((a.compare/name/'execution.json').read_text())['interp_stats']['interp_insns']
            assert now>before,(name,now,before)
    if a.mechanics_baseline:
        prior=json.loads((a.mechanics_baseline/'charge/results.json').read_text())
        strip=lambda row:{k:v for k,v in row.items() if k!='flare_sprites'}
        assert list(map(strip,results['charge']['rows']))==list(map(strip,prior['rows'])),'Charge/pose/timers/sound/shots changed'
        for file in (a.out/'map').glob('*.wram'):
            assert file.read_bytes()==(a.mechanics_baseline/'map'/file.name).read_bytes(),file.name
    result=dict(binary_sha256=hashlib.sha256(a.exe.read_bytes()).hexdigest(),
                map_fade_coverage=results['map']['coverage'],map_flash_frames=len(results['map']['native_map_flash_frames']),
                moving_charge_samples=results['charge']['moving_idle_samples'],
                held_fire_samples=results['charge']['held_fire_samples'],baseline=a.baseline)
    (a.out/'results.json').write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps(result,indent=2))

if __name__=='__main__':main()
