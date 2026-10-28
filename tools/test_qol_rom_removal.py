#!/usr/bin/env python3
"""Exercise converted QoL consumers using the built game and an owned ROM.

Uses new, isolated profiles (including saves/settings/catalog) below --out.
All launches use dummy video/audio. Retains raw traces and a JSON result.
This is gameplay regression coverage; interpreter qualification requires a
build emitted with SNESRECOMP_EMIT_AOT_DENY_GATE and --interp-targets.
"""
from __future__ import annotations

import argparse
import csv
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import socket
import subprocess
from concurrent.futures import ThreadPoolExecutor
try:
    import tomllib
except ModuleNotFoundError:
    import tomli as tomllib  # Python 3.10, also used by the engine's tools.

SOURCE = Path(__file__).resolve().parents[1]
ROM_SHA = "12b77c4bc9c1832cee8881244659065ee1d84c70c3d29e6eaf92e6798cc2ca72"
# The same original trace is CRLF on Windows and LF on Linux. Every byte
# otherwise matches; retain raw file identities without confusing line endings
# with a guest-state regression.
STOCK_TRACE = ("66976992eb0baea5b7c6696cd6f8e0def597797cb8880497204a58ac93cfc947" if os.name == "nt" else
               "63bf723ed7e919aad7df7cac4004ecff2b4943f94d1626cf77e110b2881cced5")


def state(features):
    catalog = {}
    for path in sorted((SOURCE / "mods/preloaded/packages").glob("*/1.0.0/manifest.toml")):
        package = tomllib.loads(path.read_text(encoding="utf8"))
        for feature in package.get("feature", []):
            catalog[package["id"] + "/" + feature["id"]] = package["version"]
    selected = {f.partition(":")[0]: f.partition(":")[2] for f in features}
    assert not selected.keys() - catalog.keys(), "unknown fixture feature"
    lines = ["format_version = 1\n"]
    for package in sorted({f.split("/")[0] for f in catalog}):
        version = next(version for name, version in catalog.items() if name.startswith(package + "/"))
        lines += [f'[[package]]\nid = "{package}"\nversion = "{version}"\n']
    # Explicitly disable unselected features, including correctness fixes
    # whose normal user-facing defaults may be enabled.
    for feature in sorted(catalog):
        package, name = feature.split("/")
        enabled = "true" if feature in selected else "false"
        options = selected.get(feature, "")
        lines += [f'[[feature]]\npackage_id = "{package}"\nid = "{name}"\nenabled = {enabled}\n']
        if options:
            lines += ["[feature.values]\n"]
            for option in options.split(","):
                key, value = option.split("=", 1)
                lines += [f'{key} = "{value}"\n']
    return "\n".join(lines)


def prefix():
    # Blank SRAM -> Ceres, then the same stock Landing Site load used by the
    # original movement scenarios. No completion/result flags are set.
    return ("wait 210\npress start 8\nwait 120\npress start 8\nwait 360\n"
            "press start 12\nwait 240\n" + "press a 8\nwait 300\n" * 27 +
            "until16 998 == 8 20000\nwait 60\npoke d914 2200\n"
            "poke 998 0600\nuntil16 79b == 91f8 3000\n"
            "until16 998 == 8 3000\nwait 700\n")


def read_rows(path):
    with path.open() as f:
        return [{k: int(v, 10 if k == "frame" else 16) for k, v in r.items()}
                for r in csv.DictReader(f)]


