#!/usr/bin/env python3
"""Smoke-check known Super Metroid widescreen visual captures.

This intentionally depends on capture artifacts produced during widescreen
development. It is not a replacement for fresh playthrough coverage; it is a
cheap guard that the known-good HUD, map, attract, lava, and ability/effect
frames still satisfy the same geometry invariants.
"""

from __future__ import annotations

import argparse
import importlib.util
import sys
from pathlib import Path

_SCRIPT_DIR = Path(str(__file__).replace("\\", "/")).resolve().parent
_ANALYZER_PATH = _SCRIPT_DIR / "analyze_widescreen_frames.py"
_ANALYZER_SPEC = importlib.util.spec_from_file_location(
    "analyze_widescreen_frames", _ANALYZER_PATH)
if _ANALYZER_SPEC is None or _ANALYZER_SPEC.loader is None:
    raise ImportError(f"cannot load {_ANALYZER_PATH}")
_ANALYZER = importlib.util.module_from_spec(_ANALYZER_SPEC)
sys.modules[_ANALYZER_SPEC.name] = _ANALYZER
_ANALYZER_SPEC.loader.exec_module(_ANALYZER)

EXPECTED_WIDTH = _ANALYZER.EXPECTED_WIDTH
EXPECTED_HEIGHT = _ANALYZER.EXPECTED_HEIGHT
LEFT_MARGIN = _ANALYZER.LEFT_MARGIN
RIGHT_MARGIN_START = _ANALYZER.RIGHT_MARGIN_START
analyze = _ANALYZER.analyze
read_bmp_argb = _ANALYZER.read_bmp_argb
rect_stats = _ANALYZER.rect_stats
SKIP_MISSING_RETURN_CODE = 77


ROOT_RELATIVE_CHECKS = [
    {
        "name": "hud",
        "kind": "hud_edge",
        "bmp": "_codex_sm_ws_fresh_map_hud_20260721/frames/frame_004200.bmp",
    },
    {
        "name": "map",
        "kind": "native_ui_centered",
        "bmp": "_codex_sm_ws_fresh_map_hud_20260721/frames/frame_004920.bmp",
        "wram": "_codex_sm_ws_fresh_map_hud_20260721/frame_004920_wram.bin",
        "fields": {"game_state": (0x0998, 0x000F)},
        "min_active_enemies": 3,
    },
    {
        "name": "lava",
        "kind": "liquid_stretch",
        "bmp": "_codex_sm_ws_fresh_lava31013_20260721/frames/frame_031013.bmp",
        "wram": "_codex_sm_ws_fresh_lava31013_20260721/frame_031013_wram.bin",
        "fields": {
            "game_state": (0x0998, 0x002A),
            "area_index": (0x079F, 0x0002),
            "fx_type": (0x196E, 0x0002),
        },
        "min_active_enemies": 6,
        "min_active_eprojs": 1,
        "min_liquid_edge_colors": 8,
    },
    {
        "name": "powerbomb",
        "kind": "margin_fill",
        "bmp": "_codex_sm_ws_powerbomb_natural_morph_boot_20260721/frames/frame_003980.bmp",
        "wram": "_codex_sm_ws_powerbomb_natural_morph_boot_20260721/frame_003980_wram.bin",
        "fields": {
            "selected_power_bomb": (0x09D2, 0x0003),
            "power_bomb_effect": (0x0CE2, 0x0440),
        },
        "min_active_enemies": 3,
        "min_active_projectiles": 1,
    },
    {
        "name": "xray",
        "kind": "margin_fill",
        "bmp": "_codex_sm_ws_fresh_xray_ability_20260721/frames/frame_004050.bmp",
        "wram": "_codex_sm_ws_fresh_xray_ability_20260721/frame_004050_wram.bin",
        "fields": {"selected_xray": (0x09D2, 0x0005)},
        "min_active_enemies": 3,
    },
    {
        "name": "grapple",
        "kind": "margin_fill",
        "bmp": "_codex_sm_ws_fresh_grapple_ability_20260721/frames/frame_003960.bmp",
        "wram": "_codex_sm_ws_fresh_grapple_ability_20260721/frame_003949_wram.bin",
        "fields": {
            "selected_grapple": (0x09D2, 0x0004),
            "grapple_state": (0x0D32, 0xC703),
        },
        "min_active_enemies": 3,
    },
    {
        "name": "attract",
        "kind": "margin_fill",
        "bmp": "_codex_sm_ws_fresh_attract4_20260721/frames/frame_018000.bmp",
        "wram": "_codex_sm_ws_fresh_attract4_20260721/frame_018000_wram.bin",
        "fields": {"attract_gameplay_state": (0x0998, 0x002A)},
        "min_active_enemies": 8,
    },
    {
        "name": "attract_sweep",
        "kind": "sequence_clean",
        "bmps": [
            "_codex_sm_ws_attract_long3_20260721/frames/frame_013200.bmp",
            "_codex_sm_ws_attract_long3_20260721/frames/frame_014400.bmp",
            "_codex_sm_ws_attract_long3_20260721/frames/frame_015600.bmp",
            "_codex_sm_ws_attract_long3_20260721/frames/frame_016800.bmp",
            "_codex_sm_ws_attract_long3_20260721/frames/frame_018000.bmp",
            "_codex_sm_ws_attract_long3_20260721/frames/frame_019200.bmp",
            "_codex_sm_ws_attract_long3_20260721/frames/frame_020400.bmp",
            "_codex_sm_ws_attract_long3_20260721/frames/frame_021600.bmp",
            "_codex_sm_ws_attract_long3_20260721/frames/frame_022800.bmp",
            "_codex_sm_ws_attract_long3_20260721/frames/frame_024000.bmp",
        ],
        "min_sequence_frames": 10,
        "min_margin_fill_frames": 6,
    },
    {
        "name": "attract_sweep_extended",
        "kind": "sequence_clean",
        "bmps": [
            "_codex_sm_ws_attract_extended_20260721/frames/frame_025200.bmp",
            "_codex_sm_ws_attract_extended_20260721/frames/frame_027600.bmp",
            "_codex_sm_ws_attract_extended_20260721/frames/frame_030000.bmp",
            "_codex_sm_ws_attract_extended_20260721/frames/frame_032400.bmp",
            "_codex_sm_ws_attract_extended_20260721/frames/frame_034800.bmp",
            "_codex_sm_ws_attract_extended_20260721/frames/frame_037200.bmp",
            "_codex_sm_ws_attract_extended_20260721/frames/frame_039600.bmp",
            "_codex_sm_ws_attract_extended_20260721/frames/frame_042000.bmp",
            "_codex_sm_ws_attract_extended_20260721/frames/frame_044400.bmp",
            "_codex_sm_ws_attract_extended_20260721/frames/frame_046800.bmp",
        ],
        "min_sequence_frames": 10,
        "min_nonblank_frames": 7,
        "max_blank_frames": 3,
        "min_margin_fill_frames": 3,
    },
]


