#!/usr/bin/env python3
"""Inspect or replay a Super Metroid diagnostic with the matching runtime."""
import argparse
from datetime import datetime
import json
import math
import os
from pathlib import Path
import shutil
import subprocess
import sys

SIDECARS = ("", ".sav", ".config.ini", ".mods.toml", ".mods.txt")


def digest(data):
    value = 2166136261
    for byte in data:
        value = ((value ^ byte) * 16777619) & 0xffffffff
    return value


def inspect(path):
    if path.stat().st_size > 2 * 1024 * 1024:
        raise ValueError("oversized replay segment")
    lines = path.read_text(encoding="utf-8").splitlines()
    # Format 3 adds a MODS line: the gameplay-mod set's identity.
    mods = lines[0] == "SMREPLAY 3"
    header = 5 if mods else 4
    if (len(lines) < header or lines[0] not in ("SMREPLAY 1", "SMREPLAY 2", "SMREPLAY 3") or
            len(lines[1].split()) != 5 or not lines[1].startswith("BUILD ") or
            len(lines[2].split()) != 2 or not lines[2].startswith("ROM ") or
            (mods and (len(lines[3].split()) != 2 or not lines[3].startswith("MODS "))) or
            lines[header - 1] != "MODE lle"):
        raise ValueError("unsupported or truncated replay header")
    activity = lines[0] in ("SMREPLAY 2", "SMREPLAY 3")
    if not 0 <= int(lines[2].split()[1], 16) <= 0xffffffff:
        raise ValueError("invalid ROM identity")
    state_path = path.with_name(path.name + ".sav")
    if state_path.stat().st_size > 16 * 1024 * 1024:
        raise ValueError("oversized machine checkpoint sidecar")
    frames = checkpoints = 0
    pending = raster_pending = checkpoint = False
    next_frame = None
    with state_path.open("rb") as states:
        for number, line in enumerate(lines[header:], header + 1):
            fields = line.split()
            try:
                if fields[0] == "S" and len(fields) == 4 and not pending and not raster_pending:
                    offset, size, expected = int(fields[1]), int(fields[2]), int(fields[3], 16)
                    if not (0 <= offset <= 16 * 1024 * 1024 and 0 < size <= 2 * 1024 * 1024
                            and offset + size <= state_path.stat().st_size):
                        raise ValueError("invalid checkpoint extent")
                    states.seek(offset)
                    if digest(states.read(size)) != expected:
                        raise ValueError("damaged machine checkpoint")
                    checkpoints += 1
                    checkpoint = True
                    next_frame = None
                elif fields[0] == "I" and len(fields) == (7 if activity else 6) and not pending and not raster_pending and checkpoint:
                    frame, word, flags = int(fields[1]), int(fields[2], 16), int(fields[3], 16)
                    aspect, fps = map(int, fields[4:6])
                    if activity and not 0 <= int(fields[6]) <= 214:
                        raise ValueError("invalid simulation viewport")
                    if not (0 <= frame <= 0xffffffff and 0 <= word <= 0xffffffff and
                            0 <= flags <= 63 and 0 <= aspect < 5 and
                            fps in (0, 60, 90, 120, 144, 165, 240, 360) and
                            (next_frame is None or next_frame == frame)):
                        raise ValueError("invalid input/settings or discontinuity without checkpoint")
                    next_frame = (frame + 1) & 0xffffffff
                    pending = True
                elif fields[0] == "H" and len(fields) == 6 and pending:
                    if int(fields[1]) != next_frame or any(not 0 <= int(h, 16) <= 0xffffffff for h in fields[2:]):
                        raise ValueError("invalid completed-frame check")
                    pending = False
                    raster_pending = True
                elif fields[0] == "V" and len(fields) == 4 and raster_pending:
                    width, ratio = int(fields[2]), float(fields[3])
                    if not (int(fields[1]) == next_frame and 256 <= width <= 684 and
                            width % 2 == 0 and math.isfinite(ratio) and 1 <= ratio <= 4):
                        raise ValueError("invalid raster geometry")
                    frames += 1
                    raster_pending = False
                else:
                    raise ValueError("unexpected replay record")
            except (IndexError, ValueError) as error:
                raise ValueError(f"record {number}: {error}") from error
    if not checkpoints or (not frames and not pending and not raster_pending):
        raise ValueError("replay has no input executions")
    print(f"{lines[1]}; {frames} completed executions, {checkpoints} machine checkpoints")
    if pending or raster_pending:
        print("Final input is incomplete: playback will execute it to reproduce the hang/crash and return failure if it completes.")
    return lines


