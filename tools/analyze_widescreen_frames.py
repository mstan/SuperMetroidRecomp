#!/usr/bin/env python3
"""Summarize Super Metroid widescreen BMP captures.

This is intentionally heuristic. It catches hard failures automatically
(wrong dimensions, blank frames) and prints side-margin/edge-band stats that
make garble and missing-margin regressions cheap to inspect.
"""

from __future__ import annotations

import argparse
import struct
from pathlib import Path


EXPECTED_WIDTH = 342
EXPECTED_HEIGHT = 224
LEFT_MARGIN = 43
RIGHT_MARGIN_START = 299
HUD_HEIGHT = 32


def read_bmp_argb(path: Path) -> tuple[int, int, bytes]:
    data = path.read_bytes()
    if len(data) < 54 or data[:2] != b"BM":
      raise ValueError("not a BMP")
    off = struct.unpack_from("<I", data, 10)[0]
    header_size = struct.unpack_from("<I", data, 14)[0]
    if header_size < 40:
      raise ValueError("unsupported BMP header")
    width = struct.unpack_from("<i", data, 18)[0]
    height_signed = struct.unpack_from("<i", data, 22)[0]
    planes, bpp = struct.unpack_from("<HH", data, 26)
    if planes != 1 or bpp != 32:
      raise ValueError("expected 32-bit BMP")
    height = abs(height_signed)
    row_bytes = width * 4
    pixels = data[off:off + row_bytes * height]
    if len(pixels) != row_bytes * height:
      raise ValueError("truncated BMP")
    if height_signed > 0:
      rows = [pixels[i * row_bytes:(i + 1) * row_bytes]
              for i in range(height)]
      pixels = b"".join(reversed(rows))
    return width, height, pixels


def is_nonblack(pixel: bytes) -> bool:
    b, g, r, _a = pixel
    return r != 0 or g != 0 or b != 0


def rect_stats(pixels: bytes, width: int, height: int, x0: int, x1: int,
               y0: int, y1: int) -> tuple[int, int, int]:
    nonblack = 0
    colors: set[bytes] = set()
    transitions = 0
    prev: bytes | None = None
    for y in range(max(0, y0), min(height, y1)):
      row = pixels[y * width * 4:(y + 1) * width * 4]
      for x in range(max(0, x0), min(width, x1)):
        p = row[x * 4:x * 4 + 4]
        if is_nonblack(p):
          nonblack += 1
          colors.add(p[:3])
        if prev is not None and p[:3] != prev:
          transitions += 1
        prev = p[:3]
    return nonblack, len(colors), transitions


def band_stats(pixels: bytes, width: int, height: int, x0: int,
               x1: int) -> tuple[int, int, int]:
    return rect_stats(pixels, width, height, x0, x1, 0, height)


