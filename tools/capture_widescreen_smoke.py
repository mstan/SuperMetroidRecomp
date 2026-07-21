#!/usr/bin/env python3
"""Regenerate Super Metroid widescreen smoke capture artifacts.

The normal CTest smoke gate validates local BMP/WRAM artifacts when they exist.
This helper drives the existing headless script/framedump hooks to recreate
those artifacts from the ROM. The lava capture is deliberately opt-in because
it runs to frame 31013.
"""

from __future__ import annotations

import argparse
import os
import subprocess
import sys
import time
from dataclasses import dataclass
from pathlib import Path


BOOT_TO_GAME = """\
wait 210
press start 8
wait 120
press start 8
wait 360
press start 12
wait 240
press a 12
wait 900
press a 12
wait 900
press a 12
"""

ITEM_SETUP = """\
forcepoke 09a2 37f337f30f100f10
forcepoke 09aa 00080004000200014000800000800040002020001000
forcepoke 09c2 bc02bc02640064001400140014001400
"""


@dataclass(frozen=True)
class Scenario:
    name: str
    out_dir: str
    bmp_start: int
    bmp_end: int
    bmp_step: int
    wram_start: int
    wram_end: int
    timeout_seconds: int
    script: str | None = None

    @property
    def target_wram(self) -> str:
      return f"frame_{self.wram_end:06d}_wram.bin"

    def expected_bmps(self) -> list[str]:
      return [
          f"frame_{frame:06d}.bmp"
          for frame in range(self.bmp_start, self.bmp_end + 1, self.bmp_step)
      ]


SCENARIOS = {
    "map_hud": Scenario(
        name="map_hud",
        out_dir="_codex_sm_ws_fresh_map_hud_20260721",
        bmp_start=4200,
        bmp_end=4920,
        bmp_step=720,
        wram_start=4920,
        wram_end=4920,
        timeout_seconds=120,
        script=BOOT_TO_GAME + """\
wait 1800
press start 8
wait 900
"""),
    "xray": Scenario(
        name="xray",
        out_dir="_codex_sm_ws_fresh_xray_ability_20260721",
        bmp_start=4050,
        bmp_end=4050,
        bmp_step=60,
        wram_start=4050,
        wram_end=4050,
        timeout_seconds=120,
        script=BOOT_TO_GAME + """\
wait 800
wait 280
""" + ITEM_SETUP + """\
forcepoke 09d2 0500
forcepoke 0a04 000000000000000000000000
wait 10
press right 30
wait 10
press b 180
wait 180
"""),
    "grapple": Scenario(
        name="grapple",
        out_dir="_codex_sm_ws_fresh_grapple_ability_20260721",
        bmp_start=3960,
        bmp_end=3960,
        bmp_step=60,
        wram_start=3949,
        wram_end=3949,
        timeout_seconds=150,
        script=BOOT_TO_GAME + """\
wait 800
wait 280
""" + ITEM_SETUP + """\
forcepoke 09d2 0400
forcepoke 0a04 000000000000000000000000
wait 10
press right 30
wait 10
poke 0d32 1ec5
wait 1
press x 90
wait 180
"""),
    "powerbomb": Scenario(
        name="powerbomb",
        out_dir="_codex_sm_ws_powerbomb_natural_morph_boot_20260721",
        bmp_start=3980,
        bmp_end=3980,
        bmp_step=60,
        wram_start=3980,
        wram_end=3980,
        timeout_seconds=120,
        script=BOOT_TO_GAME + """\
wait 800
wait 280
""" + ITEM_SETUP + """\
forcepoke 09d2 0300
forcepoke 0a04 000000000000000000000000
forcepoke 0a1c 1d000404
wait 20
press x 8
wait 180
"""),
    "attract": Scenario(
        name="attract",
        out_dir="_codex_sm_ws_fresh_attract4_20260721",
        bmp_start=18000,
        bmp_end=18000,
        bmp_step=60,
        wram_start=18000,
        wram_end=18000,
        timeout_seconds=300),
    "attract_sweep": Scenario(
        name="attract_sweep",
        out_dir="_codex_sm_ws_attract_long3_20260721",
        bmp_start=13200,
        bmp_end=24000,
        bmp_step=1200,
        wram_start=24000,
        wram_end=24000,
        timeout_seconds=360),
    "attract_sweep_extended": Scenario(
        name="attract_sweep_extended",
        out_dir="_codex_sm_ws_attract_extended_20260721",
        bmp_start=25200,
        bmp_end=48000,
        bmp_step=2400,
        wram_start=48000,
        wram_end=48000,
        timeout_seconds=720),
    "lava": Scenario(
        name="lava",
        out_dir="_codex_sm_ws_fresh_lava31013_20260721",
        bmp_start=31013,
        bmp_end=31013,
        bmp_step=60,
        wram_start=31013,
        wram_end=31013,
        timeout_seconds=600),
}


DEFAULT_SCENARIOS = ("map_hud", "xray", "grapple", "powerbomb", "attract")

SCENARIO_CHECKS = {
    "map_hud": ("hud", "map"),
    "xray": ("xray",),
    "grapple": ("grapple",),
    "powerbomb": ("powerbomb",),
    "attract": ("attract",),
    "attract_sweep": ("attract_sweep",),
    "attract_sweep_extended": ("attract_sweep_extended",),
    "lava": ("lava",),
}