def main():
    # A redirected Windows console may use cp1252 while runtime diagnostics
    # contain Unicode. Preserve the UTF-8 replay.log and never fail a valid
    # reproduction just because its console cannot display one character.
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(errors="replace")
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("replay", type=Path)
    parser.add_argument("--check", action="store_true", help="validate records and checkpoint data without launching")
    parser.add_argument("--exe", type=Path, help="the exact recording runtime and platform")
    parser.add_argument("--rom", type=Path, help="your own matching Super Metroid JU ROM")
    parser.add_argument("--work-dir", type=Path, help="new directory for reproduction; defaults beside the recording")
    parser.add_argument("--headless", action="store_true", help="full-speed SDL dummy video and audio")
    parser.add_argument("--mute", action="store_true", help="mute visible reproduction audio")
    parser.add_argument("--until", type=int, help="stop after N completed executions, preserving restored checkpoints")
    parser.add_argument("--timeout", type=float, default=120, help="maximum replay seconds (default 120)")
    args = parser.parse_args()
    source = args.replay.resolve(strict=True)
    lines = inspect(source)
    if args.check:
        return 0
    if not args.exe or not args.rom:
        parser.error("playback requires --exe and --rom")
    if args.timeout <= 0:
        parser.error("--timeout must be positive")
    exe, rom = args.exe.resolve(strict=True), args.rom.resolve(strict=True)
    work = (args.work_dir or source.parent / "reproductions" / datetime.now().strftime("sm-%Y%m%d-%H%M%S-%f")).resolve()
    work.mkdir(parents=True, exist_ok=False)
    print(f"Reproduction directory: {work}", flush=True)
    for suffix in SIDECARS:
        src = source.with_name(source.name + suffix)
        if src.is_file():
            shutil.copy2(src, work / ("capture.replay" + suffix))
    replay = work / "capture.replay"
    if args.until is not None:
        if args.until <= 0:
            parser.error("--until must be positive")
        completed = 0
        for i, line in enumerate(lines):
            if line.startswith("V "):
                completed += 1
                if completed == args.until:
                    replay.write_text("\n".join(lines[:i+1]) + "\n", encoding="utf-8")
                    break
        else:
            parser.error("--until exceeds the number of completed executions")
    config = source.with_name(source.name + ".config.ini")
    if not config.is_file():
        raise ValueError("missing .replay.config.ini sidecar")
    config_text = config.read_text(encoding="utf-8-sig")
    # Presentation-only overrides; guest renderer choices are restored per input.
    config_text += "\n[General]\nRunAhead=0\nAutosave=0\n[Graphics]\nFullscreen=0\n"
    if args.headless:
        config_text += "\n[General]\nDisableFrameDelay=1\n[Graphics]\nRenderer=software\nOutputMethod=SDL\nVSync=0\n"
    (work / "config.ini").write_text(config_text, encoding="utf-8")
    env = {key: value for key, value in os.environ.items()
           if not key.startswith(("SM_", "SNESRECOMP_", "SNES_NET"))}
    env.update(SM_REPLAY_RECORD="0", SM_REPLAY_DIR=str(work), SNESRECOMP_RUNAHEAD="0",
               SNESRECOMP_EXECUTION_MODE="lle", SNESRECOMP_DEBUG_PORT="0")
    if args.headless or args.mute:
        env["SDL_AUDIODRIVER"] = env["SDL_AUDIO_DRIVER"] = "dummy"
    if args.headless:
        env["SDL_VIDEODRIVER"] = env["SDL_VIDEO_DRIVER"] = "dummy"
    command = [str(exe), "--rom", str(rom), "--config", str(work / "config.ini"),
               "--no-launcher", "--replay-diagnostic", str(replay)]
    # Portable packages may keep SDL beside the executable.
    env["LD_LIBRARY_PATH"] = str(exe.parent) + os.pathsep + env.get("LD_LIBRARY_PATH", "")
    with (work / "replay.log").open("wb") as log:
        try:
            code = subprocess.run(command, cwd=work, env=env, stdout=log,
                                  stderr=subprocess.STDOUT, timeout=args.timeout).returncode
        except subprocess.TimeoutExpired:
            code = 124
            print("Replay timed out; the original hang may have reproduced. See replay.log.", flush=True)
    (work / "reproduction.json").write_text(json.dumps(dict(source=str(source), command=command, returncode=code), indent=2))
    print((work / "replay.log").read_text(encoding="utf-8", errors="replace"))
    return code


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, ValueError) as error:
        sys.exit(str(error))