def fail(message: str) -> int:
    print(f"FAIL {message}")
    return 1


def skip(message: str) -> None:
    print(f"SKIP {message}")


def check_margin_fill(row: dict[str, object], min_margin: int) -> str | None:
    if int(row["left"]) < min_margin or int(row["right"]) < min_margin:
      return (f"margin fill too low: left={row['left']} right={row['right']} "
              f"min={min_margin}")
    return None


def check_hud_edge(row: dict[str, object], min_hud: int) -> str | None:
    if int(row["hud_left"]) < min_hud or int(row["hud_right"]) < min_hud:
      return (f"HUD edge pixels too low: left={row['hud_left']} "
              f"right={row['hud_right']} min={min_hud}")
    return None


def check_centered_native_ui(row: dict[str, object], tolerance: int) -> str | None:
    if int(row["nonblack"]) == 0:
      return "native UI frame is blank"
    min_x = int(row["min_x"])
    max_x = int(row["max_x"])
    bbox_width = max_x - min_x + 1
    bbox_center2 = min_x + max_x
    expected_center2 = EXPECTED_WIDTH - 1
    native_width = RIGHT_MARGIN_START - LEFT_MARGIN + 1
    if bbox_width > native_width + tolerance:
      return f"native UI bbox too wide: width={bbox_width} max={native_width + tolerance}"
    if abs(bbox_center2 - expected_center2) > tolerance * 2:
      return (f"native UI off center: center2={bbox_center2} "
              f"expected={expected_center2} tol={tolerance * 2}")
    return None


def check_liquid_stretch(
    bmp_path: Path, wram_path: Path, min_edge_colors: int) -> str | None:
    width, height, pixels = read_bmp_argb(bmp_path)
    camera_y = read_wram16(wram_path, 0x0915)
    fx_y = read_wram16(wram_path, 0x1962)
    y0 = max(32, fx_y - camera_y)
    if y0 >= min(height, EXPECTED_HEIGHT):
      return f"liquid band is offscreen: y0={y0}"
    _left_nonblack, left_colors, _left_trans = rect_stats(
        pixels, width, height, 0, min(LEFT_MARGIN, width), y0, height)
    _right_nonblack, right_colors, _right_trans = rect_stats(
        pixels, width, height, min(RIGHT_MARGIN_START, width), width, y0,
        height)
    if left_colors < min_edge_colors or right_colors < min_edge_colors:
      return (f"liquid edge texture too flat: left_colors={left_colors} "
              f"right_colors={right_colors} min={min_edge_colors} y0={y0}")
    return None


