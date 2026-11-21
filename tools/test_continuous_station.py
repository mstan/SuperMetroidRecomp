#!/usr/bin/env python3
"""Save twice through native station contact and a real room round trip.

Room/position/resources are test prerequisites. Shot-opened door collision,
station prompts, Yes confirmation and native SRAM checksums remain observed
outcomes. No door completion, station-used or lockout flags are injected.
"""
from pathlib import Path
import argparse, hashlib, json, struct
from test_qol_rom_removal import prefix, run_case


def scenario():
    return (prefix()+
        "poke 78d 9a89\npoke af6 1000\npoke afa 8000\npoke e16 0000\n"
        "poke 99c 7de1\npoke 998 0900\nuntil16 79b == 93d5 3000\nuntil16 998 == 8 3000\nwait 60\n"
        "poke a42 95e625e7\npoke a60 13e9\npoke a88 0000\npoke a58 37a3\npoke a5c 52eb\npoke 799 0000\n"
        "poke 9c2 2500\npoke 9c4 f301\npoke af6 6000\npoke b10 6000\npoke afa 7800\npoke b14 7800\npoke a1c 0100\n"
        "until16 1c1f == 17 400\ndump first-prompt\nwait 90\npress a 8\nwait 660\npress a 8\nwait 360\ndump first-saved\n"
        "poke af6 d000\npoke b10 d000\npoke afa 7000\npoke b14 7000\npress right+x 70\n"
        "until16 79b == 92fd 1000\nuntil16 998 == 8 1000\nwait 120\ndump left-save-room\npress left+x 100\n"
        "until16 79b == 93d5 1000\nuntil16 998 == 8 1000\nwait 120\ndump reentered\n"
        "poke 9c2 2600\npoke af6 6000\npoke b10 6000\npoke afa 7800\npoke b14 7800\npoke a1c 0100\nwait 120\ndump second-attempt\n"
        "wait 90\npress a 8\nwait 660\npress a 8\nwait 360\ndump second-saved\nquit\n")


def validate(profile):
    def snapshot(tag):
        data=(profile/(tag+'.wram.bin')).read_bytes()
        return lambda at: struct.unpack_from('<H',data,at)[0]
    prompt,first,left,back,second,last=map(snapshot,
        ('first-prompt','first-saved','left-save-room','reentered','second-attempt','second-saved'))
    assert prompt(0x1c1f)==0x17 and first(0x1e75)==1
    assert left(0x79b)==0x92fd and back(0x79b)==0x93d5
    assert left(0x998)==back(0x998)==8 and left(0x1e75)==back(0x1e75)==0
    assert second(0x1c1f)==0x17 and last(0x1e75)==1 and last(0x9c2)==38
    old=(profile/'first-saved.sram.bin').read_bytes()
    saved=(profile/'second-saved.sram.bin').read_bytes()
    assert old!=saved
    slot=last(0x952);assert 0<=slot<3
    # SaveToSram sums the native D7C0..DE1B file and stores mirrored headers.
    # Read its file offset from the verified cartridge instead of inventing it.
    return {'secondPrompt':True,'lockoutResetOnBothCrossings':True,
        'secondSramChanged':True,'secondSramSha256':hashlib.sha256(saved).hexdigest(),'slot':slot}


def main():
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('build','rom','out'):p.add_argument('--'+name,type=Path,required=True)
    args=p.parse_args();args.out=args.out.resolve();args.out.mkdir(parents=True,exist_ok=False)
    rom=args.rom.resolve().read_bytes();results=[]
    for name,seamless in [('native-transition',0),('continuous-transition',1)]:
        run_case((name,[],scenario(),'079B:2,0998:2,0AF6:2,0AFA:2,1E75:2,1C1F:2,09C2:2'),
            build=args.build.resolve(),rom=args.rom.resolve(),out=args.out,
            video_settings={'EnhancedRenderer':1,'Aspect':'16:9','PresentationEnabled':0,
                'PresentationFPS':0,'HudAnchored':1,'ContinuousRoomsEnabled':1,
                'NeighboringRoomLogic':0,'SeamlessRooms':seamless})
        case=validate(args.out/name);data=(args.out/name/'second-saved.sram.bin').read_bytes()
        offset=0x812b+2*case['slot'];base=struct.unpack_from('<H',rom,offset)[0]
        total=sum(struct.unpack_from('<'+str((0xde1c-0xd7c0)//2)+'H',data,base))&65535
        word=lambda at:struct.unpack_from('<H',data,at)[0]
        assert all(word(at+2*case['slot'])==value for at,value in
            [(0,total),(0x1ff0,total),(8,total^65535),(0x1ff8,total^65535)])
        assert word(base+0x9c2-0x9a2)==38
        results.append({'case':name,**case,'nativeChecksums':True,'storedHealth':38})
    (args.out/'results.json').write_text(json.dumps(results,indent=2)+'\n')
    print('PASS: repeat native station prompt/Yes/SRAM after real native and continuous room round trips')


if __name__=='__main__':main()
