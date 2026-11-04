#!/usr/bin/env python3
"""Use the real pause Options action and return to native file selection.

Fresh isolated profile; no saved progress is modified. An optional existing
host self-test saves/loads during the fade to validate transition snapshots.
"""
from __future__ import annotations
import argparse
from concurrent.futures import ThreadPoolExecutor
import hashlib
import json
import re
from pathlib import Path
from test_qol_rom_removal import ROM_SHA, prefix, read_rows, run_case

PROBE = "079B:2,0998:2,0727:2,0051:1,0723:2,0725:2,05B6:2,09C2:2,1F51:2"


def scenario(run_ahead=False):
    # Offline/default video: HUD anchoring is the one disabled row. Eight
    # fresh Down edges reach Quit, immediately before Exit and Back.
    navigation = "press down 1\n" * 6 + ("press right 1\n" * 2 if run_ahead else "") + "press down 1\n" * 2
    return prefix() + ("dump before\npress start 8\nwait 60\npress l 1\nwait 2\n" +
        navigation + "dump selected\npress a 1\n"
        "until16 998 == 4 2400\nwait 1000\ndump files\nquit\n")


def validate(out, names):
    observations = {}
    for name in names:
        root = out / name
        before = (root / "before.sram.bin").read_bytes()
        assert before == (root / "files.sram.bin").read_bytes(), "native SRAM changed"
        ram = (root / "files.wram.bin").read_bytes()
        assert int.from_bytes(ram[0x998:0x99a], "little") == 4
        log = (root / "stderr.log").read_text(errors="replace")
        rows = read_rows(root / "wram.csv")
        states = {r["0998"] for r in rows if r["frame"] > 10200}
        assert 0x1a in states and 1 in states and 4 in states
        assert rows[-1]["05b6"] != rows[-2]["05b6"], "guest stopped on the file screen"
        observations[name] = {"native_sram_sha256": hashlib.sha256(before).hexdigest(),
                              "file_select_reached": True, "guest_interrupts_advance": True}
        if name == "snapshot":
            assert "pad X (save) only" in log and "load issued" in log
            transitions = []
            for row in rows[10262:]:
                if not transitions or transitions[-1] != row["0998"]: transitions.append(row["0998"])
            assert transitions.count(0x1a) == 2 and transitions.count(4) == 2, transitions
            observations[name]["restored_fade_after_first_return_completed"] = True
        if name == "run-ahead":
            assert re.search(r"(?mi)^RunAhead\s*=\s*2\s*$", (root / "config.ini").read_text())
            assert "[runahead] 2 frame(s)" in log and "[runahead] snapshot buffer" in log
            assert "disabling run-ahead" not in log
            observations[name]["run_ahead_frames"] = 2
    return observations


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    for name in ("build", "rom", "out"): ap.add_argument("--" + name, type=Path, required=True)
    ap.add_argument("--interp-targets", type=Path)
    ap.add_argument("--compare", type=Path)
    ap.add_argument("--snapshot-frame", type=int, help="save/load the native host slot during quit fading")
    ap.add_argument("--load-frame", type=int, default=11000)
    ap.add_argument("--run-ahead", action="store_true")
    a = ap.parse_args(); a.out = a.out.resolve(); a.out.mkdir(parents=True, exist_ok=False)
    assert hashlib.sha256(a.rom.read_bytes()).hexdigest() == ROM_SHA
    cases = ["quit"] + (["snapshot"] if a.snapshot_frame else []) + (["run-ahead"] if a.run_ahead else [])
    def run(name):
        overrides = {"SNESRECOMP_OVERLAY_SELFTEST": str(a.snapshot_frame),
                     "SNESRECOMP_OVERLAY_SELFTEST_SAVEONLY": "1",
                     "SNESRECOMP_OVERLAY_SELFTEST_LOADAT": str(a.load_frame)} if name == "snapshot" else None
        return run_case((name, ["super-metroid.pacing/quit-to-file-select"], scenario(name == "run-ahead"), PROBE),
                        build=a.build.resolve(), rom=a.rom.resolve(), out=a.out,
                        interp_targets=a.interp_targets, env_overrides=overrides, screenshot_frame=10262)
    with ThreadPoolExecutor(max_workers=2) as pool: identities = dict(pool.map(run, cases))
    result = {"observations": validate(a.out, cases), "identities": identities}
    if a.snapshot_frame:
        normal = read_rows(a.out / "quit/wram.csv"); saved = read_rows(a.out / "snapshot/wram.csv")
        assert normal[:a.load_frame] == saved[:a.load_frame]
        result["snapshot_trace"] = "identical before the intentional rewind; quit transition completes again after load"
    if a.compare:
        original = json.loads((a.compare / "results.json").read_text())
        for name in cases:
            assert read_rows(a.out / name / "wram.csv") == read_rows(a.compare / name / "wram.csv")
            for path in (a.out / name).glob("*.wram.bin"):
                assert path.read_bytes() == (a.compare / name / path.name).read_bytes()
        assert identities["quit"]["execution_share"]["interp_insns"] > original["identities"]["quit"]["execution_share"]["interp_insns"]
        result["comparison"] = "observed fields and full WRAM snapshots identical"
    (a.out / "results.json").write_text(json.dumps(result, indent=2))
    print(json.dumps(result["observations"], indent=2))


if __name__ == "__main__": main()
