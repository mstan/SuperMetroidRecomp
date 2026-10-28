#!/usr/bin/env python3
"""Prepare six disposable, current-build states for owner replay review."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import struct
import time

from sm_progression_qa import Rom, Session

SCENARIOS = [
    dict(id='SM27-01', title='Plasma Spark - upper shaft', room=0xd340, door=0xa5d0,
         clip='731,658', items=0x7125, beams=0x1007, health=599, max_health=599, missiles=105, supers=10, bombs=15, hud=False,
         steps='Move left from the upper platforms and drop down the shaft. Move back up if useful. Look at both edges while the camera moves.',
         expected='Terrain, background, Samus and sound should remain coherent. Point out anything that looks or sounds wrong; the black edge cutouts are an open question.'),
    dict(id='SM27-02', title='Morph Ball eye - light cone', room=0x9e9f, door=0x8ec2,
         clip='528,702', items=4, beams=0, health=99, max_health=99, missiles=5, supers=0, bombs=0, hud=True,
         steps='Walk left and right near the eye. Let its light cone activate, rotate and switch off. Try Morph Ball if useful.',
         expected='Inspect the cone, moving camera, Samus, HUD and sound. No specific fault has been confirmed.'),
    dict(id='SM27-03', title='Cathedral - jumps above lava', room=0xa788, door=0x9732,
         clip='650,331', items=0x1105, beams=0x1004, health=345, max_health=399, missiles=34, supers=5, bombs=0, hud=True,
         steps='Jump across the platforms above lava and move the camera in both directions. Check terrain, background, lava, HUD and sound.',
         expected='Layers and effects should stay aligned through jumps and scrolling. Describe the exact place and movement if something breaks.'),
    dict(id='SM27-04', title='Wave Beam / Double Chamber - doors and lava run', room=0xadde, door=0x961e,
         clip='682,203', items=0x3105, beams=0x1007, health=399, max_health=399, missiles=60, supers=5, bombs=0, hud=False,
         steps='Exit left from the item room, run left through Double Chamber, then return. Hold Run for the speed boost. Inspect both door transitions and the lava run.',
         expected='Check for extra doors, stale room pieces, lava seams or sound problems. Black transition phases alone can be normal.'),
    dict(id='SM27-05', title='Crocomire - boss and lava', room=0xa98d, door=0x93d2,
         clip='699,506', items=0x3105, beams=0x1007, health=599, max_health=599, missiles=65, supers=5, bombs=0, hud=False,
         steps='Move around the boss, fire at it, and move the camera left/right. Inspect the floor and lava near the old 4:3 edges, the boss body, and sound.',
         expected='Point out any floor/lava discontinuity, repeated body, missing effect or sound issue. The suspected seam has not been reproduced.'),
    dict(id='SM27-06', title='Bubble Mountain - jump and drop', room=0xacb3, door=0x97ce,
         clip='666,141', items=0x1105, beams=0x1004, health=300, max_health=399, missiles=34, supers=5, bombs=0, hud=True,
         steps='Jump from the middle platforms, drop to the lower floor, and move around. Inspect the scrolling, Samus, HUD and sound.',
         expected='No specific fault has been confirmed. Please describe anything wrong and what you did immediately before it.'),
]

def prepare(exe, rom_path, output, seed=None):
    output.mkdir(parents=True, exist_ok=False)
    (output/'states').mkdir()
    game=Session(exe,rom_path,output/'preparation',wide=True,capture_profile=False)
    rom=Rom(rom_path)
    manifest=dict(version=1,created=time.strftime('%Y-%m-%dT%H:%M:%S'),
                  exe=str(exe),rom=str(rom_path),binary_sha256=hashlib.sha256(exe.read_bytes()).hexdigest(),
                  setup='Fresh boot/landing, equipment grants and ROM door loader. Plasma Spark also uses diagnostic position placement followed by normal camera settling. Diagnostic access, not unassisted progression or exact video input playback. No boss victory flags set.',scenarios=[])
    try:
        if seed:
            shutil.copyfile(seed,game.output/'saves/save0.sav')
            game.command('loadstate 0');game.step(3)
            assert game.state()['state']==8
        else:
            game.boot();game.land()
        game.command('savestate 0');game.step(2)
        for slot,scenario in enumerate(SCENARIOS,1):
            game.command('loadstate 0');game.step(3)
            game.write(0x9a2,struct.pack('<HHHH',scenario['items'],scenario['items'],scenario['beams'],scenario['beams']))
            for address,value in [(0x9c2,scenario['health']),(0x9c4,scenario['max_health']),
                (0x9c6,scenario['missiles']),(0x9c8,scenario['missiles']),
                (0x9ca,scenario['supers']),(0x9cc,scenario['supers']),
                (0x9ce,scenario['bombs']),(0x9d0,scenario['bombs'])]:game.word(address,value)
            # A synthetic outgoing doorway needs a coherent local camera.
            # Leaving the ship's old camera while positioning Samus at x16
            # otherwise seeds a misaligned ring-buffer offset in the loader.
            for address in (0x911,0x915,0x917,0x919,0x91d,0x91f,0x921,0x923):game.word(address,0)
            a=0x830000|scenario['door']
            door=dict(pointer=a&65535,destination=rom.word(a),flags=rom.byte(a+2),direction=rom.byte(a+3),
                      cap_x=rom.byte(a+4),cap_y=rom.byte(a+5),screen_x=rom.byte(a+6),screen_y=rom.byte(a+7))
            assert door['destination']==scenario['room']
            transition=game.transition(door)
            if slot==1:
                for address,value in ((0xaf6,675),(0xb10,675),(0xafa,120),(0xb14,120)):
                    game.word(address,value)
                game.step(60)
            elif slot==2:
                for _ in range(80):
                    if game.word(0xaf6)<=1100:break
                    game.word(0x9c2,99);game.step(20,'left,a,x');game.step(2,'left,x')
                game.step(60)
            elif slot==3:
                game.step(50);game.step(32,'left,a');game.step(4)
            elif slot==4:
                game.step(10,'right');game.step(3)
            elif slot==5:
                game.step(100,'right,x');game.step(30)
            elif slot==6:
                game.step(40);game.step(38,'right,a');game.step(3)
            game.word(0x9c2,scenario['health'])
            game.command(f'savestate {slot}');game.step(2)
            state=game.snapshot(scenario['id'])
            assert state['room']==scenario['room'],state
            path=game.output/f'saves/save{slot}.sav'
            shutil.copyfile(path,output/f'states/save{slot}.sav')
            case=dict(scenario,slot=slot,state=state,transition=transition,
                      camera=[game.word(0x911),game.word(0x915)],state_sha256=hashlib.sha256(path.read_bytes()).hexdigest())
            manifest['scenarios'].append(case)
            (output/'manifest.json').write_text(json.dumps(manifest,indent=2),encoding='utf-8')
            print(json.dumps(dict(id=scenario['id'],room=hex(state['room']),x=state['x'],y=state['y'],camera=case['camera'])),flush=True)
        # Reload each actual guarded slot and prove simulation accepts input.
        manifest['reload_checks']=[]
        for scenario in manifest['scenarios']:
            game.command(f"loadstate {scenario['slot']}");game.step(3)
            before=game.state();game.step(8,'right');after=game.state()
            assert before['room']==scenario['room'] and after['state']==8,(before,after)
            manifest['reload_checks'].append(dict(id=scenario['id'],before=before,after=after))
        (output/'manifest.json').write_text(json.dumps(manifest,indent=2),encoding='utf-8')
    finally:game.close()

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--exe',type=Path,required=True);p.add_argument('--rom',type=Path,required=True);p.add_argument('--output',type=Path,required=True)
    p.add_argument('--seed',type=Path,help='Optional current-build landed save0.sav; normal identity checks still apply.')
    a=p.parse_args();prepare(a.exe.resolve(),a.rom.resolve(),a.output.resolve(),a.seed.resolve() if a.seed else None)