def row_hard_flags(row: dict[str, object]) -> set[str]:
    flags = set() if row["flags"] == "-" else set(str(row["flags"]).split(","))
    return flags & {"BAD_SIZE", "BLANK", "NOISY_MARGINS"}


def sequence_margin_filled(row: dict[str, object], min_margin: int) -> bool:
    return int(row["left"]) >= min_margin and int(row["right"]) >= min_margin


def read_wram16(path: Path, address: int) -> int:
    data = path.read_bytes()
    if address < 0 or address + 1 >= len(data):
      raise ValueError(f"WRAM address out of range: 0x{address:04x}")
    return data[address] | (data[address + 1] << 8)


def check_wram_fields(path: Path, fields: dict[str, tuple[int, int]]) -> str | None:
    for name, (address, expected) in fields.items():
      actual = read_wram16(path, address)
      if actual != expected:
        return (f"{name} expected 0x{expected:04x} at 0x{address:04x}, "
                f"got 0x{actual:04x}")
    return None


def count_nonzero_words(path: Path, base: int, stride: int, slots: int) -> int:
    data = path.read_bytes()
    count = 0
    for slot in range(slots):
      address = base + slot * stride
      if address + 1 >= len(data):
        raise ValueError(f"WRAM address out of range: 0x{address:04x}")
      if data[address] | (data[address + 1] << 8):
        count += 1
    return count


def check_min_count(path: Path, label: str, actual: int, minimum: int) -> str | None:
    if actual < minimum:
      return f"{label} count too low: count={actual} min={minimum}"
    return None


def check_wram_slot_counts(path: Path, check: dict[str, object]) -> str | None:
    # enemy_ptr(+0) == 0 means empty; see the post-mortem slot dump comment.
    count_specs = (
        ("active enemy", "min_active_enemies", 0x0F78, 0x40, 32),
        ("active projectile", "min_active_projectiles", 0x0C04, 2, 16),
        ("active enemy projectile", "min_active_eprojs", 0x1997, 2, 48),
    )
    for label, key, base, stride, slots in count_specs:
      value = check.get(key)
      if value is None:
        continue
      reason = check_min_count(
          path, label, count_nonzero_words(path, base, stride, slots), int(value))
      if reason:
        return reason
    return None


