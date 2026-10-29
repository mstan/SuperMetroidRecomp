#!/usr/bin/env python3
"""Real-ROM pause-tab, snapshot, run-ahead and graceful-exit regression gate."""
import argparse
import configparser
import hashlib
import json
from pathlib import Path
import tempfile

from PIL import Image
from sm_progression_qa import Session


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--exe', type=Path, required=True, help='current TCP-enabled build')
    parser.add_argument('--rom', type=Path, required=True)
    parser.add_argument('--artifacts', type=Path, required=True)
    args = parser.parse_args()
    args.artifacts.mkdir(parents=True, exist_ok=True)
    root = Path(tempfile.mkdtemp(prefix='sm-pause-options-', dir=args.artifacts.resolve()))
    report = {'passed': False, 'profile': str(root / 'profile'),
              'exe_sha256': hashlib.sha256(args.exe.read_bytes()).hexdigest()}
    session = Session(args.exe, args.rom, root / 'profile', wide=True, capture_profile=False)

    def tap(button):
        # Run-ahead changes the debug-visible future-frame offset once when
        # enabled. Drive host iterations and check the actual UI outcome.
        session.command('set_controller ' + button)
        session.command('step 1')
        session.command('set_controller 0')
        session.command('step 3')

    def cursor(label):
        session.command(f'screenshot {label}.bmp')
        image = Image.open(session.output / f'{label}.bmp').convert('RGB')
        counts = [sum(image.getpixel((x, 55 + row * 13)) == (43, 74, 140)
                      for x in range(10, image.width - 10)) for row in range(11)]
        selected = max(range(11), key=lambda row: counts[row])
        assert counts[selected] > 30, (label, image.size, counts)
        return selected

    try:
        session.boot()
        session.land()
        tap('start')
        session.step(120)
        assert session.state()['state'] == 15
        tap('l')
        # The open rides the host's native-style enter fade: 15 fast dim steps
        # to force-blank, 15 slow steps back, full 45 frames after the press,
        # with the panel hidden through the down phase and dimmed through the
        # up phase. Wait it out before expecting panel pixels.
        session.step(48)
        assert cursor('opened') == 0
        tap('down')
        tap('right')
        assert 'HudAnchored=0' in (session.output / 'sm-video.ini').read_text()
        tap('left')
        assert 'HudAnchored=1' in (session.output / 'sm-video.ini').read_text()
        for _ in range(4):
            tap('down')
        assert cursor('vsync') == 5
        tap('right')
        session.command('savestate 4')
        session.step(2)
        tap('up')
        assert cursor('moved-up') == 4
        session.command('loadstate 4')
        session.step(8)
        assert cursor('file-reopened') == 0
        report['file_panel_reopened_first_row'] = True
        for _ in range(7):
            tap('down')
        assert cursor('run-ahead-row') == 7
        tap('right')
        tap('down')
        assert cursor('run-ahead-cursor') == 8
        report['run_ahead_cursor_preserved'] = True
        # This profile is disposable; never write the owner's native saves.
        sentinel = b'OPTIONQA'
        session.command('write_sram 10 ' + sentinel.hex())
        tap('down')
        assert cursor('exit-row') == 9
        session.command('set_controller a')
        try:
            session.command('continue')
        except (OSError, ValueError):
            pass  # A queued SDL_QUIT may close the debug stream first.
        code = session.process.wait(timeout=15)
        assert code == 0, code
        config = configparser.ConfigParser()
        config.read(session.output / 'config.ini')
        assert config.getint('Graphics', 'VSync') == 1
        assert config.getint('General', 'RunAhead') == 1
        assert (session.output / 'saves/save.srm').read_bytes()[0x10:0x18] == sentinel
        report.update(passed=True, exit_code=code, config_persisted=True, sram_persisted=True)
    finally:
        session.close()
        (root / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
    print('PASS: pause tab, live settings, snapshot restore, run-ahead cursor, Exit and SRAM/config persistence')
    print(root / 'report.json')


if __name__ == '__main__':
    main()
