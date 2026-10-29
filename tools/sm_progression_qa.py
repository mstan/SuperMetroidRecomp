#!/usr/bin/env python3
"""Memory-assisted Super Metroid progression QA, using a private config/saves.

The supplied ROM is never modified. All memory edits and controller commands
are journalled. Injected room transitions are diagnostic coverage, not proof of
an unassisted playthrough. Audio emulation stays enabled with a dummy device
and zero output volume.

Every session is headless: SDL's dummy video driver, no console window, no
launcher. Tier-2 coverage capture is enabled from process start (instruction
zero) and written as an identified checkpoint plus journal in the output
directory; the always-on interpreted/AOT cycle counters are read back at the
end of each session into execution.json and report.json.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import socket
import shutil
import struct
import subprocess
import sys
import time
from collections import deque


AREAS = ('Crateria', 'Brinstar', 'Norfair', 'Wrecked Ship', 'Maridia', 'Tourian', 'Ceres')
ITEM_NAMES = ('ETank', 'Missile', 'Super', 'PowerBomb', 'Bombs', 'Charge', 'Ice',
              'HiJump', 'SpeedBooster', 'Wave', 'Spazer', 'SpringBall', 'Varia',
              'Gravity', 'XRayScope', 'Plasma', 'Grapple', 'SpaceJump', 'ScrewAttack',
              'Morph', 'ReserveTank')
# Diagnostic waypoints following ordinary equipment progression. The ROM
# supplies active item objects; no community dataset or savestate is bundled.
EQUIPMENT_ROUTE = (0x9e9f, 0xa107, 0x9804, 0x9d19, 0x9bc8, 0x9c07, 0xa447,
                   0xa6e2, 0xa9e5, 0xad1b, 0xadde, 0xa890, 0xa98d, 0xac2b,
                   0xa3ae, 0xa2ce, 0xce40, 0xd9aa, 0xd6d0, 0xd2aa, 0xb6c1)
WORDS = dict(room=0x79b, area=0x79f, state=0x998, door=0x78d,
             transition=0x99c, x=0xaf6, y=0xafa, health=0x9c2,
             items=0x9a4, beams=0x9a8, max_health=0x9c4,
             missiles=0x9c6, max_missiles=0x9c8, max_supers=0x9cc,
             max_powerbombs=0x9d0, max_reserve=0x9d4, pose=0xa1c, time_frames=0x9da)


class Rom:
    def __init__(self, path):
        self.data = Path(path).read_bytes()
        if len(self.data) != 0x300000 or hashlib.sha256(self.data).hexdigest() != (
                '12b77c4bc9c1832cee8881244659065ee1d84c70c3d29e6eaf92e6798cc2ca72'):
            # The game host also verifies identity. Reject headered/other ROMs
            # before deriving memory-edit addresses from the wrong revision.
            expected = Path(__file__).resolve().parents[1] / 'rom_identity.txt'
            identity = expected.read_text() if expected.exists() else ''
            digest = hashlib.sha256(self.data).hexdigest()
            if len(self.data) != 0x300000 or digest not in identity:
                raise ValueError(f'Expected verified unheadered JU ROM; SHA256={digest}')

    def offset(self, address):
        return ((address >> 16) & 0x7f) * 0x8000 + (address & 0x7fff)

    def byte(self, address):
        return self.data[self.offset(address)]

    def word(self, address):
        return struct.unpack_from('<H', self.data, self.offset(address))[0]

    def rooms(self):
        # Room headers begin with geometry and one of the ROM's state selectors.
        # Restrict to the actual room-header region, excluding the unused debug
        # room, and validate every door's destination against the header set.
        rooms = {}
        for address in range(0x91f8, 0xe100):
            base = 0x8f0000 | address
            if (self.byte(base + 1) <= 6 and 0 < self.byte(base + 4) <= 16
                    and 0 < self.byte(base + 5) <= 16
                    and self.word(base + 11) in (0xe5e6, 0xe5ff, 0xe612, 0xe629, 0xe652, 0xe669)
                    and 0x8000 <= self.word(base + 9) < 0xe200):
                rooms[address] = dict(area=self.byte(base + 1), width=self.byte(base + 4),
                                      height=self.byte(base + 5), doors=[])
        for address, room in rooms.items():
            table = self.word(0x8f0000 | (address + 9))
            for index in range(16):
                door = self.word(0x8f0000 | (table + 2 * index))
                if door < 0x8000:
                    break
                destination = self.word(0x830000 | door)
                # Null-destination doors ($88FC/$A18A) are unused slots
                # inside live lists, not terminators; save rooms follow them.
                if destination == 0:
                    continue
                if destination not in rooms:
                    break
                base = 0x830000 | door
                room['doors'].append(dict(pointer=door, destination=destination,
                    flags=self.byte(base + 2), direction=self.byte(base + 3),
                    cap_x=self.byte(base + 4), cap_y=self.byte(base + 5),
                    screen_x=self.byte(base + 6), screen_y=self.byte(base + 7)))
        return rooms


class Session:
    def __init__(self, exe, rom, output, port=0, floor=None, wide=False, interpreter_only=False, interpret_targets=(), seed_sram=None, capture_profile=True):
        with socket.socket() as reservation:
            reservation.bind(('127.0.0.1', port))
            port = reservation.getsockname()[1]
        self.output = Path(output).resolve()
        self.output.mkdir(parents=True, exist_ok=False)
        (self.output / 'saves').mkdir()
        if seed_sram:
            shutil.copyfile(Path(seed_sram).resolve(strict=True), self.output / 'saves' / 'save.srm')
        self.journal = (self.output / 'commands.jsonl').open('w', encoding='utf-8')
        self.config = self.output / 'config.ini'
        self.config.write_text(
            '[General]\nAutosave=0\nSkipLauncher=1\nDisableFrameDelay=1\nRunAhead=0\n'
            '[Graphics]\nWindowScale=2\nVSync=0\nRenderer=software\nNewRenderer=1\nNoSpriteLimits=0\n'
            '[Sound]\nEnableAudio=1\nVolume=0\n'
            '[Controller]\nSourceP1=0\nSourceP2=0\nSourceP3=0\nSourceP4=0\n'
            '[GamepadMap]\nEnableGamepad1=false\nEnableGamepad2=false\n'
            'EnableGamepad3=false\nEnableGamepad4=false\n',
            encoding='utf-8')
        (self.output / 'sm-video.ini').write_text(
            f'[SuperMetroidVideo]\nEnhancedRenderer={int(wide)}\nAspect=16:9\nPresentationEnabled=0\n',
            encoding='utf-8')
        env = os.environ.copy()
        for key in list(env):
            if key.startswith(('SM_', 'SNESRECOMP_')):
                env.pop(key)
        # Never show a window: SDL2 and SDL3 spell the video driver hint
        # differently, so set both.
        env.update(SM_REPLAY_RECORD='0', SDL_AUDIODRIVER='dummy', SDL_AUDIO_DRIVER='dummy',
                   SDL_VIDEODRIVER='dummy', SDL_VIDEO_DRIVER='dummy',
                   SNESRECOMP_DEBUG_PORT=str(port), SNESRECOMP_CPU_TRACE_RING_ENTRIES='65536')
        self.capture_profile = capture_profile
        if capture_profile:
            env.update(SNESRECOMP_TIER2_CAPTURE='1',
                       SNESRECOMP_TIER2_MANIFEST=str(self.output / 'tier2-profile.json'),
                       SNESRECOMP_TIER2_JOURNAL=str(self.output / 'tier2-profile.jsonl'))
        if floor:
            env['SNESRECOMP_EXECUTION_MODE'] = floor
        if interpreter_only:
            env['SNESRECOMP_LLE_BOUNCE'] = '0'
        if interpret_targets:
            targets = self.output / 'interpret-targets.txt'
            targets.write_text(''.join(f'{p:06x}\n{p ^ 0x800000:06x}\n' for p in interpret_targets))
            env['SNESRECOMP_LLE_INTERP_TARGET_FILE'] = str(targets)
        self.stdout = (self.output / 'stdout.log').open('wb')
        self.stderr = (self.output / 'stderr.log').open('wb')
        self.process = subprocess.Popen(
            [str(Path(exe).resolve()), '--config', str(self.config), '--paused', '--no-launcher',
             str(Path(rom).resolve())],
            cwd=self.output, env=env, stdout=self.stdout, stderr=self.stderr,
            creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))
        self.sock = None
        self.allow_rebase = False
        self.invincible = False
        self.health_floor = 1999
        self.step_chunk = 60
        self.assist_pickups = False
        deadline = time.monotonic() + 40
        while time.monotonic() < deadline:
            if self.process.poll() is not None:
                self.close()
                raise RuntimeError(f'Game exited {self.process.returncode}; see {self.output}')
            try:
                self.sock = socket.create_connection(('127.0.0.1', port), timeout=1)
                break
            except OSError:
                time.sleep(.1)
        if self.sock is None:
            self.close()
            raise TimeoutError('Debug server not ready')
        # Generous: a whole-WRAM read on a loaded host can take seconds, and a
        # timeout here abandons the session rather than reporting a guest fault.
        self.sock.settimeout(120)
        self.stream = self.sock.makefile('rb')
        self.command('frame')

    def command(self, command):
        if command.startswith('loadstate '):
            self.allow_rebase = True
        self.journal.write(json.dumps(dict(command=command)) + '\n')
        self.journal.flush()
        self.sock.sendall(command.encode('ascii') + b'\n')
        response = json.loads(self.stream.readline())
        if 'connected' in response:
            response = json.loads(self.stream.readline())
        if 'error' in response or response.get('ok') is False:
            raise RuntimeError(f'{command}: {response}')
        return response

    def read(self, address, size):
        return bytes.fromhex(self.command(f'read_ram {address:x} {size}')['hex'])

    def write(self, address, data):
        reply = self.command(f'write_ram {address:x} {data.hex()}')
        if reply['count'] != len(data):
            raise RuntimeError('Short WRAM write')

    def word(self, address, value=None):
        if value is None:
            return struct.unpack('<H', self.read(address, 2))[0]
        self.write(address, struct.pack('<H', value & 0xffff))

    def state(self):
        ram = self.read(0x700, 0x1500)
        result = {name: struct.unpack_from('<H', ram, address - 0x700)[0]
                  for name, address in WORDS.items()}
        result['frame'] = self.command('frame')['frame']
        result['bosses'] = self.read(0xd828, 8).hex()
        return result

    def step(self, frames=1, buttons='0'):
        self.command(f'set_controller {buttons}')
        remaining = frames
        while remaining:
            count = min(remaining, self.step_chunk)
            if self.invincible:
                self.word(0x9c2, self.health_floor)
            result = self.command(f'step {count}')
            # The debug server decrements its step counter before publishing
            # the frame counter; wait for publication instead of mistaking
            # that one-frame handoff for a guest stall.
            expected = result['frame_before'] + count
            deadline = time.monotonic() + 2
            while not self.allow_rebase and not result.get('timeout') and result['frame_after'] < expected and time.monotonic() < deadline:
                result['frame_after'] = self.command('frame')['frame']
            if result.get('timeout') or (not self.allow_rebase and result['frame_after'] != expected):
                raise RuntimeError(f'Simulation stopped advancing: {result}')
            self.allow_rebase = False
            remaining -= count

    def snapshot(self, name):
        # L3 snapshots contain hardware state, not the port execution residue;
        # keep them for diagnostics only. Slot savestates exercise real reloads.
        self.command(f'save_state {name}.l3sn')
        self.command(f'screenshot {name}.bmp')
        (self.output / f'{name}.wram').write_bytes(self.read(0, 0x20000))
        state = self.state()
        (self.output / f'{name}.json').write_text(json.dumps(state, indent=2) + '\n')
        return state

    def boot(self):
        self.step(210)
        for buttons, frames, wait in [('start', 8, 120), ('start', 8, 360), ('start', 12, 240)]:
            self.step(frames, buttons)
            self.step(wait)
        for _ in range(80):
            self.step(8, 'a')
            self.step(300)
            state = self.state()
            if state['state'] == 8:
                return self.snapshot('boot')
        self.snapshot('boot-failure')
        raise RuntimeError(f'New game did not reach gameplay: {self.state()}')

    def transition(self, door, limit=900):
        self.dismiss_dialog()
        before = self.state()
        self.command('set_controller 0')
        # Enter the same state used by a door-block collision. Let the ROM
        # select room state, decompress tiles, upload enemies/music, execute
        # room/door ASM, scroll and fade in. Never just replace room_ptr.
        self.word(0x78d, door['pointer'])
        direction = door['direction'] & 3
        x = (0xf0 if direction == 0 else 0x10) if direction < 2 else (door['cap_x'] & 15) * 16 + 32
        y = (door['cap_y'] & 15) * 16 + 32 if direction < 2 else (0xe0 if direction == 2 else 0x10)
        self.word(0xaf6, x)
        self.word(0xafa, y)
        self.word(0xb10, x)
        self.word(0xb14, y)
        self.word(0xe16, 0)  # no stale outgoing elevator animation
        self.word(0x99c, 0xe17d)
        self.word(0x998, 9)
        for elapsed in range(0, limit, 30):
            self.step(30)
            after = self.state()
            if after['state'] == 8 and after['room'] == door['destination']:
                # A forced elevator hop did not run its outgoing enemy's
                # animation. Release that test-only control lock explicitly;
                # use the ROM's normal handlers, as the existing effect
                # fixtures do. This edit is not evidence of a game defect.
                self.write(0xa42, bytes.fromhex('95e625e7'))
                self.word(0xa60, 0xe913)
                self.word(0xa88, 0)
                self.word(0xa58, 0xa337)
                self.word(0xa5c, 0xeb52)
                self.word(0xe16, 0)
                self.word(0xe18, 0)
                self.word(0x799, 0)
                self.step(8, 'right' if direction == 0 else 'left')
                self.step(2)
                return dict(before=before, after=after, frames=elapsed + 30)
        self.snapshot(f"stalled-{before['room']:04x}-{door['pointer']:04x}")
        raise RuntimeError(f'Door did not settle: {door}, {self.state()}')

    def dismiss_dialog(self):
        # Item messages impose a 360-frame reading delay. game_state stays
        # eight while the CPU is inside that coroutine, so it is insufficient
        # as a readiness check. Do not overwrite it with a door state while
        # the message still owns the guest stack.
        for _ in range(24):
            cpu = self.command('get_v2_cpu')
            pc = int(cpu.get('resume_pc', '0'), 16)
            if pc >> 16 != 0x85 and not self.word(0xdc8):
                return
            self.step(30)
            self.step(4, 'a')
        raise RuntimeError(f'Message did not return to gameplay: {self.state()}')

    def land(self):
        # The same load mode the Ceres escape cutscene hands to the loader.
        # This skips the cinematic but initializes ship/room/PLMs in the ROM.
        self.word(0xd914, 0x22)
        self.word(0x998, 6)
        for _ in range(40):
            self.step(60)
            state = self.state()
            if state['room'] == 0x91f8 and state['state'] == 8:
                self.step(600)
                return self.snapshot('landing')
        self.snapshot('landing-failure')
        raise RuntimeError(f'Landing did not settle: {self.state()}')

    def save_roundtrip(self, slot=0):
        self.command(f'savestate {slot}')
        self.step(2)
        saved = self.state()
        refill = self.invincible
        self.invincible = False
        self.word(0x9c2, 1)
        self.command(f'loadstate {slot}')
        self.step(3)
        loaded = self.state()
        self.invincible = refill
        if loaded['health'] != saved['health'] or loaded['room'] != saved['room']:
            raise RuntimeError(f'Save roundtrip failed: {saved}, {loaded}')
        self.step(30, 'right')
        after = self.state()
        if after['time_frames'] == loaded['time_frames'] and after['x'] == loaded['x']:
            raise RuntimeError(f'Guest frozen after reload: {loaded}, {after}')
        return dict(saved=saved, loaded=loaded, after_input=after)

    def collect(self, rom, items):
        results = []
        for item in items:
            if 'address' in item:
                offset = int(item['address'], 16)
                plm, bx, by, argument = struct.unpack_from('<HBBH', rom.data, offset)
            else:
                plm, bx, by = item['plm'], item['bx'], item['by']
            before = self.state()
            x, y = bx * 16 + 8, by * 16 + 8
            self.word(0xaf6, x - 24)
            self.word(0xafa, y - 8)
            self.word(0xb10, x - 24)
            self.word(0xb14, y - 8)
            self.step(3, 'left')
            self.step(30, 'right+x')
            # After shooting a Chozo orb, approach from above so the normal
            # block-collision pickup runs, instead of stopping at its pedestal.
            self.word(0xaf6, x)
            self.word(0xafa, y - 32)
            self.word(0xb10, x)
            self.word(0xb14, y - 32)
            for _ in range(10):
                self.step(12, 'right+x')
                self.step(12)
                self.step(4, 'b')
            after = self.state()
            metrics = ('items', 'beams', 'max_health', 'max_missiles', 'max_supers', 'max_powerbombs', 'max_reserve')
            changed = any(before[k] != after[k] for k in metrics)
            assisted = False
            if not changed and self.assist_pickups:
                # A short walk can leave a small item room. Finish that door
                # before returning to the intended object; do not count the
                # outgoing transition as a failed pickup or a guest softlock.
                for _ in range(30):
                    if after['state'] == 8:
                        break
                    self.step(30)
                    after = self.state()
                if after['room'] != before['room']:
                    rooms = rom.rooms()
                    door = next(d for room in rooms.values() for d in room['doors']
                                if d['destination'] == before['room'])
                    self.transition(door)
                # Feed the same per-item contact/shot marker used by guest
                # collision code. Inventory, item bits, messages, suit effects
                # and control restoration still execute in the ROM.
                for _ in range(8):
                    active = self.read(0x1c37, 0x190)
                    width = self.word(0x7a5)
                    for slot in range(40):
                        header = struct.unpack_from('<H', active, slot * 2)[0]
                        block = struct.unpack_from('<H', active, 0x50 + slot * 2)[0]
                        if header != plm or block != (by * width + bx) * 2:
                            continue
                        pre = struct.unpack_from('<H', active, 0xa0 + slot * 2)[0]
                        if pre in (0xdf89, 0xdfe6, 0xbd0f):
                            self.word(0x1d77 + slot * 2, 1 if pre == 0xbd0f else 0xff)
                            assisted = True
                    self.step(30)
                    self.step(4, 'b')
                after = self.state()
                changed = any(before[k] != after[k] for k in metrics)
            self.dismiss_dialog()
            after = self.state()
            results.append(dict(item=item['name'], plm=f'{plm:04x}', x=x, y=y,
                                acquired=changed, assisted=assisted, before=before, after=after))
        return results

    def active_items(self):
        headers, blocks = self.read(0x1c37, 80), self.read(0x1c87, 80)
        width = self.word(0x7a5)
        result = []
        for slot in range(40):
            header = struct.unpack_from('<H', headers, slot * 2)[0]
            if 0xeed7 <= header <= 0xefcf and (header - 0xeed7) % 4 == 0:
                block = struct.unpack_from('<H', blocks, slot * 2)[0] // 2
                result.append(dict(plm=header, bx=block % width, by=block // width,
                                   name=ITEM_NAMES[(header - 0xeed7) // 4 % 21]))
        return result

    def boss_assist(self, frames=1800, phase=False):
        """Leave phase/death logic to the guest, accelerate real projectile hits."""
        before = self.state()
        self.step(120, 'right' if before['x'] < 128 else 'left')
        self.step(180)
        self.word(0x9d2, 2)  # real super missiles, fired through normal input
        for _ in range(frames // 20):
            enemies = self.read(0xf78, 0x400)
            enemy = enemies[:0x40]
            enemy_id, x, _, y = struct.unpack_from('<HHHH', enemy)
            health = struct.unpack_from('<H', enemy, 0x14)[0]
            if enemy_id and health:
                for slot in range(16):
                    if struct.unpack_from('<H', enemies, slot * 64)[0] and struct.unpack_from('<H', enemies, slot * 64 + 20)[0]:
                        self.word(0xf8c + slot * 64, 1)
            self.word(0x9ca, 50)
            self.step(4, 'x')
            if enemy_id and health:
                types = self.read(0xc18, 10)
                for projectile in range(5):
                    if struct.unpack_from('<H', types, projectile * 2)[0]:
                        self.word(0xb64 + projectile * 2, x)
                        self.word(0xb78 + projectile * 2, y)
                        self.word(0xc2c + projectile * 2, 0x7fff)
                        self.word(0xbb4 + projectile * 2, 64)
                        self.word(0xbc8 + projectile * 2, 64)
            self.step(16)
            now = self.state()
            if now['bosses'] != before['bosses']:
                self.step(600)
                return dict(defeated=True, method='damage', before=before, after=self.state())
        method = 'damage'
        if phase:
            # Explicit diagnostic phase injection; never write the boss bits.
            # Let the ROM execute death animation, item drops and room unlock.
            room = self.state()['room']
            if room == 0xb32e:  # Ridley main AI observes zero health itself.
                self.word(0xf8c, 0)
                method = 'zero-health'
            elif room == 0xcd13:  # Phantoon_StartDeathSequence, non-swooping branch.
                self.word(0xfb2, 0xd948)
                self.word(0xfec, 0)
                self.word(0xff2, 0)
                self.word(0xf86, self.word(0xf86) | 0x400)
                self.word(0x1988, self.word(0x1988) | 0x4000)
                method = 'death-phase'
            elif room == 0xa59f:  # Kraid_Death_Init.
                self.word(0xfa8, 0xc360)
                self.word(0x782a, 0)
                method = 'death-phase'
            if method != 'damage':
                for _ in range(60):
                    self.step(60)
                    if self.state()['bosses'] != before['bosses']:
                        self.step(600)
                        return dict(defeated=True, method=method, before=before, after=self.state())
        return dict(defeated=False, method=method, before=before, after=self.state())

    def save_station(self, rooms):
        """Land on the Crateria save platform and accept the real save prompt."""
        door = next(d for room in rooms.values() for d in room['doors']
                    if d['destination'] == 0x93d5)
        self.transition(door)
        before = bytes.fromhex(self.command('read_sram 0 8192')['hex'])
        self.word(0x1e75, 0)
        headers, blocks = self.read(0x1c37, 80), self.read(0x1c87, 80)
        slot = next(i for i in range(40) if struct.unpack_from('<H', headers, i * 2)[0] == 0xb76f)
        block = struct.unpack_from('<H', blocks, slot * 2)[0] // 2
        width = rooms[0x93d5]['width'] * 16
        x, y = (block % width) * 16 + 8, (block // width) * 16 - 64
        for address, value in ((0xaf6, x), (0xb10, x), (0xafa, y), (0xb14, y), (0xa1c, 1)):
            self.word(address, value)
        self.step(100)
        self.snapshot('save-prompt')
        self.dismiss_dialog()
        self.step(600)
        after = bytes.fromhex(self.command('read_sram 0 8192')['hex'])
        if before == after:
            raise RuntimeError('Save station did not change SRAM')
        (self.output / 'station-sram.bin').write_bytes(after)
        return dict(state=self.snapshot('station-saved'), load_station=self.word(0x78b),
                    sram_sha256=hashlib.sha256(after).hexdigest())

    def mother_brain(self, timeline):
        """Mother Brain via explicit phase injection; the ROM runs every phase.

        Diagnostic assists, all journalled: the glass event is set instead of
        shooting the tank, the brain's health is zeroed when each phase's AI
        is active (the AI observes zero health itself, as for Ridley), and
        Samus' health is held low during the rainbow beam so the ROM's own
        baby Metroid sequence and hyper beam hand-off run. Returns once the
        escape timebomb event is set."""
        # Enemy slot 0 is the body, slot 1 the brain; AI pointers in bank A9.
        first_phase, phase2_thinking, phase3_main = 0x87E1, 0xB605, 0xC209
        self.write(0xd820, bytes([self.read(0xd820, 1)[0] | 0x04]))  # event 2
        for address, value in ((0xaf6, 0x80), (0xb10, 0x80)):
            self.word(address, value)
        revive_start = 0xC059
        zeroed, last, revived = set(), None, False
        for _ in range(2400):
            body_ai, brain_health = self.word(0xfa8), self.word(0xfcc)
            state = self.state()
            if (body_ai, state['state']) != last:
                last = (body_ai, state['state'])
                enemies = self.read(0xf78, 0x800)
                slots = [dict(slot=slot, id=f'{struct.unpack_from("<H", enemies, slot * 0x40)[0]:04x}',
                              ai=f'{struct.unpack_from("<H", enemies, slot * 0x40 + 0x30)[0]:04x}',
                              health=struct.unpack_from('<H', enemies, slot * 0x40 + 0x14)[0])
                         for slot in range(32) if struct.unpack_from('<H', enemies, slot * 0x40)[0]]
                timeline.append(dict(frame=state['frame'], body_ai=f'{body_ai:04x}',
                                     brain_health=brain_health, samus_health=state['health'],
                                     game_state=state['state'], room=f"{state['room']:04x}",
                                     enemies=slots))
            if self.read(0xd821, 1)[0] & 0x40:  # event 0x0E: Zebes timebomb set
                self.health_floor = 1999
                return True
            if body_ai in (first_phase, phase2_thinking, phase3_main) and brain_health:
                if body_ai == first_phase:
                    self.word(0xaf6, 0x80)
                    self.word(0xb10, 0x80)
                self.word(0xfcc, 0)
                zeroed.add(body_ai)
                timeline.append(dict(frame=state['frame'], assist=f'zero brain health at {body_ai:04x}'))
            # Rainbow beam until the baby Metroid drains the brain: hold
            # Samus low so the ROM's cut-in (health <= 4*damage(0x50)+20)
            # runs, never at zero. From the brain's revival (36000 health,
            # Phase2_Revive_0 at $A9:C059) the baby heals Samus to full and
            # must see her full, so the normal refill resumes.
            if brain_health == 36000 or body_ai >= revive_start:
                revived = True
            self.health_floor = 30 if phase2_thinking in zeroed and not revived else 1999
            # The baby Metroid's two waits for the brain's attacks to drain it
            # (ShitroidInCutscene_IdleUntilToNoHealth $A9:CABD after healing
            # Samus, ShitroidInCutscene_FinalCharge $A9:CBD8) each observe
            # zero health themselves. Inject those phases too; the release,
            # the death, the hyper beam hand-off remain the ROM's.
            if revived:
                enemies = self.read(0xf78, 0x800)
                for slot in range(2, 32):
                    base = slot * 0x40
                    if (struct.unpack_from('<H', enemies, base + 0x30)[0] in (0xcabd, 0xcbd8)
                            and struct.unpack_from('<H', enemies, base + 0x14)[0]):
                        self.word(0xf78 + base + 0x14, 0)
                        timeline.append(dict(frame=state['frame'], assist=f'zero baby Metroid health (slot {slot})'))
            self.step(10)
        return False

    def ride_gunship(self, timeline, frames=48000):
        """Board the ship during the escape and let the ending and credits run."""
        enemies = self.read(0xf78, 0x800)
        for slot in range(32):
            if struct.unpack_from('<H', enemies, slot * 0x40 + 0x3a)[0] == 0xa9bd:  # GunshipTop_8
                ship = 0xf78 + slot * 0x40
                ship_x = struct.unpack_from('<H', enemies, slot * 0x40 + 2)[0]
                ship_y = struct.unpack_from('<H', enemies, slot * 0x40 + 6)[0]
                break
        else:
            raise RuntimeError('Gunship not found in landing site enemy slots')
        for address, value in ((0xaf6, ship_x), (0xb10, ship_x), (0xafa, ship_y - 24), (0xb14, ship_y - 24)):
            self.word(address, value)
        # Enemies are only processed near the screen. A teleport skips the
        # scroll triggers on the way, so the camera stays behind red scroll
        # blocks: open the room's scrolls ($CD20, one byte per screen) and
        # bring layer 1 ($0911/$0915) to the ship, then let it settle.
        screens = self.word(0x7a9) * self.word(0x7ab)
        self.write(0xcd20, bytes([1] * screens))
        self.word(0x911, max(0, ship_x - 128))
        self.word(0x915, max(0, ship_y - 128))
        self.step(30)
        if abs(self.word(0x911) + 128 - ship_x) >= 128:
            raise RuntimeError(f'Camera did not reach the ship: layer1 x={self.word(0x911)}')
        last, attempts = None, 0
        for _ in range(frames // 60):
            state = self.state()
            if state['state'] != last:
                last = state['state']
                timeline.append(dict(frame=state['frame'], game_state=state['state'], room=f"{state['room']:04x}"))
                self.snapshot(f"ending-state-{state['state']:02x}")
            if state['state'] == 8:
                if attempts < 4:
                    # GunshipTop_8's boarding test, as the guest sees it.
                    timeline.append(dict(frame=state['frame'], board_attempt=attempts,
                                         samus=(self.word(0xaf6), self.word(0xafa)),
                                         movement=self.read(0xa1f, 1)[0], alfa=f'{self.word(0xa42):04x}',
                                         ship=(self.word(ship + 2), self.word(ship + 6)),
                                         ship_ai=f'{self.word(ship + 0x3a):04x}'))
                attempts += 1
                # GunshipTop_8 needs Samus standing (movement type 0) on the
                # frame Down is newly pressed; stand up first if not.
                if self.read(0xa1f, 1)[0]:
                    self.step(1, 'up')
                    self.step(29)
                else:
                    self.step(1, 'down')
                    self.step(59)
            else:
                self.step(60)
        return last

    def execution(self):
        """Read the always-on counters; nothing is armed by this query.

        interp_stats and interp_profile accumulate from instruction zero, so
        the reply covers the whole session up to the current frame."""
        stats = self.command('interp_stats')
        profile = self.command('interp_profile top=64')
        result = dict(frame=self.command('frame')['frame'], interp_stats=stats,
                      interp_profile=profile)
        (self.output / 'execution.json').write_text(json.dumps(result, indent=2) + '\n')
        return result

    def close(self):
        process = getattr(self, 'process', None)
        if process is not None and process.poll() is None and self.sock is not None:
            # A headless host has no window to close and keeps running
            # once released. Persist the final complete checkpoint (the
            # periodic one is up to five seconds old) and the execution
            # counters before stopping it, while the guest is paused at
            # the frame the route ended on.
            try:
                self.final_execution = self.execution()
            except (OSError, ValueError, RuntimeError) as error:
                self.final_execution = dict(error=str(error))
            if self.capture_profile:
                try:
                    self.command(f'tier2_dump {self.output / "tier2-profile.json"}')
                except (OSError, ValueError, RuntimeError) as error:
                    self.final_execution['tier2_dump_error'] = str(error)
        if process is not None and process.poll() is None:
            process.terminate()
            try:
                process.wait(timeout=10)
            except subprocess.TimeoutExpired:
                # A parked headless host can outlive SIGTERM (its debug loop
                # never reaps a dead client); escalate so no session leaks a
                # process, and never raise out of close() to mask the route's
                # real result.
                process.kill()
                try:
                    process.wait(timeout=10)
                except subprocess.TimeoutExpired:
                    print(f'[session] process {process.pid} survived SIGKILL; '
                          f'leaked', file=sys.stderr)
        if self.sock is not None:
            try:
                self.stream.close()
                self.sock.close()
            except OSError:
                pass
            self.stream = None
            self.sock = None
        for name in ('journal', 'stdout', 'stderr'):
            stream = getattr(self, name, None)
            if stream:
                try:
                    stream.close()
                except OSError:
                    pass
                setattr(self, name, None)


def path_to(rooms, start, target):
    queue = deque([(start, [])])
    seen = {start}
    while queue:
        current, path = queue.popleft()
        if current == target:
            return path
        for door in rooms[current]['doors']:
            dest = door['destination']
            if dest not in seen:
                seen.add(dest)
                queue.append((dest, path + [door]))
    raise ValueError(f'No door path {start:04x} -> {target:04x}')

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--exe', type=Path, required=True)
    parser.add_argument('--rom', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--port', type=int, default=0, help='TCP port; default chooses an available local port')
    parser.add_argument('--floor', choices=('off', 'lle', 'hle'))
    parser.add_argument('--capture-profile', dest='capture_profile', action='store_true', default=True,
                        help='capture an identified tier-2 profile in the private QA output (default)')
    parser.add_argument('--no-capture-profile', dest='capture_profile', action='store_false',
                        help='disable tier-2 capture (cycle counters remain always-on)')
    parser.add_argument('--attract', type=int, default=0, metavar='FRAMES',
                        help='power on and let the title/attract demos run FRAMES frames with no input')
    parser.add_argument('--wide', action='store_true')
    parser.add_argument('--interpreter-only', action='store_true', help='reference run with AOT bounces disabled')
    parser.add_argument('--interpret-targets', nargs='*', type=lambda x: int(x, 16), default=[])
    parser.add_argument('--transition-probe', action='store_true')
    parser.add_argument('--landing-probe', action='store_true')
    parser.add_argument('--save-probe', action='store_true')
    parser.add_argument('--rooms', nargs='*', type=lambda x: int(x, 16), default=[])
    parser.add_argument('--equipment-route', action='store_true', help='visit equipment waypoints and collect active items')
    parser.add_argument('--collect-items', action='store_true', help='discover and collect active item PLMs from guest RAM')
    parser.add_argument('--item-data', type=Path, help='optional sm-json-data room/item index')
    parser.add_argument('--direct', action='store_true', help='teleport via an incoming horizontal door when possible')
    parser.add_argument('--seed-state', type=Path, help='private host savestate at a normal gameplay NMI boundary')
    parser.add_argument('--seed-sram', type=Path, help='copy guest SRAM before launch, then start via normal menus')
    parser.add_argument('--station-probe', action='store_true', help='activate a real save station and verify SRAM changes')
    parser.add_argument('--invincible', action='store_true', help='refill health before each frame batch; logged as a cheat')
    parser.add_argument('--assist-pickups', action='store_true', help='simulate contact/shot marker for active item PLMs after input attempts')
    parser.add_argument('--sweep', action='store_true', help='visit every Zebes room through the ROM loader')
    parser.add_argument('--post-boss', action='store_true', help='diagnostic boss bits for alternate room states; does not validate boss victories')
    parser.add_argument('--grant-equipment', action='store_true', help='diagnostic traversal equipment and ammo')
    parser.add_argument('--save-each-room', action='store_true', help='retain real reloadable host checkpoints')
    parser.add_argument('--start-room', type=lambda x: int(x, 16), default=0, help='resume a sweep at this room address')
    parser.add_argument('--boss-assist', action='store_true', help='one HP boss and high damage projectiles; require real boss flags')
    parser.add_argument('--boss-phase', action='store_true', help='allow explicit death-phase injection for Kraid, Phantoon and Ridley')
    parser.add_argument('--endgame', action='store_true',
                        help='Mother Brain by phase injection, timed escape to the ship, ending and credits')
    parser.add_argument('--board-ship', action='store_true',
                        help='from an escape landing-site seed, board the ship and run the ending')
    args = parser.parse_args()
    rom = Rom(args.rom)
    report = dict(schema=1, status='running',
                  rom_sha256=hashlib.sha256(rom.data).hexdigest(),
                  executable_sha256=hashlib.sha256(args.exe.read_bytes()).hexdigest(),
                  arguments={key: str(value) if isinstance(value, Path) else value
                             for key, value in vars(args).items()}, cases=[])
    game = Session(args.exe, args.rom, args.output, args.port, args.floor, args.wide, args.interpreter_only, args.interpret_targets, args.seed_sram, args.capture_profile)
    def write_report():
        (game.output / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
    write_report()
    try:
        if args.attract:
            # No input from power-on: title, intro and the ROM's own demos.
            game.step(args.attract)
            report['initial'] = game.snapshot('attract')
            print('attract', json.dumps(report['initial']), flush=True)
        elif args.seed_state:
            shutil.copyfile(args.seed_state.resolve(strict=True), game.output / 'saves' / 'save0.sav')
            game.step(1)
            game.command('loadstate 0')
            game.step(3)
            report['initial'] = game.snapshot('seed')
            print('seed', json.dumps(report['initial']), flush=True)
        else:
            report['initial'] = game.boot()
            print(json.dumps(report['initial']), flush=True)
        if args.landing_probe:
            print('landing', json.dumps(game.land()), flush=True)
            game.step(90, 'right')
            print('movement', json.dumps(game.snapshot('movement')), flush=True)
        if args.invincible:
            game.word(0x9c4, 1999)
            game.invincible = True
        game.assist_pickups = args.assist_pickups
        if args.post_boss:
            game.write(0xd828, bytes([7] * 6 + [0, 0]))
        if args.grant_equipment:
            game.write(0x9a2, struct.pack('<HHHH', 0xf32f, 0xf32f, 0x100b, 0x100f))
            game.write(0x9c6, struct.pack('<HHHHHH', 230, 230, 50, 50, 50, 50))
        if args.save_probe:
            report['save_roundtrip'] = game.save_roundtrip()
            print('save roundtrip passed', flush=True)
        if args.station_probe:
            report['save_station'] = game.save_station(rom.rooms())
            print('guest save station passed', flush=True)
        if args.rooms or args.sweep or args.equipment_route:
            rooms = rom.rooms()
            metadata = json.loads(args.item_data.read_text()) if args.item_data else {}
            targets = args.rooms or (EQUIPMENT_ROUTE if args.equipment_route else
                                    sorted(p for p, room in rooms.items() if room['area'] < 6 and p >= args.start_room))
            for target in targets:
                case = dict(target=f'{target:04x}', status='running', transitions=[])
                report['cases'].append(case)
                write_report()
                if args.direct or args.sweep or args.equipment_route:
                    candidates = [d for room in rooms.values() for d in room['doors'] if d['destination'] == target]
                    if not candidates:
                        case.update(status='skipped', reason='no incoming door in ROM graph')
                        write_report()
                        print('skipped', f'{target:04x}', 'no incoming door in ROM graph', flush=True)
                        continue
                    route = [min(candidates, key=lambda d: (bool(d['flags'] & 0x80), (d['direction'] & 3) >= 2))]
                else:
                    route = path_to(rooms, game.state()['room'], target)
                for door in route:
                    result = game.transition(door)
                    case['transitions'].append(dict(door=door, result=result))
                    print('door', hex(door['pointer']), hex(door['destination']), result['frames'], flush=True)
                game.step(60)
                if args.boss_assist and target in (0x9804, 0x9dc7, 0xa59f, 0xa98d, 0xcd13, 0xd95e, 0xda60, 0xb283, 0xb32e, 0xdd58):
                    boss = game.boss_assist(phase=args.boss_phase)
                    case['boss'] = boss
                    (game.output / f'boss-{target:04x}.json').write_text(json.dumps(boss, indent=2) + '\n')
                    print('boss', f'{target:04x}', boss['defeated'], flush=True)
                if args.collect_items or args.equipment_route:
                    case['items'] = game.collect(rom, game.active_items())
                elif metadata.get(f'{target:04x}', {}).get('items'):
                    case['items'] = game.collect(rom, metadata[f'{target:04x}']['items'])
                case['after'] = game.snapshot(f'room-{target:04x}')
                case['status'] = 'completed'
                print('room', f'{target:04x}', 'actual', f"{case['after']['room']:04x}",
                      'pose', case['after']['pose'], flush=True)
                if args.save_each_room:
                    game.command('savestate 1')
                    game.step(2)
                    shutil.copyfile(game.output / 'saves' / 'save1.sav', game.output / f'room-{target:04x}.sav')
                write_report()
        if args.endgame:
            rooms = rom.rooms()
            timeline = report.setdefault('endgame', dict(timeline=[]))['timeline']
            game.invincible = True
            game.write(0xd828, bytes([7] * 5 + [0, 0, 0]))  # Tourian boss alive
            game.write(0x9a2, struct.pack('<HHHH', 0xf32f, 0xf32f, 0x100b, 0x100f))
            door = min((d for room in rooms.values() for d in room['doors'] if d['destination'] == 0xdd58),
                       key=lambda d: (bool(d['flags'] & 0x80), (d['direction'] & 3) >= 2))
            report['endgame']['entry'] = game.transition(door)
            game.step(120)
            report['endgame']['mother_brain'] = game.mother_brain(timeline)
            game.snapshot('mother-brain-done')
            if not report['endgame']['mother_brain']:
                raise RuntimeError('Mother Brain did not set the escape event')
            for _ in range(60):  # death sequence and escape set-up
                if game.state()['state'] == 8 and game.word(0x943):  # timer running
                    break
                game.step(60)
            route = path_to(rooms, game.state()['room'], 0x91f8)
            report['endgame']['escape_rooms'] = [f"{d['destination']:04x}" for d in route]
            for door in route:
                game.transition(door)
            game.snapshot('escape-landing-site')
            # A real reloadable checkpoint for --board-ship iterations.
            game.command('savestate 1')
            game.step(2)
            shutil.copyfile(game.output / 'saves' / 'save1.sav', game.output / 'escape-landing-site.sav')
            report['endgame']['final_state'] = game.ride_gunship(timeline)
            game.snapshot('ending-final')
            if report['endgame']['final_state'] not in (0x26, 0x27):
                raise RuntimeError(f"Ending did not start: {report['endgame']['final_state']:#x}")
        if args.board_ship:
            timeline = report.setdefault('endgame', dict(timeline=[]))['timeline']
            report['endgame']['final_state'] = game.ride_gunship(timeline)
            game.snapshot('ending-final')
        if args.transition_probe:
            rooms = rom.rooms()
            current = game.state()['room']
            print('transition', json.dumps(game.transition(rooms[current]['doors'][0])), flush=True)
            print('settled', json.dumps(game.snapshot('transition-probe')), flush=True)
        report['status'] = 'completed'
    except Exception as error:
        failure = dict(error=str(error))
        report.update(status='failed', error=str(error))
        try:
            failure['state'] = game.snapshot('failure')
            failure['cpu'] = game.command('get_v2_cpu')
            failure['offrails'] = game.command('offrails_get')
        except Exception as diagnostic_error:
            failure['diagnostic_error'] = str(diagnostic_error)
        (game.output / 'failure.json').write_text(json.dumps(failure, indent=2) + '\n')
        raise
    finally:
        game.close()
        report['execution'] = getattr(game, 'final_execution', None)
        report['process_exit_code'] = game.process.returncode
        cases = report['cases']
        pickups = [item for case in cases for item in case.get('items', [])]
        report['results'] = dict(
            rooms_entered=sum(any(t['result']['after']['room'] == int(case['target'], 16)
                                  for t in case['transitions']) for case in cases),
            skipped=[case['target'] for case in cases if case['status'] == 'skipped'],
            pickup_types_acquired=sorted({item['item'] for item in pickups if item['acquired']}),
            pickups_unverified=[dict(room=case['target'], item=item['item'])
                                for case in cases for item in case.get('items', []) if not item['acquired']],
            bosses_completed=[dict(room=case['target'], method=case['boss']['method'])
                              for case in cases if case.get('boss', {}).get('defeated')])
        write_report()
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