def describe_wram_slot_counts(path: Path, check: dict[str, object]) -> str:
    output: list[str] = []
    count_specs = (
        ("enemies", "min_active_enemies", 0x0F78, 0x40, 32),
        ("projectiles", "min_active_projectiles", 0x0C04, 2, 16),
        ("eprojs", "min_active_eprojs", 0x1997, 2, 48),
    )
    for label, key, base, stride, slots in count_specs:
      if check.get(key) is not None:
        output.append(
            f"{label}={count_nonzero_words(path, base, stride, slots)}")
    return "" if not output else " slots=(" + ",".join(output) + ")"


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=Path, default=Path.cwd())
    parser.add_argument("--check", action="append",
                        choices=[str(check["name"]) for check in ROOT_RELATIVE_CHECKS],
                        help="named smoke check to run; may be passed more than once")
    parser.add_argument("--skip-missing", action="store_true",
                        help="return CTest skip code instead of failing on missing artifacts")
    parser.add_argument("--min-margin-pixels", type=int, default=100)
    parser.add_argument("--min-hud-edge-pixels", type=int, default=20)
    parser.add_argument("--center-tolerance", type=int, default=4)
    args = parser.parse_args()

    failures = 0
    skipped_missing = False
    selected = set(args.check) if args.check else None
    for check in ROOT_RELATIVE_CHECKS:
      name = str(check["name"])
      if selected is not None and name not in selected:
        continue
      kind = str(check["kind"])
      if kind == "sequence_clean":
        paths = [args.root / str(path) for path in check.get("bmps", [])]
        missing = [path for path in paths if not path.exists()]
        if missing:
          if args.skip_missing:
            skipped_missing = True
            skip(f"{name}/{kind} missing {missing[0]}")
          else:
            failures += fail(f"{name}/{kind} missing {missing[0]}")
          continue

        rows = []
        try:
          rows = [analyze(path) for path in paths]
        except Exception as exc:  # pragma: no cover - diagnostic path
          failures += fail(f"{name}/{kind}: {exc}")
          continue

        max_blank_frames = int(check.get("max_blank_frames", 0))
        blank_frames = [
            row for row in rows
            if "BLANK" in (set() if row["flags"] == "-"
                           else set(str(row["flags"]).split(",")))
        ]
        allowed_hard_flags = {"BLANK"} if max_blank_frames else set()
        hard_failures = [
            row for row in rows
            if row_hard_flags(row) - allowed_hard_flags
        ]
        if hard_failures:
          row = hard_failures[0]
          failures += fail(f"{name}/{kind} {row['name']}: flags={row['flags']}")
          continue
        if len(blank_frames) > max_blank_frames:
          failures += fail(
              f"{name}/{kind}: blank_frames={len(blank_frames)} "
              f"max={max_blank_frames}")
          continue

        margin_fill_frames = sum(
            1 for row in rows
            if sequence_margin_filled(row, args.min_margin_pixels))
        min_frames = int(check.get("min_sequence_frames", 1))
        min_nonblank_frames = int(check.get("min_nonblank_frames", min_frames))
        min_margin_frames = int(check.get("min_margin_fill_frames", 0))
        if len(rows) < min_frames:
          failures += fail(
              f"{name}/{kind}: frames={len(rows)} min={min_frames}")
          continue
        if len(rows) - len(blank_frames) < min_nonblank_frames:
          failures += fail(
              f"{name}/{kind}: nonblank_frames={len(rows) - len(blank_frames)} "
              f"min={min_nonblank_frames}")
          continue
        if margin_fill_frames < min_margin_frames:
          failures += fail(
              f"{name}/{kind}: margin_fill_frames={margin_fill_frames} "
              f"min={min_margin_frames}")
          continue

        print(
            f"OK {name}/{kind} frames={len(rows)} "
            f"nonblank_frames={len(rows) - len(blank_frames)} "
            f"blank_frames={len(blank_frames)} "
            f"margin_fill_frames={margin_fill_frames}")
        continue

      path = args.root / str(check["bmp"])
      if not path.exists():
        if args.skip_missing:
          skipped_missing = True
          skip(f"{name}/{kind} missing {path}")
        else:
          failures += fail(f"{name}/{kind} missing {path}")
        continue
      try:
        row = analyze(path)
      except Exception as exc:  # pragma: no cover - diagnostic path
        failures += fail(f"{name}/{kind} {path}: {exc}")
        continue

      flags = set() if row["flags"] == "-" else set(str(row["flags"]).split(","))
      if "BAD_SIZE" in flags or "BLANK" in flags or "NOISY_MARGINS" in flags:
        failures += fail(f"{name}/{kind} {path}: flags={row['flags']}")
        continue

      wram_rel = check.get("wram")
      wram_path = args.root / str(wram_rel) if wram_rel else None
      reason = None
      if kind == "margin_fill":
        reason = check_margin_fill(row, args.min_margin_pixels)
      elif kind == "hud_edge":
        reason = check_hud_edge(row, args.min_hud_edge_pixels)
      elif kind == "native_ui_centered":
        reason = check_centered_native_ui(row, args.center_tolerance)
      elif kind == "liquid_stretch":
        reason = check_margin_fill(row, args.min_margin_pixels)
        if not reason:
          if wram_path is None or not wram_path.exists():
            reason = f"missing liquid WRAM {wram_path}"
          else:
            reason = check_liquid_stretch(
                path, wram_path, int(check.get("min_liquid_edge_colors", 8)))
      else:
        reason = f"unknown check kind {kind}"

      if reason:
        failures += fail(f"{name}/{kind} {path}: {reason}")
        continue

      slot_counts = ""
      if wram_rel:
        assert wram_path is not None
        if not wram_path.exists():
          if args.skip_missing:
            skipped_missing = True
            skip(f"{name}/{kind} missing {wram_path}")
          else:
            failures += fail(f"{name}/{kind} missing {wram_path}")
          continue
        field_reason = check_wram_fields(
            wram_path,
            check.get("fields", {}))  # type: ignore[arg-type]
        if field_reason:
          failures += fail(f"{name}/{kind} {wram_path}: {field_reason}")
          continue
        count_reason = check_wram_slot_counts(wram_path, check)
        if count_reason:
          failures += fail(f"{name}/{kind} {wram_path}: {count_reason}")
          continue
        slot_counts = describe_wram_slot_counts(wram_path, check)

      print(
          f"OK {name}/{kind} {path.name} "
          f"bbox={row['bbox']} left={row['left']} right={row['right']} "
          f"hud=({row['hud_left']},{row['hud_right']}){slot_counts}")

    if failures:
      return 1
    if skipped_missing:
      return SKIP_MISSING_RETURN_CODE
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
