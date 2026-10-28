#!/usr/bin/env python3
"""Run two muted local game peers with the full QoL configuration."""
from __future__ import annotations
import argparse,csv,hashlib,json,os,re,shutil,socket,subprocess
from pathlib import Path
from test_qol_rom_removal import SOURCE,ROM_SHA,state,tomllib

def selections(jump=True, mute=False):
    result=[]
    for path in sorted((SOURCE/'mods/preloaded/packages').glob('*/1.0.0/manifest.toml')):
        d=tomllib.loads(path.read_text(encoding='utf8'))
        for f in d.get('feature',[]):
            name=d['id']+'/'+f['id']
            if f['id']=='shinespark-steering':name+=':down-to-morph=true,jump-to-cancel='+str(jump).lower()
            if f['id']=='quick-morph':name+=':air-morph=true'
            if f['id']=='item-pickup' and mute:name+=':mute-jingle=true'
            result.append(name)
    return result

def ports():
    sockets=[socket.socket(socket.AF_INET,socket.SOCK_DGRAM) for _ in range(2)]
    try:
        for s in sockets:s.bind(('127.0.0.1',0))
        return [s.getsockname()[1] for s in sockets]
    finally:
        for s in sockets:s.close()

def run_pair(a,name,mismatch):
    root=a.out/name;root.mkdir();addresses=ports();jobs=[]
    exe=a.build/('SuperMetroidSNESRecomp.exe' if os.name=='nt' else 'SuperMetroidSNESRecomp')
    for seat in range(2):
        p=root/f'p{seat}';p.mkdir()
        config=(SOURCE/'config.ini').read_text().replace('DisableFrameDelay = 0','DisableFrameDelay = 1')
        config=config.replace('Volume = 100','Volume = 0').replace('Fullscreen = 1','Fullscreen = 0')
        config=config.replace('EnableGamepad1 = true','EnableGamepad1 = false').replace('EnableGamepad2 = true','EnableGamepad2 = false')
        (p/'config.ini').write_text(config);shutil.copytree(SOURCE/'mods/preloaded/packages',p/'mods/preloaded/packages')
        different = mismatch and seat == 1
        # With the new sound option selected, make that option the mismatch.
        selected = selections(True if a.cut_pickup_jingle else not different,
                              a.cut_pickup_jingle and not different)
        (p/'mods/preloaded/state.toml').write_text(state(selected))
        # Netplay forbids local scripts; cold boot with controllers disabled.
        env={k:v for k,v in os.environ.items() if not k.startswith(('SNES_NET','SNES_RB','RNET_','RBE_','SM_'))}
        for key in ('SNESRECOMP_LLE_INTERP_TARGET_FILE','SNESRECOMP_SCREENSHOT','SNESRECOMP_SCREENSHOT_FRAME'):
            env.pop(key,None)
        env.update(SDL_VIDEODRIVER='dummy',SDL_AUDIODRIVER='dummy',SNESRECOMP_NO_LAUNCHER='1',SM_REPLAY_RECORD='0',
            SNESRECOMP_RUN_FRAMES='900',SNESRECOMP_DEBUG_PORT=str(addresses[seat]),
            SM_STATE_TRACE=str(p/'state.csv'),SM_WRAM_PROBE=str(p/'wram.csv'),SM_WRAM_PROBE_ADDRS='079B:2,0998:2,05B6:2',
            SNES_NETPLAY='1',SNES_NET_SLOT=str(seat),SNES_NET_SLOTS='2',SNES_NET_INPUT_PLAYER='0',SNES_NET_DELAY='2',
            SNES_NET_MODE='rollback',SNES_NET_TRANSPORT='lan',SNES_NET_SESSION_ID=str(0x534d0000|(os.getpid()&65535)),
            SNES_NET_BIND=f'127.0.0.1:{addresses[seat]}',SNES_NET_PEER=f'127.0.0.1:{addresses[1-seat]}',
            RNET_LOBBY_URL='ws://127.0.0.1:9')
        cmd=[str(exe),'--config',str(p/'config.ini'),str(a.rom)]
        stdout=(p/'stdout.log').open('w');stderr=(p/'stderr.log').open('w')
        proc=subprocess.Popen(cmd,cwd=p,env=env,stdout=stdout,stderr=stderr)
        jobs.append((p,proc,stdout,stderr,cmd))
    results=[]
    try:
        for p,proc,stdout,stderr,cmd in jobs:
            code=proc.wait(timeout=75);stdout.close();stderr.close()
            log=(p/'stderr.log').read_text(errors='replace')
            trace=list(csv.DictReader((p/'wram.csv').open())) if (p/'wram.csv').exists() else []
            maximum=max((int(row['frame']) for row in trace),default=0)
            refused='mod_set_not_agreed' in log or 'mod_set_mismatch' in log
            assert code in (0,1),(name,p.name,code)
            if mismatch:
                assert refused and maximum==0,(name,p.name,'refusal/zero frames',maximum)
            else:
                assert code==0 and not refused and maximum>=150,(name,p.name,code,maximum)
                assert not re.search(r'boot_digest_mismatch|\[brk\]|\[interp_cap\]|rollback.*abort|POST.*disagree',log,re.I)
            results.append({'seat':p.name,'exit':code,'maximum_guest_frame':maximum,'mod_set_refused':refused,'command':cmd})
        if not mismatch:
            logs=[(p/'stderr.log').read_text(errors='replace') for p,_,_,_,_ in jobs]
            assert 'peer confirmed the mod set' in logs[0] and 'host mod set accepted' in logs[1]
            traces=[list(csv.DictReader((p/'wram.csv').open())) for p,_,_,_,_ in jobs]
            keyed=[{int(row['frame']):row for row in rows} for rows in traces]
            shared=sorted(set(keyed[0])&set(keyed[1]));assert len(shared)>=150
            for frame in shared[:150]:assert keyed[0][frame]==keyed[1][frame],('guest trace differs',frame)
    finally:
        for p,proc,stdout,stderr,cmd in jobs:
            if proc.poll() is None:proc.terminate();proc.wait(timeout=10)
            stdout.close();stderr.close()
    return results

def main():
    p=argparse.ArgumentParser(description=__doc__)
    for key in ('build','rom','out'):p.add_argument('--'+key,type=Path,required=True)
    p.add_argument('--cut-pickup-jingle',action='store_true',
                   help='Enable cut-all pickup sounds; mismatch only that option on peer 1')
    a=p.parse_args()
    for key in ('build','rom','out'):setattr(a,key,getattr(a,key).resolve())
    assert hashlib.sha256(a.rom.read_bytes()).hexdigest()==ROM_SHA
    a.out.mkdir(parents=True,exist_ok=False)
    mismatch_name='different-pickup-jingle' if a.cut_pickup_jingle else 'different-tail-option'
    result={'matching':run_pair(a,'matching',False),mismatch_name:run_pair(a,mismatch_name,True)}
    result['exe_sha256']=hashlib.sha256((a.build/('SuperMetroidSNESRecomp.exe' if os.name=='nt' else 'SuperMetroidSNESRecomp')).read_bytes()).hexdigest()
    result['feature_count']=len(selections());result['matching_trace']='first150 common WRAM probe rows identical'
    (a.out/'results.json').write_text(json.dumps(result,indent=2));print(json.dumps(result,indent=2))

if __name__=='__main__':main()