def run_case(case, *, build, rom, out, interp_targets=None, screenshot_frame=None, env_overrides=None):
    exe_name = "SuperMetroidSNESRecomp" + (".exe" if os.name == "nt" else "")
    config = (SOURCE / "config.ini").read_text(encoding="utf8")
    config = config.replace("DisableFrameDelay = 0", "DisableFrameDelay = 1")
    config = config.replace("Volume = 100", "Volume = 0").replace("Fullscreen = 1", "Fullscreen = 0")
    tag, features, script, fields = case
    profile = out / tag; profile.mkdir()
    (profile / "config.ini").write_text(config, encoding="utf8")
    shutil.copytree(SOURCE / "mods/preloaded/packages", profile / "mods/preloaded/packages")
    (profile / "mods/preloaded/state.toml").write_text(state(features), encoding="utf8")
    env = dict(os.environ)
    # Offline cases must not inherit another session's networking/test knobs.
    # A peer fixture supplies its explicit local setup through env_overrides.
    for key in list(env):
        if key.startswith(("SNES_NET", "SNES_RB", "RNET_RB_", "RNET_SIM_", "RBE_RB_")):
            env.pop(key)
    for key in ("SM_RUN_FRAMES", "SNESRECOMP_LLE_INTERP_TARGET_FILE", "SM_CAPTURE_PATH", "SM_CAPTURE_FRAME",
                "SNESRECOMP_SCREENSHOT", "SNESRECOMP_SCREENSHOT_FRAME", "SNESRECOMP_SCREENSHOT_DIR",
                "SNESRECOMP_SCREENSHOT_FROM", "SNESRECOMP_SCREENSHOT_TO"):
        env.pop(key, None)
    env.update(SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="dummy", SNESRECOMP_NO_LAUNCHER="1",
               SM_REPLAY_RECORD="0", SM_STATE_TRACE=str(profile / "state.csv"),
               SM_WRAM_PROBE=str(profile / "wram.csv"), SM_WRAM_PROBE_ADDRS=fields)
    # TRACE builds bind a debugger even for scripted runs. Use a private port
    # so parallel cases cannot collide with each other or an owner playtest.
    with socket.socket() as reservation:
        reservation.bind(('127.0.0.1', 0))
        env['SNESRECOMP_DEBUG_PORT'] = str(reservation.getsockname()[1])
    if interp_targets: env["SNESRECOMP_LLE_INTERP_TARGET_FILE"] = str(interp_targets.resolve())
    if screenshot_frame is not None:
        env["SNESRECOMP_SCREENSHOT"] = str(profile / "presented.ppm")
        env["SNESRECOMP_SCREENSHOT_FRAME"] = str(screenshot_frame)
    if env_overrides: env.update(env_overrides)
    executable = build / exe_name
    command = [str(executable), "--config", str(profile / "config.ini")]
    if script is None: env["SM_RUN_FRAMES"] = "3200"
    else:
        (profile / "scenario.txt").write_text(script, encoding="utf8")
        command += ["--script", str(profile / "scenario.txt")]
    command += [str(rom)]
    with (profile / "stdout.log").open("w") as stdout, (profile / "stderr.log").open("w") as stderr:
        result = subprocess.run(command, cwd=profile, env=env, stdout=stdout, stderr=stderr, timeout=300)
    log = (profile / "stderr.log").read_text(errors="replace")
    assert result.returncode == 0, f"{tag}: process exit {result.returncode}"
    assert not re.search(r"TIMEOUT|\bFAILED\b|refused|step.cap|\[interp_cap\]|\[brk\]|LLE loop bailed|scheduler bail|abort|hook table full|registry full|no mod_hook", log, re.I), f"{tag}: inspect stderr"
    info = {"exit": result.returncode, "command": command, "features": features,
            "interp_targets": str(interp_targets) if interp_targets else None,
            "exe_sha256": hashlib.sha256(executable.read_bytes()).hexdigest(),
            "trace_sha256": hashlib.sha256((profile / "state.csv").read_bytes()).hexdigest()}
    report_path = profile / "last_run_report.json"
    if report_path.exists():
        report = json.loads(report_path.read_text())
        info["execution_share"] = report.get("execution_share")
        info["tier2_coverage"] = report.get("tier2_coverage")
    if screenshot_frame is not None:
        screenshot = profile / "presented.ppm"
        assert screenshot.is_file(), f"{tag}: requested presented frame was not captured"
        info["presented_sha256"] = hashlib.sha256(screenshot.read_bytes()).hexdigest()
    (profile / "identity.json").write_text(json.dumps(info, indent=2))
    print(tag + " completed", flush=True)
    return tag, info


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--build", type=Path, required=True)
    ap.add_argument("--rom", type=Path, required=True)
    ap.add_argument("--out", type=Path, required=True)
    ap.add_argument("--baseline", type=Path, help="optional original build: stock trace only")
    ap.add_argument("--interp-targets", type=Path,
                    help="deny list for a QA build emitted with AOT deny gates")
    ap.add_argument("--jobs", type=int, default=2)
    a = ap.parse_args()
    a.build = a.build.resolve(); a.rom = a.rom.resolve(); a.out = a.out.resolve()
    assert hashlib.sha256(a.rom.read_bytes()).hexdigest() == ROM_SHA, "wrong source ROM"
    a.out.mkdir(parents=True, exist_ok=False)
    probe = "079B:2,0998:2,0AF6:2,0AFA:2,0B2E:2,0B32:2,0B36:2,0A58:2,0A1C:2,183E:2,1840:2,00B1:2,00B3:2,00B5:2,00B7:2"
    cases = [("stock", [], None, probe)]
    walk = prefix() + "press right 70\nwait 30\n"
    for mode in ("stock", "heavy"):
        cases.append(("jump-" + mode, [] if mode == "stock" else ["super-metroid.movement/heavy-physics"],
                      walk + "press a 60\nwait 60\nquit\n", probe))
    for mode in ("stock", "half", "off"):
        script = prefix() + "".join(f"poke 183e {t:02x}00\npoke 1840 3000\nwait 24\n" for t in (8, 17, 35)) + "quit\n"
        features = [] if mode == "stock" else ["super-metroid.pacing/screen-shake:shake=" + mode]
        cases.append(("shake-" + mode, features, script, probe))
    # Place a genuine station instruction list in the first PLM slot. The
    # stock activation, message copier, DMA and refill code must do the work.
    station = prefix() + "".join(f"poke {addr:x} {value:02x}00\n" for addr, value in
                                 ((0x9c6, 0), (0x9c8, 5), (0x9ca, 0), (0x9cc, 3), (0x9ce, 0), (0x9d0, 2)))
    station += ("poke 1d27 8aae\npoke 1cd7 d086\npoke 1c87 0000\npoke de1c 0100\n"
                "poke 1c37 dbee\nwait 40\npress b 4\nwait 200\npress b 4\nwait 100\nquit\n")
    tile_probe = ",".join(f"{addr:04x}:2" for addr in
                          (0x079b, 0x0998, 0x1c1f, 0x9c6, 0x9ca, 0x9ce, *range(0x324c, 0x3270, 2)))
    for mode in ("stock", "ammo"):
        cases.append(("station-" + mode, [] if mode == "stock" else ["super-metroid.balance-fixes/missile-station"], station, tile_probe))
    for direction, commands in (("horizontal", "press right+a 6\npress up 15\nwait 2\npress down 10\nwait 60\n"),
                                ("vertical", "wait 40\npress right 15\nwait 60\n"),
                                ("diagonal", "press a+r 40\npress down 15\nwait 60\n")):
        for mode in ("stock", "steer"):
            features = [] if mode == "stock" else ["super-metroid.movement/shinespark-steering"]
            cases.append((direction + "-" + mode, features,
                          walk + "poke a68 b400\npoke acc 0100\npress a 2\n" + commands + "quit\n", probe))

    def run(case, build=None):
        return run_case(case, build=build or a.build, rom=a.rom, out=a.out,
                        interp_targets=a.interp_targets if build is None else None)

    with ThreadPoolExecutor(max_workers=max(1, a.jobs)) as pool:
        identities = dict(pool.map(run, cases))
    result = {"rom_sha256": ROM_SHA, "identities": identities, "interpreter_targets": str(a.interp_targets) if a.interp_targets else None}
    assert identities["stock"]["trace_sha256"] == STOCK_TRACE, "disabled mods changed stock behavior"
    if a.baseline:
        _, baseline = run(("baseline", [], None, probe), a.baseline.resolve())
        assert baseline["trace_sha256"] == identities["stock"]["trace_sha256"]
        result["baseline"] = baseline
    jumps = {}
    for mode in ("stock", "heavy"):
        data = read_rows(a.out / ("jump-" + mode) / "wram.csv")
        window = data[-124:]; ground = window[0]["0afa"]
        first = next(r for r in window if r["0b36"] == 1 and r["0b2e"] > 0 and r["0afa"] < ground)
        land = next(r for r in window if r["frame"] > first["frame"] and r["0afa"] >= ground)
        assert first["079b"] == 0x91f8
        jumps[mode] = {"launch_speed": first["0b2e"], "gravity": first["0b32"], "airtime": land["frame"] - first["frame"]}
    assert jumps["stock"]["launch_speed"] == 4 and jumps["heavy"]["launch_speed"] == 6
    assert jumps["stock"]["gravity"] == 0x1c00 and jumps["heavy"]["gravity"] == 0x3400
    assert jumps["heavy"]["airtime"] < jumps["stock"]["airtime"]
    result["jumps"] = jumps
    shake = {mode: read_rows(a.out / ("shake-" + mode) / "wram.csv") for mode in ("stock", "half", "off")}
    assert all(len(data) == len(shake["stock"]) for data in shake.values())
    checked = 0
    for stock, half, off in zip(shake["stock"], shake["half"], shake["off"]):
        assert stock["1840"] == half["1840"] == off["1840"], "quake duration changed"
        if stock["183e"] not in (8, 17, 35) or not stock["1840"]: continue
        for key in ("00b1", "00b3", "00b5", "00b7"):
            delta = (stock[key] - off[key] + 32768) % 65536 - 32768
            reduced = (half[key] - off[key] + 32768) % 65536 - 32768
            assert (delta, reduced) in ((0, 0), (3, 2), (-3, -2)), "incorrect shake amplitude"
            checked += abs(delta) > 0
    assert checked >= 48
    result["shake_offsets_checked"] = checked
    for mode in ("stock", "ammo"):
        data = read_rows(a.out / ("station-" + mode) / "wram.csv")
        row = data[-1]
        assert row["079b"] == 0x91f8 and row["1c1f"] == 0x16
        assert [row[k] for k in ("09c6", "09ca", "09ce")] == ([5, 3, 2] if mode == "ammo" else [5, 0, 0])
        text = "".join(" " if row[f"{addr:04x}"] == 0x3c4e else chr(row[f"{addr:04x}"] - 0x3ce0 + ord("A")) for addr in range(0x324c, 0x3270, 2))
        assert text == ("   AMMO RELOAD    " if mode == "ammo" else "  MISSILE RELOAD  "), repr(text)
        result["station-" + mode] = text
    steering = {}
    for direction in ("horizontal", "vertical", "diagonal"):
        samples = {}
        for mode in ("stock", "steer"):
            data = read_rows(a.out / (direction + "-" + mode) / "wram.csv")
            # A pose is selected one frame before its movement handler is
            # installed. Count executions with an actual spark handler.
            active = [r for r in data if 0xc9 <= r["0a1c"] <= 0xce and
                      r["0a58"] in (0xd0ab, 0xd106, 0xd0d7)]
            assert len(active) >= 10, f"{direction}/{mode}: spark never ran"
            samples[mode] = data
            if mode == "steer": assert {r["0a58"] for r in active} == {0xd0d7}
        key = "0af6" if direction == "vertical" else "0afa"
        assert any(s[key] != m[key] for s, m in zip(samples["stock"], samples["steer"])), f"{direction}: no steering movement"
        steering[direction] = "spark active and steering changes the perpendicular coordinate"
    result["steering"] = steering
    assert hashlib.sha256(a.rom.read_bytes()).hexdigest() == ROM_SHA
    (a.out / "results.json").write_text(json.dumps(result, indent=2))
    print(json.dumps({k: v for k, v in result.items() if k != "identities"}, indent=2))


if __name__ == "__main__":
    main()