def analyze(path: Path) -> dict[str, object]:
    width, height, pixels = read_bmp_argb(path)
    min_x, max_x = width, -1
    min_y, max_y = height, -1
    total_nonblack = 0
    for y in range(height):
      row = pixels[y * width * 4:(y + 1) * width * 4]
      for x in range(width):
        if is_nonblack(row[x * 4:x * 4 + 4]):
          total_nonblack += 1
          min_x = min(min_x, x)
          max_x = max(max_x, x)
          min_y = min(min_y, y)
          max_y = max(max_y, y)

    left_nonblack, left_colors, left_trans = band_stats(
        pixels, width, height, 0, min(LEFT_MARGIN, width))
    right_nonblack, right_colors, right_trans = band_stats(
        pixels, width, height, min(RIGHT_MARGIN_START, width), width)
    center_nonblack, center_colors, center_trans = band_stats(
        pixels, width, height, min(LEFT_MARGIN, width),
        min(RIGHT_MARGIN_START, width))
    hud_left_nonblack, _hud_left_colors, _hud_left_trans = rect_stats(
        pixels, width, height, 0, min(LEFT_MARGIN, width), 0, HUD_HEIGHT)
    hud_right_nonblack, _hud_right_colors, _hud_right_trans = rect_stats(
        pixels, width, height, min(RIGHT_MARGIN_START, width), width, 0,
        HUD_HEIGHT)

    flags: list[str] = []
    if width != EXPECTED_WIDTH or height != EXPECTED_HEIGHT:
      flags.append("BAD_SIZE")
    if total_nonblack == 0:
      flags.append("BLANK")
    if total_nonblack and max_x < LEFT_MARGIN:
      flags.append("CENTER_MISSING")
    if center_nonblack > 1000 and left_nonblack == 0 and right_nonblack == 0:
      flags.append("MARGINS_BLANK")
    margin_pixels = max(1, (LEFT_MARGIN + max(0, width - RIGHT_MARGIN_START)) *
                        height)
    if (left_colors + right_colors) > 512 and (
        left_trans + right_trans) / margin_pixels > 0.85:
      flags.append("NOISY_MARGINS")

    bbox = "empty" if total_nonblack == 0 else (
        f"x={min_x}..{max_x} y={min_y}..{max_y}")
    return {
        "name": path.name,
        "size": f"{width}x{height}",
        "nonblack": total_nonblack,
        "bbox": bbox,
        "min_x": min_x,
        "max_x": max_x,
        "min_y": min_y,
        "max_y": max_y,
        "left": left_nonblack,
        "center": center_nonblack,
        "right": right_nonblack,
        "hud_left": hud_left_nonblack,
        "hud_right": hud_right_nonblack,
        "lcolors": left_colors,
        "ccolors": center_colors,
        "rcolors": right_colors,
        "flags": ",".join(flags) if flags else "-",
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("paths", nargs="+", type=Path)
    parser.add_argument(
        "--fail-on-warnings", action="store_true",
        help="treat heuristic warnings such as MARGINS_BLANK as failures")
    parser.add_argument(
        "--require-margin-fill", action="store_true",
        help="fail if contentful frames do not draw into both side margins")
    parser.add_argument(
        "--min-center-pixels", type=int, default=1000,
        help="center-pixel threshold for --require-margin-fill")
    parser.add_argument(
        "--min-margin-pixels", type=int, default=100,
        help="per-side margin-pixel threshold for --require-margin-fill")
    parser.add_argument(
        "--require-centered-native-ui", action="store_true",
        help="fail if nonblank frames are not centered as a native-width UI")
    parser.add_argument(
        "--center-tolerance", type=int, default=4,
        help="pixel tolerance for --require-centered-native-ui")
    parser.add_argument(
        "--require-hud-edge-ui", action="store_true",
        help="fail if contentful frames do not draw HUD pixels near both edges")
    parser.add_argument(
        "--min-hud-edge-pixels", type=int, default=20,
        help="per-side HUD-band pixel threshold for --require-hud-edge-ui")
    args = parser.parse_args()

    files: list[Path] = []
    for path in args.paths:
      if path.is_dir():
        files.extend(sorted(path.glob("*.bmp")))
      else:
        files.append(path)
    if not files:
      raise SystemExit("no BMP files found")

    print("frame,size,nonblack,bbox,left,center,right,hud_left,hud_right,lcolors,ccolors,rcolors,flags")
    hard_fail = 0
    warnings = 0
    targeted_fail = 0
    for path in files:
      row = analyze(path)
      flags = set() if row["flags"] == "-" else set(str(row["flags"]).split(","))
      if "BAD_SIZE" in flags:
        hard_fail += 1
      elif flags:
        warnings += 1
      if args.require_margin_fill and row["center"] >= args.min_center_pixels:
        if (row["left"] < args.min_margin_pixels or
            row["right"] < args.min_margin_pixels):
          targeted_fail += 1
          flags.add("MARGIN_FILL_FAIL")
      if args.require_centered_native_ui and row["nonblack"]:
        min_x = int(row["min_x"])
        max_x = int(row["max_x"])
        bbox_width = max_x - min_x + 1
        bbox_center2 = min_x + max_x
        expected_center2 = EXPECTED_WIDTH - 1
        tol = args.center_tolerance
        if (bbox_width > (RIGHT_MARGIN_START - LEFT_MARGIN + 1 + tol) or
            abs(bbox_center2 - expected_center2) > tol * 2):
          targeted_fail += 1
          flags.add("CENTERED_UI_FAIL")
      if args.require_hud_edge_ui and row["center"] >= args.min_center_pixels:
        if (row["hud_left"] < args.min_hud_edge_pixels or
            row["hud_right"] < args.min_hud_edge_pixels):
          targeted_fail += 1
          flags.add("HUD_EDGE_UI_FAIL")
      row["flags"] = ",".join(sorted(flags)) if flags else "-"
      print(",".join(str(row[k]) for k in (
          "name", "size", "nonblack", "bbox", "left", "center", "right",
          "hud_left", "hud_right", "lcolors", "ccolors", "rcolors", "flags")))
    if hard_fail:
      return 1
    if targeted_fail:
      return 1
    if warnings and args.fail_on_warnings:
      return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
