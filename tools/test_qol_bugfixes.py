#!/usr/bin/env python3
"""Exercise jump, invincibility and automatic reserve fixes in the built game.

Fresh isolated profiles; muted launches; RAM positioning/equipment/health cheats
reach the test conditions. Damage, contacts and refill results run normally.
The companion sm_bugfix_test covers individual hook arithmetic/edge cases.
"""
from __future__ import annotations
import argparse
from concurrent.futures import ThreadPoolExecutor
import hashlib
import json
from pathlib import Path
from test_qol_rom_removal import ROM_SHA, prefix, read_rows, run_case

PROBE = "079B:2,0998:2,09C2:2,09C4:2,09D6:2,0A4E:2,18A8:2,0B2C:2,0B2E:2,0B42:2,0B44:2,0A1F:1,0A6E:2,EF78:2"


def scenarios():
    base = prefix()
    scripts = {
        "sbjump": base + "poke 9a2 0020\npoke 9a4 0020\npress right+b 100\npress right+b+a 10\nwait 60\ndump final\nquit\n",
        "iframes": base + "poke d820 01\npoke 79f 0100\npoke 78b 0900\npoke d914 0500\npoke 998 0600\nuntil16 998 == 8 3000\nwait 200\npoke 9a2 0800\npoke 9a4 0800\npress left 20\nwait 30\npokefor fba 4005 40\npokefor fbe a002 40\npokefor 18a8 6400 1\nwait 2\npress left+a 3\npress left 60\nwait 200\ndump final\nquit\n",
        # Do not poison EF78 with an arbitrary sprite instruction pointer:
        # it executes before the clear, invalidating the original fixture.
        "reserve": base + "poke 79f 0200\npoke 78b 0a00\npoke d914 0500\npoke 998 0600\nuntil16 998 == 8 3000\nwait 60\npoke 9c0 0100\npoke 9d4 2c01\npoke 9d6 2c01\npoke 9c2 0200\nuntil16 998 == 1b 1000\npoke 18a8 3c00\nwait 50\ndump mid_refill\nuntil16 998 == 8 600\ndump refill_done\nwait 30\ndump final\nquit\n",
    }
    cases = []
    for name, script in scripts.items():
        feature = "reserve-tanks" if name == "reserve" else "bug-fixes"
        for mode in ("stock", "mod"):
            cases.append((name + "-" + mode, [] if mode == "stock" else
                          ["super-metroid.balance-fixes/" + feature], script, PROBE))
    return cases


def validate(out):
    pairs = {name: [read_rows(out / (name + "-" + mode) / "wram.csv")
                    for mode in ("stock", "mod")] for name in ("sbjump", "iframes", "reserve")}
    stock, mod = pairs["sbjump"]
    jumps = [(a, b) for a, b in zip(stock, mod) if a["0b2c"] != b["0b2c"]]
    assert jumps and jumps[0][0]["0b2c"] == 0xf000 and jumps[0][1]["0b2c"] == 0xe800
    assert jumps[0][0]["0b2e"] == jumps[0][1]["0b2e"] == 4
    stock, mod = pairs["iframes"]
    hits = [(a, b) for a, b in zip(stock, mod) if a["18a8"] != b["18a8"]]
    assert hits and hits[0][0]["18a8"] == 0 and hits[0][1]["18a8"] == 92
    assert hits[0][0]["0a6e"] == hits[0][1]["0a6e"] == 3
    observations = {"jump_speed_stock": "4.F000", "jump_speed_mod": "4.E800",
                    "screw_contact_iframes_stock": 0, "screw_contact_iframes_mod": 92}
    for mode in ("stock", "mod"):
        b = (out / ("reserve-" + mode) / "final.wram.bin").read_bytes()
        mid = (out / ("reserve-" + mode) / "mid_refill.wram.bin").read_bytes()
        def w(data, at): return int.from_bytes(data[at:at+2], "little")
        assert w(b, 0x998) == 8 and w(b, 0x9c2) > 0
        assert w(b, 0x9d6) == (201 if mode == "mod" else 0)
        assert w(mid, 0x998) == 27
        assert w(mid, 0x18a8) == (60 if mode == "mod" else 8)
        observations["reserve-" + mode] = {"leftover": w(b, 0x9d6), "mid_refill_iframes": w(mid, 0x18a8)}
    return observations


def compare(out, reference, identities):
    comparison = {}
    original = json.loads((reference / "results.json").read_text())
    for name, _, _, _ in scenarios():
        assert read_rows(reference / name / "wram.csv") == read_rows(out / name / "wram.csv")
        differences = {}
        for path in (out / name).glob("*.wram.bin"):
            current = path.read_bytes(); before = (reference / name / path.name).read_bytes()
            changed = [i for i, (a, b) in enumerate(zip(before, current)) if a != b]
            assert len(current) == len(before)
            if changed:
                # Stock-only sound queue timing discrepancy; the corrected
                # reserve path and both other routes match the whole snapshot.
                assert name == "reserve-stock" and set(changed) <= {0x64b, 0x64f, 0x652}
                differences[path.name] = {hex(i): [before[i], current[i]] for i in changed}
        assert identities[name]["execution_share"]["interp_insns"] > original["identities"][name]["execution_share"]["interp_insns"]
        comparison[name] = {"framewise_gameplay_fields": "identical", "snapshot_differences": differences}
    return comparison


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    for name in ("build", "rom", "out"): ap.add_argument("--" + name, type=Path, required=True)
    ap.add_argument("--interp-targets", type=Path)
    ap.add_argument("--compare", type=Path)
    a = ap.parse_args(); a.out = a.out.resolve(); a.out.mkdir(parents=True, exist_ok=False)
    assert hashlib.sha256(a.rom.read_bytes()).hexdigest() == ROM_SHA
    def run(c): return run_case(c, build=a.build.resolve(), rom=a.rom.resolve(), out=a.out, interp_targets=a.interp_targets)
    with ThreadPoolExecutor(max_workers=2) as pool: identities = dict(pool.map(run, scenarios()))
    observations = validate(a.out)
    result = {"observations": observations, "identities": identities}
    if a.compare:
        result["comparison"] = compare(a.out, a.compare, identities)
    (a.out / "results.json").write_text(json.dumps(result, indent=2))
    print(json.dumps(observations, indent=2))


if __name__ == "__main__": main()
