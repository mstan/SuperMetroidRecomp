#!/usr/bin/env python3
"""Isolated stock/wide controls for FPS determinism with a verified owned ROM.

No source-tree saves/configs are used. Artifacts are retained in a new directory.
WRAM CRC and sampled CPU registers are a regression probe, not a full oracle.
"""
from __future__ import annotations

import argparse
import csv
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import struct
import tempfile
import time


def require_xray_capture(path: Path) -> dict:
    """Prove the fixture reached X-ray, not merely a matching idle frame."""
    capture = path.read_bytes()
    if len(capture) != 15422488 or struct.unpack_from('<III', capture) != (0x534d5243, 2, 15422476):
        raise RuntimeError(f'{path}: expected complete v2 renderer capture')
    ram = 12 + 224 * 66656
    def word(address):
        return struct.unpack_from('<H', capture, ram + address)[0]
    result = dict(frozen=word(0xa78), phase=word(0xa7a), angle=word(0xa82),
                  half_width=word(0xa84), selected_item=word(0x9d2))
    if not result['frozen'] or result['phase'] != 2 or result['half_width'] != 10 or result['selected_item'] != 5:
        raise RuntimeError(f'{path}: fixture did not reach full X-ray beam: {result}')
    return result


def require_rotation_capture(directory: Path) -> dict:
    frames = []
    for name in ('previous', 'current'):
        path = directory / f'rotation.{name}.capture'
        data = path.read_bytes()
        if len(data) != 15422488 or struct.unpack_from('<III', data) != (0x534d5243, 2, 15422476):
            raise RuntimeError(f'{path}: invalid v2 rotation capture')
        ram = 12 + 224 * 66656
        raster = 12 + 100 * 66656
        matrix = struct.unpack_from('<hhhh', data, raster + 30)
        room = struct.unpack_from('<H', data, ram + 0x79b)[0]
        status = struct.unpack_from('<H', data, ram + 0x93f)[0]
        number = struct.unpack_from('<I', data, ram + 0x20000 + 256 * 224 * 4)[0]
        if room != 0xdf45 or not status & 0x8000 or data[raster] != 15 or data[raster + 4] & 7 != 7:
            raise RuntimeError(f'{path}: not full-bright Ceres escape Mode-7')
        frames.append(dict(frame=number, matrix=matrix, room=room))
    if frames[1]['frame'] != frames[0]['frame'] + 1 or frames[0]['matrix'] == frames[1]['matrix'] or abs(frames[1]['matrix'][1]) < 16:
        raise RuntimeError(f'{directory}: no adjacent changing rotation pair')
    return dict(previous=frames[0], current=frames[1])


def require_pose_change_capture(directory: Path) -> dict:
    frames = []
    for name in ('previous', 'current'):
        data = (directory / f'pose-change.{name}.capture').read_bytes()
        if len(data) != 15422488 or struct.unpack_from('<III', data) != (0x534d5243, 2, 15422476):
            raise RuntimeError(f'{directory}: invalid pose-change capture')
        ram = 12 + 224 * 66656
        objects = ram + 0x20000 + 256 * 224 * 4 + 9
        word = lambda address: struct.unpack_from('<H', data, objects + address)[0]
        frames.append(dict(frame=struct.unpack_from('<I', data, ram + 0x20000 + 256 * 224 * 4)[0],
                           pose=word(0xa1c), x=word(0xaf6), y=word(0xafa),
                           camera_x=word(0x911), camera_y=word(0x915),
                           graphics_x=word(0xb04), graphics_y=word(0xb06)))
        if data[objects + 0x20000] != 1:
            raise RuntimeError(f'{directory}: pose-change capture lacks published owner state')
    before, now = frames
    if now['frame'] != before['frame'] + 1 or now['pose'] == before['pose']:
        raise RuntimeError(f'{directory}: not an adjacent pose change: {frames}')
    if all(now[key] == before[key] for key in ('x', 'y', 'camera_x', 'camera_y')):
        raise RuntimeError(f'{directory}: pose changed without physical/camera motion')
    return dict(previous=before, current=now)


