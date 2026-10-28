#!/usr/bin/env python3
"""Validate map controls, unchanged discovery/saves, and live/paused gameplay.

Captures the actual composed presentation (including overlays), not only the
PPU field. All launches use fresh profiles and muted dummy SDL drivers.
"""
from __future__ import annotations
import argparse
from concurrent.futures import ThreadPoolExecutor
import hashlib
import json
from pathlib import Path
from test_qol_rom_removal import ROM_SHA, prefix, read_rows, run_case

PROBE = "079B:2,079F:2,0998:2,0727:2,0AF6:2,0AFA:2,09C2:2,09DA:2,09DC:2,09DE:2,09E0:2,05B6:2,0F7A:2,0F7E:2"
PACKAGE = "super-metroid.map-hud/"


def scenarios():
    base = prefix()
    paused = base + "press start 8\nwait 300\ndump paused\nquit\n"
    world = base + "press start 8\nwait 60\npress x 1\n"
    reveal = PACKAGE + "reveal-map:secrets=shown"
    mini = base + ("dump before\npress select+start 1\npress right+b 60\nwait 240\ndump open\n"
                   "press select+start 1\nwait 2\ndump closed\npress right+b 30\ndump resumed\nquit\n")
    return [
        ("stock", [], paused, PROBE),
        ("area", [PACKAGE + "pause-map"], paused, PROBE),
        ("area-reveal", [PACKAGE + "pause-map", reveal], paused, PROBE),
        ("world", [PACKAGE + "pause-map", PACKAGE + "world-map", reveal],
         world + "wait 240\ndump world\nquit\n", PROBE),
        ("world-zoom", [PACKAGE + "pause-map", PACKAGE + "world-map", reveal],
         world + "wait 10\npress r 1\npress r 1\npress right 12\nwait 240\ndump world\nquit\n", PROBE),
        ("mini-live", [PACKAGE + "minimap:background=live"], mini, PROBE),
        ("mini-paused", [PACKAGE + "minimap:background=paused"], mini, PROBE),
    ]


def validate(out, identities):
    def ram(name, tag): return (out / name / (tag + ".wram.bin")).read_bytes()
    def word(b, a): return int.from_bytes(b[a:a+2], "little")
    stock = ram("stock", "paused")
    assert word(stock, 0x998) == 15 and word(stock, 0x727) == 0
    assert stock == ram("area", "paused"), "clear map changed guest WRAM"
    assert read_rows(out / "stock/wram.csv") == read_rows(out / "area/wram.csv")
    assert identities["stock"]["trace_sha256"] == identities["area"]["trace_sha256"]
    assert identities["stock"]["presented_sha256"] != identities["area"]["presented_sha256"]
    for name, tag in (("area-reveal", "paused"), ("world", "world"), ("world-zoom", "world")):
        b = ram(name, tag)
        assert word(b, 0x998) == 15 and word(b, 0x727) == 0, "zoom shoulder changed the native pause tab"
        for start, end in ((0x7f7, 0x8f7), (0xcd52, 0xd352), (0xd870, 0xd8b0)):
            assert b[start:end] == stock[start:end], "view/reveal changed exploration or item bits"
        assert (out / name / (tag + ".sram.bin")).read_bytes() == (out / "stock/paused.sram.bin").read_bytes()
    assert identities["world"]["presented_sha256"] != identities["world-zoom"]["presented_sha256"]
    observations = {"clear_map_guest_trace": "stock identical", "discovery_item_bits_and_sram": "unchanged",
                    "world_zoom_stays_on_map_tab": True}
    for name in ("mini-live", "mini-paused"):
        before, opened, closed, resumed = [ram(name, tag) for tag in ("before", "open", "closed", "resumed")]
        assert all(word(b, 0x998) == 8 for b in (before, opened, closed, resumed))
        rows = read_rows(out / name / "wram.csv")
        interval = [r for r in rows if 10300 <= r["frame"] <= 10400]
        clocks = {(r["09da"], r["09dc"], r["09de"], r["09e0"]) for r in interval}
        assert len({r["05b6"] for r in interval}) > 1, "interrupts did not advance"
        if name == "mini-paused":
            assert len(clocks) == 1 and len({(r["0af6"], r["0afa"], r["0f7a"], r["0f7e"]) for r in interval}) == 1
            assert (word(resumed, 0xaf6), word(resumed, 0xafa)) != (word(closed, 0xaf6), word(closed, 0xafa))
        else:
            assert len(clocks) > 1
            assert word(opened, 0xaf6) != word(before, 0xaf6)
        observations[name] = {"distinct_game_clocks_in_101_frames": len(clocks), "interrupts_advance": True,
                              "game_state_stays_playing": True}
    return observations


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    for name in ("build", "rom", "out"): ap.add_argument("--" + name, type=Path, required=True)
    ap.add_argument("--interp-targets", type=Path)
    ap.add_argument("--compare", type=Path)
    a = ap.parse_args(); a.out = a.out.resolve(); a.out.mkdir(parents=True, exist_ok=False)
    assert hashlib.sha256(a.rom.read_bytes()).hexdigest() == ROM_SHA
    def run(c): return run_case(c, build=a.build.resolve(), rom=a.rom.resolve(), out=a.out,
                               interp_targets=a.interp_targets, screenshot_frame=10400)
    with ThreadPoolExecutor(max_workers=2) as pool: identities = dict(pool.map(run, scenarios()))
    result = {"observations": validate(a.out, identities), "identities": identities}
    if a.compare:
        original = json.loads((a.compare / "results.json").read_text())
        differences = {}
        for name, _, _, _ in scenarios():
            assert read_rows(a.out / name / "wram.csv") == read_rows(a.compare / name / "wram.csv")
            assert identities[name]["presented_sha256"] == original["identities"][name]["presented_sha256"]
            for path in (a.out / name).glob("*.wram.bin"):
                current = path.read_bytes(); before = (a.compare / name / path.name).read_bytes()
                assert len(current) == len(before) == 0x20000
                changed = [i for i, (x, y) in enumerate(zip(before, current)) if x != y]
                if changed: differences[name + "/" + path.name] = [hex(i) for i in changed]
        result["comparison"] = {"observed_fields_and_presentation": "identical", "full_wram_differences": differences}
        assert not differences, differences
        assert identities["mini-paused"]["execution_share"]["interp_insns"] > original["identities"]["mini-paused"]["execution_share"]["interp_insns"]
    (a.out / "results.json").write_text(json.dumps(result, indent=2))
    print(json.dumps(result["observations"], indent=2))


if __name__ == "__main__": main()
