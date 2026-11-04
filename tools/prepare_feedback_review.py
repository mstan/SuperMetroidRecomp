#!/usr/bin/env python3
"""Prepare a small current-build review for the November room-cover feedback."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import struct
import time
from sm_progression_qa import Rom, Session

def main():
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('exe','rom','out'):p.add_argument('--'+name,type=Path,required=True)
    a=p.parse_args();out=a.out.resolve();out.mkdir(parents=True,exist_ok=False)
    (out/'states').mkdir()
    features=['super-metroid.controls/zero-mission:aim-shoulder=L,missile=off',
              'super-metroid.map-hud/laser-sight']
    game=Session(a.exe,a.rom,out/'preparation',wide=True,capture_profile=False,features=features)
    rooms=Rom(a.rom).rooms()
    manifest=dict(version=1,created=time.strftime('%Y-%m-%dT%H:%M:%S'),exe=str(a.exe.resolve()),
        rom=str(a.rom.resolve()),binary_sha256=hashlib.sha256(a.exe.read_bytes()).hexdigest(),
        aspect='Fit',mods_root=str(out/'preparation/mods'),features=features,
        setup='Current build, fresh landing, equipment/health grants, ROM door loader. Access is bypassed; no secret blocks, item bits, scroll permissions or boss victory flags are written.',
        scenarios=[])
    try:
        game.boot();game.land();game.command('savestate 0');game.step(2)
        for slot,room,door_ptr,title in (
            (1,0xacf0,0x95be,'Speed Booster Hall — right doorway and ceiling'),
            (2,0xad5e,0x9582,'Large cavern $AD5E — platforms and camera limits'),
            (3,0x91f8,None,'Aim memory and laser — Landing Site')):
            game.command('loadstate 0');game.step(3)
            game.write(0x9a2,struct.pack('<HHHH',0x7125,0x7125,0x1007,0x1007))
            for at,value in ((0x9c2,1999),(0x9c4,1999),(0x9c6,150),(0x9c8,150),
                             (0x9ca,30),(0x9cc,30),(0x9ce,20),(0x9d0,20)):
                game.word(at,value)
            if door_ptr:
                for at in (0x911,0x915,0x917,0x919,0x91d,0x91f,0x921,0x923):game.word(at,0)
                door=next(d for v in rooms.values() for d in v['doors'] if d['pointer']==door_ptr)
                assert door['destination']==room
                game.transition(door);game.step(30)
            else:
                game.step(70,'right');game.step(30)
            game.word(0x9c2,1999)
            game.command(f'savestate {slot}');game.step(2)
            state=game.snapshot(f'room-{room:04x}')
            assert state['room']==room and state['state']==8,state
            source=game.output/f'saves/save{slot}.sav'
            shutil.copyfile(source,out/f'states/save{slot}.sav')
            for hud in ((False,True) if slot<3 else (True,)):
                case=dict(id=f'SM03-{slot:02}-{int(hud)}',title=title+(' / Edges' if hud else ' / Center'),
                    room=room,slot=slot,hud=hud,state=state,camera=[game.word(0x911),game.word(0x915)],
                    state_sha256=hashlib.sha256(source.read_bytes()).hexdigest(),
                    reference='November 3 feedback; reconstructed room setup, not exact recorded inputs',
                    steps=('Move left/right and jump so the camera scrolls. Check the HUD and black covers separately. Resize the game window wide/tall; Fit follows its shape. Pause on a problem and save a note.' if slot<3 else
                           'Hold L and Down, release Down, then release/repress L. Repeat facing left and with Up. Fire while aiming; compare the laser with the cannon/shots.'),
                    expected=('HUD icons/backplates should stay at the selected screen positions. Note any moving black cover that hides useful terrain or reveals a secret. Room-edge/filler boundaries can follow the room; identify the undesirable one.' if slot<3 else
                              'L remembers your last Up/Down angle without holding it. The thicker laser starts at the displayed muzzle, follows shots and stops at walls.'))
                manifest['scenarios'].append(case)
            (out/'manifest.json').write_text(json.dumps(manifest,indent=2),encoding='utf-8')
            print(f'Prepared {room:04X}, slot {slot}',flush=True)
    finally:game.close()

if __name__=='__main__':main()