def require_intermediate_presents(directory: Path, first_frame: int) -> dict:
    intermediate = moving = 0
    previous = None
    samples = []
    with (directory / 'present.csv').open(newline='') as source:
        for row in csv.DictReader(source):
            frame, alpha = int(row['frame']), float(row['alpha'])
            if frame < first_frame or not 0 < alpha < 1:
                previous = None
                continue
            intermediate += 1
            if previous and previous['frame'] == row['frame'] and previous['crc32'] != row['crc32']:
                moving += 1
                if len(samples) < 8:
                    samples.append(dict(frame=frame, before_phase=float(previous['alpha']),
                                        after_phase=alpha, before_crc=previous['crc32'], after_crc=row['crc32']))
            previous = row
    if not intermediate or not moving:
        raise RuntimeError(f'{directory}: extra presents did not show intermediate pixel motion')
    return dict(intermediate_presents=intermediate, changing_same_frame_pairs=moving, samples=samples)


def require_state_roundtrip(directory: Path) -> dict:
    frames = {name: (directory / f'{name}.wram.bin').read_bytes()
              for name in ('expected-load', 'actual-load', 'reset-load', 'continued-input')}
    if any(len(frame) != 0x20000 for frame in frames.values()):
        raise RuntimeError(f'{directory}: incomplete state dump')
    if frames['expected-load'] != frames['actual-load'] or frames['actual-load'] != frames['reset-load']:
        raise RuntimeError(f'{directory}: save/load or reset/load changed the expected continuation')
    # Keep the input check independent of advancing RNG, timers, and counters.
    before = tuple(struct.unpack_from('<H', frames['actual-load'], address)[0] for address in (0xaf6, 0xafa))
    after = tuple(struct.unpack_from('<H', frames['continued-input'], address)[0] for address in (0xaf6, 0xafa))
    if before == after:
        raise RuntimeError(f'{directory}: no player movement after reload')
    return dict(exact_wram_continuation=True, before=before, after=after)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", type=Path, required=True)
    parser.add_argument("--rom", type=Path, required=True)
    parser.add_argument("--frames", type=int, default=3200)
    parser.add_argument("--modes", nargs='+', choices=['stock', 'wide', 'fps', 'fps_only'],
                        default=['stock', 'wide', 'fps', 'fps_only'])
    parser.add_argument("--fps", type=int, default=120,
                        choices=[60, 90, 120, 144, 165, 240, 360])
    parser.add_argument("--artifacts", type=Path, required=True)
    parser.add_argument("--script", type=Path)
    parser.add_argument("--capture-power-bomb", action="store_true",
                        help="capture effect phases and the actual DMA setup (diagnostic fixtures)")
    parser.add_argument("--capture-rooms", action="store_true",
                        help="capture the first stable bright frame in each room/mode reached")
    parser.add_argument("--capture-grapple", action="store_true",
                        help="require an adjacent-frame grapple diagnostic capture")
    parser.add_argument('--grapple-min-length', type=int, default=32, choices=range(1, 128))
    parser.add_argument('--capture-rotation', action='store_true',
                        help='require changing Ceres Mode-7 matrix captures')
    parser.add_argument("--require-xray", action="store_true",
                        help="require the final capture to contain an active full X-ray beam")
    parser.add_argument("--xray-angle", type=lambda value: int(value, 0),
                        help="also require this captured beam angle (decimal or 0x hex)")
    parser.add_argument("--audio", action="store_true")
    parser.add_argument('--capture-pose-change', action='store_true',
                        help='require an adjacent moving Samus pair with different poses')
    parser.add_argument('--motion-after-frame', type=int, default=1)
    parser.add_argument('--check-state-roundtrip', action='store_true',
                        help='check the high_fps_scene fixture save/load and reset/load dumps')
    parser.add_argument('--execution-policy', default='off',
                        choices=('off', 'on', 'force', 'verify', 'auto', 'lle', 'hle'))
    parser.add_argument('--run-ahead', type=int, choices=range(5), default=0,
                        help='shared runner range: 0..SNES_RUNAHEAD_MAX (4)')
    parser.add_argument('--require-wide-content', action='store_true',
                        help='fixture requires nonblack world pixels outside the native view')
    parser.add_argument('--min-simulation-hz', type=float,
                        help='require this wall-time delivery rate in paced fixture modes')
    parser.add_argument("--profile", action="store_true",
                        help="collect opt-in wall-time stage diagnostics")
    parser.add_argument("--profile-start-frame", type=int, default=1)
    parser.add_argument("--timeout", type=float, default=600)
    args = parser.parse_args()
    if args.modes[0] != 'stock' or len(set(args.modes)) != len(args.modes):
        parser.error('modes must start with stock and contain no duplicates')
    if 'fps' in args.modes and ('wide' not in args.modes or args.modes.index('wide') > args.modes.index('fps')):
        parser.error('the wide control must precede the wide FPS run')
    if args.require_xray and not any(mode in args.modes for mode in ('wide', 'fps')):
        parser.error('--require-xray requires a wide or fps capture mode')
    if args.xray_angle is not None and (not args.require_xray or not 0 <= args.xray_angle <= 256):
        parser.error('--xray-angle requires --require-xray and a value from 0 to 256')
    exe, rom = args.exe.resolve(strict=True), args.rom.resolve(strict=True)
    if args.frames < 1 or args.timeout <= 0:
        parser.error("frames and timeout must be positive")
    if args.motion_after_frame < 1:
        parser.error('motion-after-frame must be positive')
    if args.min_simulation_hz is not None and args.min_simulation_hz <= 0:
        parser.error('minimum simulation rate must be positive')
    if not 1 <= args.profile_start_frame <= args.frames:
        parser.error("profile start must be within the simulation run")
    root = args.artifacts.resolve()
    root.mkdir(parents=True, exist_ok=True)
    run_root = Path(tempfile.mkdtemp(prefix="sm-renderer-", dir=root))
    print(f"Artifacts: {run_root}", flush=True)
    script = args.script.resolve(strict=True) if args.script else None
    report: dict = {
        "frames": args.frames, "runs": {}, "passed": False,
        "exe": str(exe), "exe_sha256": hashlib.sha256(exe.read_bytes()).hexdigest(),
        "rom_sha256": hashlib.sha256(rom.read_bytes()).hexdigest(),
        "audio_enabled": args.audio, "requested_fps": args.fps,
        "profiling_enabled": args.profile,
        "profile_start_frame": args.profile_start_frame,
        "execution_policy": args.execution_policy,
        "run_ahead": args.run_ahead,
    }
    # Retain exact input identities even if the first runtime arm fails.
    (run_root / 'report.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    references = {}
    for index, name in enumerate(args.modes):
        directory = run_root / name
        directory.mkdir()
        paced = name in ("fps", "fps_only")
        wide = name in ("wide", "fps")
        (directory / "config.ini").write_text(
            "[General]\nAutosave=0\nSkipLauncher=1\n"
            f"DisableFrameDelay={0 if paced else 1}\n"
            "[Graphics]\nWindowScale=2\nNewRenderer=1\nNoSpriteLimits=0\n"
            f"[Sound]\nEnableAudio={int(args.audio)}\nVolume=0\n",
            encoding="utf-8")
        # Scripted presses own the guest. A user typing in another app must
        # not inject Start or a gamepad movement into a background QA run.
        with (directory / 'config.ini').open('a', encoding='utf-8') as config:
            config.write('\n[Controller]\nSourceP1=0\nSourceP2=0\nSourceP3=0\nSourceP4=0\n'
                         '[GamepadMap]\nEnableGamepad1=false\nEnableGamepad2=false\n'
                         'EnableGamepad3=false\nEnableGamepad4=false\n')
        (directory / "sm-video.ini").write_text(
            f"[SuperMetroidVideo]\nEnhancedRenderer={int(wide)}\n"
            f"Aspect=32:9\nPresentationEnabled={int(paced)}\n"
            f"PresentationFPS={args.fps}\nHudAnchored=1\n", encoding="utf-8")
        env = os.environ.copy()
        # Automated QA preserves desktop focus and emulates audio silently.
        # SDL2 and SDL3 use different spellings; set both explicitly.
        env.update(SDL_VIDEODRIVER='dummy', SDL_VIDEO_DRIVER='dummy',
                   SDL_AUDIODRIVER='dummy', SDL_AUDIO_DRIVER='dummy',
                   SM_REPLAY_RECORD='0')
        # Diagnostic overrides from an interactive shell must not contaminate
        # the controlled run. Each executable process owns its own directory.
        for key in list(env):
            if key.startswith(("SM_CAPTURE_", "SM_VIDEO_", "SM_STATE_", "SNESRECOMP_FRAME_BMP")):
                env.pop(key)
        env.pop("SNESRECOMP_WIDESCREEN", None)
        env.pop("SM_PROFILE", None)
        env.pop("SM_PROFILE_START_FRAME", None)
        env.pop("SNESRECOMP_HOST_PROFILE", None)
        env.pop("SNESRECOMP_HOST_PROFILE_START_FRAME", None)
        env.pop("SM_DMA_TRACE", None)
        env.pop("SM_XRAY_TILE_REPORT", None)
        if args.capture_power_bomb:
            env["SM_DMA_TRACE"] = "dma.csv"
            if wide:
                env["SM_CAPTURE_POWERBOMB_PREFIX"] = "powerbomb"
        if args.capture_rooms and wide:
            env['SM_CAPTURE_ROOMS_PREFIX'] = 'room'
        if args.capture_grapple and wide:
            env['SM_CAPTURE_GRAPPLE_PREFIX'] = 'grapple'
            env['SM_CAPTURE_GRAPPLE_MIN_LENGTH'] = str(args.grapple_min_length)
        if args.capture_rotation and wide:
            env['SM_CAPTURE_ROTATION_PREFIX'] = 'rotation'
        if args.capture_pose_change and name != 'stock':
            env['SM_CAPTURE_MOTION_PREFIX'] = 'pose-change'
            env['SM_CAPTURE_MOTION_POSE_CHANGE'] = '1'
            env['SM_CAPTURE_MOTION_AFTER'] = str(args.motion_after_frame)
            if paced:
                env['SNESRECOMP_PRESENT_LOG'] = 'present.csv'
        if args.profile:
            env["SNESRECOMP_HOST_PROFILE"] = "1"
            env["SNESRECOMP_HOST_PROFILE_START_FRAME"] = str(args.profile_start_frame)
        env["SM_RUN_FRAMES"] = str(args.frames)
        if args.check_state_roundtrip:
            env['SNESRECOMP_DUMP_DIR'] = str(directory)
        env["SM_STATE_TRACE"] = "state.csv"
        env["SNESRECOMP_DEBUG_PORT"] = '0'
        env['SNESRECOMP_EXECUTION_MODE'] = args.execution_policy
        env['SNESRECOMP_RUNAHEAD'] = str(args.run_ahead)
        env["SM_CAPTURE_FRAME"] = str(args.frames)
        env["SM_CAPTURE_PATH"] = "final.capture"
        env["SNESRECOMP_FRAME_BMP"] = "final.bmp"
        env["SNESRECOMP_FRAME_BMP_FRAME"] = str(args.frames)
        if args.check_state_roundtrip:
            # FRAME_BMP follows the restored guest counter. The host screenshot
            # follows the monotonic run index, so it still reaches the final
            # presentation after reset/load has rewound guest frame numbers.
            env['SNESRECOMP_SCREENSHOT'] = 'final.ppm'
            env['SNESRECOMP_SCREENSHOT_FRAME'] = str(args.frames)
        command = [str(exe), "--config", "config.ini"]
        if script:
            command += ["--script", str(script)]
        command.append(str(rom))
        started = time.monotonic()
        with (directory / "stdout.log").open("wb") as stdout, (directory / "stderr.log").open("wb") as stderr:
            process = subprocess.Popen(command, cwd=directory, env=env, stdout=stdout, stderr=stderr)
            try:
                while True:
                    try:
                        code = process.wait(timeout=20)
                        break
                    except subprocess.TimeoutExpired:
                        elapsed = time.monotonic() - started
                        print(f"{name}: pid={process.pid} live, {elapsed:.1f}s", flush=True)
                        if elapsed >= args.timeout:
                            raise TimeoutError(f"{name} exceeded {args.timeout}s")
            except BaseException:
                # Only terminate the exact child created by this test.
                process.terminate()
                try:
                    process.wait(timeout=10)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait()
                raise
        if code:
            raise RuntimeError(f"{name} exited {code}; see {directory}")
        trace = (directory / "state.csv").read_bytes()
        lines = trace.splitlines()
        if len(lines) != args.frames or [int(line.split(b",", 1)[0]) for line in lines] != list(range(1, args.frames + 1)):
            raise RuntimeError(f"{name}: missing/extra simulation records")
        log = (directory / "stderr.log").read_text(encoding="utf-8", errors="replace")
        if "[interp_cap]" in log:
            raise RuntimeError(f"{name}: interpreter cap reached; simulation run is invalid")
        totals = re.search(r"video totals: simulations=(\d+) presentations=(\d+) seconds=([\d.]+)", log)
        if not totals:
            raise RuntimeError(f"{name}: missing presentation accounting")
        sim, present, seconds = int(totals[1]), int(totals[2]), float(totals[3])
        if sim != args.frames:
            raise RuntimeError(f"{name}: simulation count differs")
        if paced and args.fps > 61 and present <= sim:
            raise RuntimeError("FPS run did not produce extra presentations")
        if args.check_state_roundtrip:
            magic, dimensions, maximum, rgb = (directory / 'final.ppm').read_bytes().split(b'\n', 3)
            w, h = map(int, dimensions.split())
            if magic != b'P6' or maximum != b'255' or len(rgb) != w * h * 3:
                raise RuntimeError(f'{name}: invalid final presentation screenshot')
            pixels = bytearray(w * h * 4)
            pixels[0::4], pixels[1::4], pixels[2::4] = rgb[2::3], rgb[1::3], rgb[0::3]
            header = bytearray(54)
            struct.pack_into('<2sI4xIIiiHH', header, 0, b'BM', 54 + len(pixels), 54, 40, w, -h, 1, 32)
            (directory / 'final.bmp').write_bytes(header + pixels)
        bitmap = (directory / "final.bmp").read_bytes()
        if args.capture_power_bomb and wide and not list(directory.glob("powerbomb.*.capture")):
            raise RuntimeError(f"{name}: fixture did not reach a power-bomb phase")
        if args.capture_rooms and wide and not list(directory.glob('room.*.capture')):
            raise RuntimeError(f'{name}: no stable gameplay room was captured')
        if args.capture_grapple and wide and not (directory / 'grapple.current.capture').is_file():
            raise RuntimeError(f'{name}: fixture did not reach grapple capture conditions')
        if args.capture_rotation and wide and not (directory / 'rotation.current.capture').is_file():
            raise RuntimeError(f'{name}: fixture did not reach changing Mode-7 rotation')
        if wide or paced:
            capture = (directory / 'final.capture').read_bytes()
            if len(capture) != 15422488 or struct.unpack_from('<III', capture) != (0x534d5243, 2, 15422476):
                raise RuntimeError(f'{name}: expected complete v2 live capture')
            if capture[15291413 + 0x20000] != 1:
                raise RuntimeError(f'{name}: live frame is missing its pre-NMI owner latch')
        if len(bitmap) < 54 or bitmap[:2] != b"BM":
            raise RuntimeError(f"{name}: missing/invalid presentation bitmap")
        width, height = struct.unpack_from("<ii", bitmap, 18)
        expected_width = 682 if wide else 256
        if width != expected_width or abs(height) != 224:
            raise RuntimeError(f"{name}: presentation is {width}x{height}, expected {expected_width}x224")
        digest = hashlib.sha256(trace).hexdigest()
        # Visible enemy activation is simulation geometry. Compare FPS on/off
        # at the same width; stock and wide are deliberately different packages.
        if not paced:
            references[wide] = trace
        elif trace != references[wide]:
            first = next(i + 1 for i, (a, b) in enumerate(zip(references[wide].splitlines(), lines)) if a != b)
            raise RuntimeError(f"{name}: game-state trace differs from its width control at simulation frame {first}")
        report["runs"][name] = {"trace_sha256": digest, "simulations": sim,
                               "presentations": present, "seconds": seconds,
                               "render_width": width, "render_height": abs(height),
                               "presentation_hz": present / seconds if seconds else 0}
        report['runs'][name]['reference_mode'] = 'wide' if wide else 'stock'
        if name == 'wide':
            report['runs'][name]['matches_stock_trace'] = trace == references[False]
        report['runs'][name]['simulation_hz'] = sim / seconds if seconds else 0
        if args.capture_pose_change and name != 'stock':
            report['runs'][name]['pose_change'] = require_pose_change_capture(directory)
            if paced and args.fps > 61:
                report['runs'][name]['intermediate_pixels'] = require_intermediate_presents(directory, args.motion_after_frame)
        if args.check_state_roundtrip:
            report['runs'][name]['state_roundtrip'] = require_state_roundtrip(directory)
        if paced and args.min_simulation_hz is not None and sim / seconds < args.min_simulation_hz:
            raise RuntimeError(f'{name}: simulation delivery {sim / seconds:.3f} Hz '
                               f'is below {args.min_simulation_hz:.3f} Hz')
        if wide and args.require_wide_content:
            if struct.unpack_from('<H', bitmap, 28)[0] != 32:
                raise RuntimeError('wide-content check requires a 32-bit BMP')
            start = struct.unpack_from('<I', bitmap, 10)[0]
            extra = (width - 256) // 2
            pixels = struct.unpack_from(f'<{width * abs(height)}I', bitmap, start)
            margin = sum(bool(pixels[(y if height < 0 else abs(height)-1-y)*width+x] & 0xffffff)
                         for y in range(30, abs(height)) for x in range(width)
                         if x < extra or x >= extra + 256)
            if margin < 64:
                raise RuntimeError(f'{name}: missing extended world content ({margin} pixels)')
            report['runs'][name]['extended_world_pixels'] = margin
        if args.require_xray and wide:
            report['runs'][name]['xray'] = require_xray_capture(directory / 'final.capture')
            if args.xray_angle is not None and report['runs'][name]['xray']['angle'] != args.xray_angle:
                raise RuntimeError(f'{name}: X-ray angle did not reach {args.xray_angle}')
        if args.capture_rotation and wide:
            report['runs'][name]['rotation'] = require_rotation_capture(directory)
        if args.profile:
            window = re.search(r"video profile window: first=(\d+) last=(\d+) seconds=([\d.]+) presentations=(\d+)", log)
            if not window or int(window[1]) != args.profile_start_frame or int(window[2]) != args.frames:
                raise RuntimeError(f"{name}: missing or incorrect profile window")
            report["runs"][name]["profile_window"] = {
                "first": int(window[1]), "last": int(window[2]),
                "seconds": float(window[3]), "presentations": int(window[4]),
                "presentation_hz": int(window[4]) / float(window[3]),
            }
            stages = re.findall(r"video profile: stage=(\S+) count=(\d+) total_ms=([\d.]+) mean_ms=([\d.]+) max_ms=([\d.]+) max_frame=(\d+)", log)
            required = {'guest', 'raster-capture', 'surface-acquire', 'compose',
                        'upload-present', 'state-trace'}
            if not required.issubset({s[0] for s in stages}) or len(stages) != len({s[0] for s in stages}):
                raise RuntimeError(f"{name}: missing stage profiling output")
            report["runs"][name]["profile"] = {
                stage: {"count": int(count), "total_ms": float(total),
                        "mean_ms": float(mean), "max_ms": float(maximum),
                        "max_frame": int(max_frame)}
                for stage, count, total, mean, maximum, max_frame in stages
            }
            for stage, values in report["runs"][name]["profile"].items():
                if stage not in required:
                    if values['count'] and not args.profile_start_frame <= values['max_frame'] <= args.frames:
                        raise RuntimeError(f'{name}: invalid {stage} profiling frame')
                    continue
                expected_count = args.frames - args.profile_start_frame + 1 if stage in (
                    "guest", "raster-capture", "state-trace") else int(window[4])
                if values["count"] != expected_count or not args.profile_start_frame <= values["max_frame"] <= args.frames:
                    raise RuntimeError(f"{name}: incorrect profile samples for {stage}")
        print(f"{name}: {sim} simulations, {present} presentations, {seconds:.3f}s, trace matches", flush=True)
        (run_root / "report.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    report["passed"] = True
    (run_root / "report.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    extra = "extra presentations verified" if args.fps > 61 and any(mode in args.modes for mode in ('fps', 'fps_only')) else "no extra-presentation requirement for selected modes/rate"
    print(f"PASS: identical state traces at matching simulation widths; {extra}.", flush=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
