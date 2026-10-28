#!/usr/bin/env python3
"""Muted auto-charge checks with isolated saves, real shots and special attacks.

With --interp-targets, the build must have been emitted with AOT deny gates.
Pass --compare to a previous compiled run to compare gameplay and full snapshots.
"""
from __future__ import annotations
import argparse
from concurrent.futures import ThreadPoolExecutor
import hashlib
import json
from pathlib import Path

from test_qol_rom_removal import ROM_SHA, prefix, read_rows, run_case

PROBE = "079B:2,0998:2,0AF6:2,0AFA:2,0CD0:2,0C18:2,0C1A:2,0C1C:2,0C1E:2,0C20:2,0CCC:2,09A6:2,09D2:2,09CE:2,0CCE:2"


def snapshot_word(root, tag, address):
    data = (root / (tag + ".wram.bin")).read_bytes()
    return int.from_bytes(data[address:address + 2], "little")


def scenarios():
    cases = []
    walk = prefix() + "press right 70\nwait 30\n"
    for mode in ("stock", "auto", "no-charge"):
        equip = "0000" if mode == "no-charge" else "0010"
        script = walk + f"press left 2\nwait 20\npoke 9a6 {equip}\npoke 9a8 {equip}\nwait 90\ndump idle\n"
        script += "".join(f"press x 1\nwait 35\ndump shot{n}\n" for n in (1, 2, 3))
        script += "wait 90\ndump recharged\npress x 1\nwait 8\ndump shot4\nquit\n"
        features = [] if mode == "stock" else ["super-metroid.combat/auto-charge"]
        cases.append((mode, features, script, PROBE))
    for mode, button, hud in (("native-pb", "x", "0300"), ("select", "select", "0000")):
        script = walk + ("poke 9a6 0110\npoke 9a8 0110\npoke 9ce 0500\npoke 9d0 0500\n"
                         f"poke 9d2 {hud}\nwait 150\ndump idle\npress {button} 1\nwait 8\ndump special\nquit\n")
        features = ["super-metroid.combat/auto-charge"]
        if mode == "select": features.append("super-metroid.combat/beam-combo-select")
        cases.append((mode, features, script, PROBE))
    return cases


def validate(out):
    result = {}
    for mode in ("stock", "auto", "no-charge"):
        root = out / mode
        idle = json.loads((root / "idle.info.json").read_text())["frame"]
        data = read_rows(root / "wram.csv")
        before = [r for r in data if idle - 80 <= r["frame"] <= idle]
        slots = ("0c18", "0c1a", "0c1c", "0c1e", "0c20")
        assert all(not any(r[key] for key in slots) for r in before), "idle emitted a shot"
        charge = max(r["0cd0"] for r in before)
        assert (60 <= charge <= 120) if mode == "auto" else charge == 0
        if mode == "auto": assert snapshot_word(root, "recharged", 0xcd0) == 120
        previous = [0] * 5; shots = []
        for row in data:
            if row["frame"] < idle: continue
            for i, key in enumerate(slots):
                value, old = row[key], previous[i]
                if value and not value & 0xf00 and (not old or old & 0xf00 or (value ^ old) & 0x10):
                    shots.append({"frame": row["frame"], "slot": i, "type": value, "charged": bool(value & 0x10)})
                previous[i] = value
        assert [s["charged"] for s in shots] == ([True, False, False, True] if mode == "auto" else [False] * 4)
        result[mode] = {"shots": shots, "idle_charge": charge, "idle_shots": 0}
    for mode in ("native-pb", "select"):
        root = out / mode
        assert snapshot_word(root, "idle", 0x9ce) == 5
        assert snapshot_word(root, "idle", 0xcce) == 0
        assert snapshot_word(root, "idle", 0xcd0) == 120
        assert snapshot_word(root, "special", 0x9ce) == 4
        assert snapshot_word(root, "special", 0xcce) == 4
        assert snapshot_word(root, "special", 0x9d2) == (3 if mode == "native-pb" else 0)
        result[mode] = {"idle_power_bombs": 5, "spent": 1, "special_projectiles": 4}
    return result


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    for name in ("build", "rom", "out"): ap.add_argument("--" + name, type=Path, required=True)
    ap.add_argument("--interp-targets", type=Path)
    ap.add_argument("--compare", type=Path)
    ap.add_argument("--jobs", type=int, default=2)
    a = ap.parse_args()
    a.out = a.out.resolve(); a.out.mkdir(parents=True, exist_ok=False)
    assert hashlib.sha256(a.rom.read_bytes()).hexdigest() == ROM_SHA
    def run(case):
        return run_case(case, build=a.build.resolve(), rom=a.rom.resolve(), out=a.out, interp_targets=a.interp_targets)
    with ThreadPoolExecutor(max_workers=max(1, a.jobs)) as pool:
        identities = dict(pool.map(run, scenarios()))
    result = {"cases": validate(a.out), "identities": identities}
    if a.compare:
        reference = a.compare.resolve()
        equality = {}
        for case in scenarios():
            name = case[0]
            assert read_rows(reference / name / "wram.csv") == read_rows(a.out / name / "wram.csv")
            for current in (a.out / name).glob("*.wram.bin"):
                tag = current.name[:-len("wram.bin")]
                for ext in ("wram.bin", "sram.bin", "vram.bin", "cgram.bin", "oam.bin", "fb.bgrx"):
                    path = current.with_name(tag + ext)
                    assert path.read_bytes() == (reference / name / path.name).read_bytes(), path.name
            equality[name] = "framewise gameplay fields and all memory/render snapshots identical"
        original = json.loads((reference / "results.json").read_text())
        assert identities["auto"]["execution_share"]["interp_insns"] > original["identities"]["auto"]["execution_share"]["interp_insns"]
        result["comparison"] = equality
    (a.out / "results.json").write_text(json.dumps(result, indent=2))
    print(json.dumps(result["cases"], indent=2))


if __name__ == "__main__": main()
