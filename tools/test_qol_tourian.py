#!/usr/bin/env python3
"""Validate both Tourian exit gates and re-entry using real door transitions.

Room teleports position the test. The baby drains Samus naturally; only then
is health restored for route protection. Boss/door success flags are never set.
Re-entry uses a normal door load to exercise state/music/population selection.
"""
from __future__ import annotations
import argparse
from concurrent.futures import ThreadPoolExecutor
import hashlib
import json
from pathlib import Path
from test_qol_rom_removal import ROM_SHA, prefix, read_rows, run_case

PROBE = "079B:2,079F:2,0998:2,09C2:2,D82D:1,0AF6:2,0AFA:2,0A1C:2,07C9:2,07CB:2,07CF:2,07BB:2,0F78:2,1C85:2,1E15:2,1E65:2"


def scenarios():
    base = prefix() + ("poke 99c 7de1\npoke 78d 2caa\npoke 998 0900\n"
        "until16 79b == dcb1 3000\nuntil16 998 == 8 3000\n"
        "poke af6 c003\npoke afa 8000\npoke b10 c003\npoke b14 8000\n"
        "wait 60\npress left 120\npress left+a 25\npress left 200\nwait 1800\n"
        "dump drained\nuntil16 a1c == 2 3000\ndump recovered\n"
        "poke 9c4 e703\npoke 9c2 e703\n"
        "poke af6 b003\npoke b10 b003\npress right 8\npress right+a 12\n"
        "press right+x 60\npress right+a 60\nwait 120\ndump big_exit\n")
    result = [("stock", [], base + "quit\n", PROBE)]
    # Actual PLM geometry: Big Boy door x62/y6, Dust Torizo x30/y6.
    mod = base + ("until16 79b == dc65 1200\ndump dust_entry\n"
        "poke af6 b001\npoke b10 b001\npress right 8\npress right+a 12\n"
        "press right+x 60\npress right+a 60\nwait 120\ndump dust_exit\n"
        "until16 79b == dc19 1200\npoke 99c 7de1\npoke 78d 2caa\npoke 998 0900\n"
        "until16 79b == dcb1 3000\nuntil16 998 == 8 3000\nwait 120\ndump big_reentry\nquit\n")
    result.append(("mod", ["super-metroid.balance-fixes/tourian-exit"], mod, PROBE))
    return result


def validate(out, rom):
    def ram(mode, tag): return (out / mode / (tag + ".wram.bin")).read_bytes()
    def word(data, at): return int.from_bytes(data[at:at + 2], "little")
    for mode in ("stock", "mod"):
        drained = ram(mode, "drained"); recovered = ram(mode, "recovered")
        assert word(drained, 0x9c2) == 1 and word(recovered, 0xa1c) == 2
        assert bool(drained[0xd82d] & 4) == (mode == "mod")
    assert word(ram("stock", "big_exit"), 0x79b) == 0xdcb1
    assert word(ram("mod", "big_exit"), 0x79b) == 0xdc65
    assert word(ram("mod", "dust_exit"), 0x79b) == 0xdc19
    final = ram("mod", "big_reentry")
    assert word(final, 0x79b) == 0xdcb1 and word(final, 0x998) == 8 and word(final, 0x9c2) > 0
    assert word(final, 0x7bb) == 0xdcdd and word(final, 0x7cf) == 0xe28e
    assert word(final, 0x7cb) == 0x1e and word(final, 0x7c9) == 5
    image = rom.read_bytes()
    offset = (0x21 * 0x8000) + (0xe26e & 0x7fff)
    removed = [word(image, offset), word(image, offset + 16)]
    expected = []
    at = offset + 32
    while word(image, at) != 0xffff:
        expected.append(word(image, at))
        at += 16
    loaded = [word(final, addr) for addr in range(0xf78, 0x1778, 0x40) if word(final, addr)]
    # The live and corpse sidehoppers share an enemy definition. Check the
    # complete shortened population, including order/count, rather than
    # rejecting an ID that the retained corpse legitimately uses.
    assert loaded == expected, "re-entry did not load exactly the corpse population"
    dust = ram("mod", "dust_entry")
    assert word(dust, 0x1e15) == 0xa4 and word(dust, 0x1e65) == 4
    return {"natural_drain_health": 1, "stock_exit_room": "DCB1", "mod_exit_rooms": ["DC65", "DC19"],
            "reentry": {"room": "DCB1", "state": "DCDD", "population": "E28E", "music": "1E/5", "alive": True},
            "removed_enemy_ids": removed, "loaded_enemy_ids": loaded,
            "setup_bypasses": ["room positioning", "health restored after natural drain", "door-load teleport for re-entry"]}


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    for name in ("build", "rom", "out"): ap.add_argument("--" + name, type=Path, required=True)
    ap.add_argument("--interp-targets", type=Path)
    ap.add_argument("--compare", type=Path)
    a = ap.parse_args(); a.out = a.out.resolve(); a.out.mkdir(parents=True, exist_ok=False)
    assert hashlib.sha256(a.rom.read_bytes()).hexdigest() == ROM_SHA
    def run(case): return run_case(case, build=a.build.resolve(), rom=a.rom.resolve(), out=a.out, interp_targets=a.interp_targets)
    with ThreadPoolExecutor(max_workers=2) as pool: identities = dict(pool.map(run, scenarios()))
    result = {"observations": validate(a.out, a.rom), "identities": identities}
    if a.compare:
        reference = a.compare.resolve()
        for name, _, _, _ in scenarios():
            assert read_rows(reference / name / "wram.csv") == read_rows(a.out / name / "wram.csv")
            for path in (a.out / name).glob("*.wram.bin"):
                assert path.read_bytes() == (reference / name / path.name).read_bytes()
        result["comparison"] = "framewise gameplay fields and complete WRAM snapshots identical"
        original = json.loads((reference / "results.json").read_text())
        assert identities["mod"]["execution_share"]["interp_insns"] > original["identities"]["mod"]["execution_share"]["interp_insns"]
    (a.out / "results.json").write_text(json.dumps(result, indent=2))
    print(json.dumps(result["observations"], indent=2))


if __name__ == "__main__": main()
