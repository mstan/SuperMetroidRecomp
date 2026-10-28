#!/usr/bin/env python3
"""Prepare isolated current-build states and a local owner playtest selector.

Preparation is headless/muted. Explicit selector launches use normal windows
and audio. Native saves/settings outside --out are never read or changed.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
import shutil
import subprocess
import threading
from concurrent.futures import ThreadPoolExecutor
from datetime import datetime, timezone
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

from sm_progression_qa import Session
from test_qol_rom_removal import SOURCE, state as mod_state


BASE = ['super-metroid.balance-fixes/bug-fixes']
MAPS = ['super-metroid.map-hud/' + f for f in
        ('pause-stats', 'ammo-totals:where=always', 'laser-sight',
         'reveal-map:secrets=shown', 'pause-map', 'world-map', 'minimap')]
CONTROLS = ['super-metroid.controls/zero-mission', 'super-metroid.controls/radial-menu']
MOVE = ['super-metroid.movement/' + f for f in
        ('respin', 'shinespark-steering:down-to-morph=true,jump-to-cancel=true',
         'landing-momentum', 'speed-ball', 'quick-morph:air-morph=true',
         'space-jump:window=anytime', 'bomb-control', 'store-shinespark')]
CASES = {
    'maps': dict(title='Map and aiming', features=BASE + MAPS + CONTROLS,
                 checks=[('map', 'Pause map: read the doors, items and one-way arrows. X opens the world; L/R zoom, D-pad pans, Y centres, B goes back.'),
                         ('overlay', 'Start resumes. Select+Start toggles the live minimap. Check readability and HUD overlap.'),
                         ('aim', 'R aims diagonally; hold Down for diagonal down. L holds missiles. On a connected pad, test the right-stick weapon ring.')]),
    'movement': dict(title='Movement and Speed Booster sound', features=BASE + MOVE + CONTROLS + ['super-metroid.combat/shinespark-drain'],
                     checks=[('feel', 'Start resumes. Compare respin, landing momentum, held-Down morph and sprinting in the ball. Check whether control feels predictable.'),
                             ('air', 'Jump and use L+Down to morph. Test Space Jump and stored shinespark. During a spark, Down morphs and Jump cancels.'),
                             ('sound', 'Sprint, stop and jump repeatedly. Listen for a stuck Speed Booster sound and inspect the blue palette.')]),
    'heavy': dict(title='Heavy physics comparison', features=BASE + ['super-metroid.movement/heavy-physics'],
                  checks=[('heavy', 'Start resumes. Run and jump around the landing site; judge the heavier rise/fall against the Movement setup.')]),
    'combat': dict(title='Auto charge and manual reserves', features=BASE +
                   ['super-metroid.balance-fixes/reserve-tanks:manual-control=true,come-full=true',
                    'super-metroid.combat/' + 'auto-charge',
                    'super-metroid.combat/item-magnet', 'super-metroid.combat/screw-attack',
                    'super-metroid.combat/bomb-limit:bombs=10,interval=2'],
                   checks=[('charge', 'Start resumes. Wait, tap Shoot for a charged shot; rapid further taps should be normal until charge rebuilds. Judge the timing.'),
                           ('reserve', 'Pause, press R for equipment, select the reserve transfer button and press A; press A again to stop. Check clarity and control.')]),
    'xray': dict(title='X-ray over a respawning item', features=BASE + ['super-metroid.balance-fixes/xray'],
                 checks=[('xray', 'Start resumes in the Morph Ball room with X-ray selected. Hold Dash to scan over the respawning item, turn, release and move again.')]),
}


def normal_config(device='keyboard'):
    text = (SOURCE / 'config.ini').read_text(encoding='utf-8')
    text = text.replace('Fullscreen = 1', 'Fullscreen = 0')
    text += '\n[Controller]\nSourceP1=' + ('2' if device == 'gamepad' else '1') + '\nSourceP2=0\nSourceP3=0\nSourceP4=0\n'
    text += '\n[GamepadMap]\nEnableGamepad1=true\nEnableGamepad2=false\nEnableGamepad3=false\nEnableGamepad4=false\n'
    return text


def prepare(args):
    for case in CASES.values():
        mod_state(case['features'])
    args.out.mkdir(parents=True, exist_ok=False)

    def one(key):
        game = Session(args.exe, args.rom, args.out / key,
                       features=CASES[key]['features'], capture_profile=False)
        try:
            game.boot()
            game.land()
            if key == 'xray':
                for address, value in ((0xd820, 1), (0x79f, 1), (0x78b, 9),
                                       (0xd914, 5), (0x998, 6)):
                    game.word(address, value)
                for _ in range(160):
                    game.step(10)
                    if game.word(0x998) == 8 and game.word(0xe18) == 0 and game.word(0xa42) == 0xe695:
                        break
                assert game.word(0x79b) == 0x9e9f and game.word(0xa42) == 0xe695
                game.step(30)
                game.step(8, 'left')
                game.step(30)
                assert game.word(0x1c79) == 0xeee3
                game.word(0x1e09, 0x801b)  # valid respawning-item prerequisite
            for address, value in ((0x9a2, 0xf32f), (0x9a4, 0xf32f),
                                   (0x9a6, 0x100b), (0x9a8, 0x100f),
                                   (0x9c2, 999), (0x9c4, 999), (0x9c6, 99),
                                   (0x9c8, 99), (0x9ca, 30), (0x9cc, 30),
                                   (0x9ce, 20), (0x9d0, 20), (0x9d4, 300), (0x9d6, 300)):
                game.word(address, value)
            if key == 'combat':
                game.word(0x9c2, 50)
                game.word(0x9c0, 2)
            if key == 'xray':
                game.word(0x9d2, 5)
            game.step(2)
            game.step(8, 'start')
            for _ in range(100):
                game.step(5)
                if game.word(0x998) == 15:
                    break
            assert game.word(0x998) == 15, game.state()
            game.snapshot('ready')
            game.command('savestate 0')
            game.step(2)
            state = game.output / 'saves/save0.sav'
            assert state.stat().st_size > 100000
            shutil.copyfile(state, game.output / 'prepared.sav')
            return key, game.state()
        finally:
            game.close()
            (args.out / key / 'config.ini').write_text(normal_config(), encoding='utf-8')

    with ThreadPoolExecutor(max_workers=2) as pool:
        states = dict(pool.map(one, CASES))
    data = dict(exe=str(args.exe.resolve()), rom=str(args.rom.resolve()),
                binary_sha256=hashlib.sha256(args.exe.read_bytes()).hexdigest(),
                cases=CASES, states=states,
                warning='Prepared states are current-build-only. All setups use disposable progress; native owner saves are untouched.')
    (args.out / 'manifest.json').write_text(json.dumps(data, indent=2) + '\n')
    print('Prepared', len(states), 'native-paused setups:', args.out, flush=True)


HTML = '''<!doctype html><html lang="en"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>Super Metroid playtest</title>
<style>body{font:17px/1.5 system-ui;background:#111725;color:#edf1fa;max-width:1080px;margin:30px auto;padding:0 24px}h1{font-size:30px}button,select,textarea{font:inherit}button,select{padding:8px 14px;border:1px solid #7890af;border-radius:6px;background:#223449;color:white;cursor:pointer}button:hover{background:#34516c}section{background:#1b2638;padding:22px;margin:18px 0;border-radius:12px}label{display:block;margin:15px 0}textarea{display:block;width:95%;min-height:52px;background:#101a29;color:white;border:1px solid #63738d;padding:8px}small{color:#b6c6db}#message{min-height:28px;color:#9ef0bf}.check{border-top:1px solid #536077;padding-top:12px}a{color:#9ed9ff}.head{display:flex;gap:14px;align-items:center;flex-wrap:wrap}</style>
<h1>Super Metroid · focused playtest</h1><p>Each setup starts in the native pause menu. Press Start to play. Switching setups restarts only the game launched by this selector. Your usual saves and settings are untouched.</p>
<div class="head"><label>Input <select id="device"><option value="keyboard">Keyboard</option><option value="gamepad">Controller 1</option></select></label><button onclick="save()">Save observations</button></div>
<p><small>Keyboard: arrows move · Z jumps (SNES A) · A shoots (SNES X) · X dashes (SNES B) · S cancels (SNES Y) · C/V are L/R · Enter is Start · Right Shift is Select. F1 reloads this setup. Controller checks require your connected controller; physical ownership and right-stick feel need your confirmation.</small></p>
<div id="message" role="status"></div><main id="cases"></main>
<p><small>Normal window and audio. Suit transformations remain native. Falling aim-down is deferred for your definition. Source/binary identity and prepared-state details are in manifest.json; observations stay in notes.json.</small></p>
<script>
let data;
async function api(path,body){let r=await fetch('/api/'+path,{method:body?'POST':'GET',headers:body?{'Content-Type':'application/json'}:{},body:body?JSON.stringify(body):undefined});let j=await r.json();if(!r.ok)throw Error(j.error||r.statusText);return j;}
async function init(){data=await api('state');for(let [id,c] of Object.entries(data.cases)){let s=document.createElement('section');let h=document.createElement('h2');h.textContent=c.title;s.append(h);let b=document.createElement('button');b.textContent='Open / restart setup';b.onclick=()=>launch(id);s.append(b);for(let [check,text] of c.checks){let wrap=document.createElement('div');wrap.className='check';let p=document.createElement('p');p.textContent=text;wrap.append(p);let select=document.createElement('select');select.id=id+'-'+check+'-status';for(let value of ['Unreviewed','Pass','Fail','Deferred']){let o=document.createElement('option');o.textContent=value;select.append(o);}let old=data.notes[id]?.[check]||{};select.value=old.status||'Unreviewed';wrap.append(select);let note=document.createElement('textarea');note.id=id+'-'+check+'-note';note.placeholder='Observation or caveat';note.setAttribute('aria-label',c.title+' — '+check+' observation');note.value=old.note||'';wrap.append(note);s.append(wrap);}document.querySelector('#cases').append(s);}}
async function save(){let notes={};for(let [id,c] of Object.entries(data.cases)){notes[id]={};for(let [check] of c.checks)notes[id][check]={status:document.getElementById(id+'-'+check+'-status').value,note:document.getElementById(id+'-'+check+'-note').value};}await api('note',{notes});document.querySelector('#message').textContent='Observations saved locally.';}
async function launch(id){try{await save();document.querySelector('#message').textContent='Opening '+data.cases[id].title+'…';let r=await api('launch',{id,device:document.querySelector('#device').value});document.querySelector('#message').textContent='Opened '+data.cases[id].title+'. Press Start in the game to resume.';}catch(e){document.querySelector('#message').textContent=e.message;}}
init().catch(e=>document.querySelector('#message').textContent=e.message);
</script></html>'''


class Playtest:
    def __init__(self, root):
        self.root = root.resolve()
        self.manifest = json.loads((root / 'manifest.json').read_text())
        self.child = None
        self.lock = threading.Lock()

    def launch(self, key, device):
        if key not in CASES or device not in ('keyboard', 'gamepad'):
            raise ValueError('Unknown setup or input device')
        with self.lock:
            if self.child and self.child.poll() is None:
                self.child.terminate()
                self.child.wait(timeout=10)
            exe = Path(self.manifest['exe'])
            assert hashlib.sha256(exe.read_bytes()).hexdigest() == self.manifest['binary_sha256'], 'Prepared build has changed; regenerate the setups'
            profile = self.root / key
            shutil.copyfile(profile / 'prepared.sav', profile / 'resume-once.sav')
            (profile / 'config.ini').write_text(normal_config(device), encoding='utf-8')
            env = dict(os.environ)
            for name in list(env):
                if name.startswith(('SM_', 'SNESRECOMP_', 'SNES_NET', 'RNET_', 'RBE_', 'SDL_')):
                    env.pop(name)
            env['SM_REPLAY_RECORD'] = '0'
            with (profile / 'owner-stdout.log').open('wb') as stdout, (profile / 'owner-stderr.log').open('wb') as stderr:
                self.child = subprocess.Popen([str(exe), '--config', str(profile / 'config.ini'), '--no-launcher',
                    '--resume-state', str(profile / 'resume-once.sav'), self.manifest['rom']], cwd=profile, env=env,
                    stdout=stdout, stderr=stderr, creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))
            (self.root / 'launched.json').write_text(json.dumps(dict(id=key, device=device, pid=self.child.pid,
                time=datetime.now(timezone.utc).isoformat()), indent=2))
            return dict(pid=self.child.pid)


def serve(args):
    app = Playtest(args.out)

    class Handler(BaseHTTPRequestHandler):
        def reply(self, data, code=200):
            raw = json.dumps(data).encode()
            self.send_response(code)
            self.send_header('Content-Type', 'application/json')
            self.send_header('Content-Length', str(len(raw)))
            self.end_headers()
            self.wfile.write(raw)

        def do_GET(self):
            if self.path == '/':
                raw = HTML.encode()
                self.send_response(200)
                self.send_header('Content-Type', 'text/html; charset=utf-8')
                self.send_header('Content-Length', str(len(raw)))
                self.end_headers()
                self.wfile.write(raw)
            elif self.path == '/api/state':
                notes = app.root / 'notes.json'
                self.reply(dict(cases=CASES, notes=json.loads(notes.read_text()).get('notes', {}) if notes.exists() else {}))
            else:
                self.reply(dict(error='Not found'), 404)

        def do_POST(self):
            try:
                if self.headers.get('Origin') not in (None, f'http://127.0.0.1:{args.port}', f'http://localhost:{args.port}'):
                    raise ValueError('Unexpected origin')
                length = int(self.headers.get('Content-Length', '0'))
                if not 0 < length <= 65536:
                    raise ValueError('Invalid request size')
                data = json.loads(self.rfile.read(length))
                if self.path == '/api/launch':
                    self.reply(app.launch(data['id'], data['device']))
                elif self.path == '/api/note':
                    clean = {}
                    for key, case in CASES.items():
                        clean[key] = {}
                        for check, _ in case['checks']:
                            value = data['notes'].get(key, {}).get(check, {})
                            status = value.get('status', 'Unreviewed')
                            if status not in ('Unreviewed', 'Pass', 'Fail', 'Deferred'):
                                raise ValueError('Invalid observation status')
                            clean[key][check] = dict(status=status, note=str(value.get('note', ''))[:4000])
                    path = app.root / 'notes.json'
                    pending = app.root / 'notes.pending.json'
                    pending.write_text(json.dumps(dict(time=datetime.now(timezone.utc).isoformat(), notes=clean), indent=2) + '\n')
                    pending.replace(path)
                    self.reply(dict(saved=True))
                else:
                    self.reply(dict(error='Not found'), 404)
            except (KeyError, ValueError, AssertionError, OSError) as error:
                self.reply(dict(error=str(error)), 400)

    server = ThreadingHTTPServer(('127.0.0.1', args.port), Handler)
    if args.launch:
        app.launch(args.launch, args.device)
    print(f'Playtest selector http://127.0.0.1:{args.port}', flush=True)
    server.serve_forever()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('action', choices=('prepare', 'serve'))
    parser.add_argument('--exe', type=Path)
    parser.add_argument('--rom', type=Path)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--port', type=int, default=8786)
    parser.add_argument('--launch', choices=tuple(CASES))
    parser.add_argument('--device', choices=('keyboard', 'gamepad'), default='keyboard')
    args = parser.parse_args()
    if args.action == 'prepare':
        if not args.exe or not args.rom:
            parser.error('prepare requires --exe and --rom')
        prepare(args)
    else:
        serve(args)


if __name__ == '__main__':
    main()