def default_root() -> Path:
    return Path(str(__file__).replace("\\", "/")).resolve().parent.parent


def resolve_file(root: Path, value: str | None, default: str) -> Path:
    path = Path(value) if value else root / default
    return path if path.is_absolute() else root / path


def clean_scenario_outputs(out_dir: Path, scenario: Scenario) -> None:
    paths = [
        out_dir / "stdout.log",
        out_dir / "stderr.log",
        out_dir / scenario.target_wram,
        out_dir / f"frame_{scenario.wram_end:06d}.json",
    ]
    paths.extend(out_dir / "frames" / bmp for bmp in scenario.expected_bmps())
    for path in paths:
      try:
        path.unlink()
      except FileNotFoundError:
        pass


def run_scenario(root: Path, exe: Path, config: Path, rom: Path,
                 scenario: Scenario) -> None:
    out_dir = root / scenario.out_dir
    frames_dir = out_dir / "frames"
    out_dir.mkdir(parents=True, exist_ok=True)
    frames_dir.mkdir(parents=True, exist_ok=True)
    clean_scenario_outputs(out_dir, scenario)

    command = [
        str(exe),
        "--config", str(config),
    ]
    if scenario.script is not None:
      script_path = out_dir / f"{scenario.name}_capture_script.txt"
      script_path.write_text(scenario.script, encoding="ascii")
      command.extend(["--script", str(script_path)])
    command.extend(["--framedump", str(out_dir), str(rom)])

    env = os.environ.copy()
    env.update({
        "SNESRECOMP_FRAME_BMP_DIR": str(frames_dir),
        "SNESRECOMP_FRAME_BMP_START": str(scenario.bmp_start),
        "SNESRECOMP_FRAME_BMP_END": str(scenario.bmp_end),
        "SNESRECOMP_FRAME_BMP_STEP": str(scenario.bmp_step),
        "SNESRECOMP_FRAMEDUMP_START": str(scenario.wram_start),
        "SNESRECOMP_FRAMEDUMP_END": str(scenario.wram_end),
    })

    target = out_dir / scenario.target_wram
    stdout = (out_dir / "stdout.log").open("w", encoding="utf-8")
    stderr = (out_dir / "stderr.log").open("w", encoding="utf-8")
    proc = subprocess.Popen(
        command, cwd=root, env=env, stdout=stdout, stderr=stderr)
    try:
      deadline = time.monotonic() + scenario.timeout_seconds
      while time.monotonic() < deadline:
        if target.exists():
          break
        if proc.poll() is not None:
          break
        time.sleep(0.5)
      if not target.exists():
        raise RuntimeError(
            f"{scenario.name}: missing {target} after capture; "
            f"see {out_dir / 'stderr.log'}")
      missing_bmps = [
          frames_dir / bmp for bmp in scenario.expected_bmps()
          if not (frames_dir / bmp).exists()
      ]
      if missing_bmps:
        missing = ", ".join(str(path) for path in missing_bmps)
        raise RuntimeError(
            f"{scenario.name}: missing BMP capture(s): {missing}; "
            f"see {out_dir / 'stderr.log'}")
    finally:
      if proc.poll() is None:
        proc.terminate()
        try:
          proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
          proc.kill()
          proc.wait(timeout=5)
      stdout.close()
      stderr.close()


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=Path, default=default_root())
    parser.add_argument("--exe")
    parser.add_argument("--config")
    parser.add_argument("--rom")
    parser.add_argument("--include-long", action="store_true",
                        help="include the long lava capture at frame 31013")
    parser.add_argument("--scenario", action="append",
                        choices=sorted(SCENARIOS),
                        help="scenario to run; may be passed more than once")
    parser.add_argument("--verify", action="store_true",
                        help="verify only the smoke checks for captured scenarios")
    parser.add_argument("--verify-all", action="store_true",
                        help="run the full tools/widescreen_visual_smoke.py gate")
    args = parser.parse_args()

    root = args.root.resolve()
    exe = resolve_file(root, args.exe, "build-codex-mingw/SuperMetroidSNESRecomp.exe")
    config = resolve_file(root, args.config, "_codex_probe_fast.ini")
    rom = resolve_file(root, args.rom, "Super Metroid (Japan, USA) (En,Ja).sfc")

    for label, path in (("exe", exe), ("config", config), ("rom", rom)):
      if not path.exists():
        raise SystemExit(f"missing {label}: {path}")

    names = tuple(args.scenario) if args.scenario else DEFAULT_SCENARIOS
    if args.include_long and "lava" not in names:
      names = names + ("lava",)

    for name in names:
      scenario = SCENARIOS[name]
      print(f"capture {name} -> {scenario.out_dir}", flush=True)
      run_scenario(root, exe, config, rom, scenario)

    if args.verify or args.verify_all:
      smoke = root / "tools" / "widescreen_visual_smoke.py"
      command = [sys.executable, str(smoke), "--root", str(root)]
      if args.verify and not args.verify_all:
        selected_checks: list[str] = []
        for name in names:
          selected_checks.extend(SCENARIO_CHECKS[name])
        for check in dict.fromkeys(selected_checks):
          command.extend(["--check", check])
      return subprocess.call(command)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
