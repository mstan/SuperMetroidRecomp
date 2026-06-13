"""Find ROM indirect-dispatch sites and emit the indirect_dispatch worklist.

Super Metroid's game-state / object / menu machines dispatch through indexed
jump tables: `JSR ($tbl,X)` (FC) / `JMP ($tbl,X)` (7C), plus single-pointer
`JMP ($tbl)` (6C) / `JML [$tbl]` (DC). Each such site needs an
`indirect_dispatch` cfg directive or the recompiler suppresses it
(cfg-required-dispatch-or-kill) and the machine never runs. This tool finds
those sites so the worklist can be authored from ground truth.

A bare opcode scan over un-disassembled ROM is hopeless (FC/7C/6C/DC occur
constantly in data/operands). The reliable validator is the recompiler's OWN
decode: `g_dispatch_table[]` in src/gen/dispatch_v2.c lists every known
function entry. A genuine jump table's reconstructed pointers ARE function
entries — so a candidate site is CONFIRMED only when the table it points at
is a run of in-bank pointers, several of which hit known function entries.
This grounds the scan in the decoder and all but eliminates false positives.

For each confirmed site the tool reconstructs the table length (run of
in-bank pointers) and cross-references:
  - existing `indirect_dispatch` directives in recomp/*.cfg (already authorized)
  - the decomp func-ptr table names from recomp/sm_decomp_symbols.json (enrich)

Output: recomp/sm_dispatch_worklist.json + a stderr summary. Note: the
reconstructed count is a heuristic (run length); verify against the
disassembly / decomp index range before authorizing.

Usage (from the SuperMetroidRecomp repo root):
    python tools/correlate_dispatch.py
        [--rom "Super Metroid (Japan, USA) (En,Ja).sfc"]
        [--gen src/gen/dispatch_v2.c] [--symbols recomp/sm_decomp_symbols.json]
        [--cfg-dir recomp] [--out recomp/sm_dispatch_worklist.json]
        [--min-hits 3]
"""
from __future__ import annotations

import argparse
import json
import re
import sys
from collections import defaultdict
from pathlib import Path
from typing import Dict, List, Set

_INDEXED = {0xFC: "JSR ($abs,X)", 0x7C: "JMP ($abs,X)"}
_SINGLE = {0x6C: "JMP ($abs)", 0xDC: "JML [$abs]"}
_ALL_OPS = {**_INDEXED, **_SINGLE}

_MAX_TABLE = 256   # cap table-length reconstruction
_PROBE = 8         # how many leading entries to test against known funcs


def rom_offset(bank: int, addr16: int) -> int:
    return (bank & 0x7F) * 0x8000 + (addr16 & 0x7FFF)


def parse_known_funcs(gen_path: Path) -> Dict[int, Set[int]]:
    """bank -> {addr16} of every entry in g_dispatch_table[]."""
    known: Dict[int, Set[int]] = defaultdict(set)
    entry_re = re.compile(r'\{\s*0x([0-9A-Fa-f]{6})u,')
    for m in entry_re.finditer(gen_path.read_text(encoding="utf-8",
                                                   errors="replace")):
        pc24 = int(m.group(1), 16)
        known[(pc24 >> 16) & 0x7F].add(pc24 & 0xFFFF)
    return known


def load_existing_dispatch(cfg_dir: Path) -> Set:
    sites = set()
    site_re = re.compile(r'^indirect_dispatch\s+([0-9a-fA-F]+)\b')
    bank_re = re.compile(r'^bank\s*=\s*([0-9a-fA-F]+)', re.IGNORECASE)
    for cfg in sorted(cfg_dir.glob("bank*.cfg")):
        bank = None
        for ln in cfg.read_text(encoding="utf-8", errors="replace").splitlines():
            mb = bank_re.match(ln.strip())
            if mb:
                bank = int(mb.group(1), 16)
                continue
            ms = site_re.match(ln.strip())
            if ms and bank is not None:
                sites.add((bank, int(ms.group(1), 16) & 0xFFFF))
    return sites


def reconstruct_table(rom: bytes, bank: int, tbl: int):
    """Return (entries[], hits_in_first_PROBE) reading 16-bit LE pointers at
    (bank, tbl) while they stay in $8000-$FFFF."""
    entries = []
    base = rom_offset(bank, tbl)
    for k in range(_MAX_TABLE):
        off = base + 2 * k
        if off + 1 >= len(rom):
            break
        w = rom[off] | (rom[off + 1] << 8)
        if not (0x8000 <= w <= 0xFFFF):
            break
        entries.append(w)
    return entries


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--rom", default="Super Metroid (Japan, USA) (En,Ja).sfc")
    ap.add_argument("--gen", default="src/gen/dispatch_v2.c")
    ap.add_argument("--symbols", default="recomp/sm_decomp_symbols.json")
    ap.add_argument("--cfg-dir", default="recomp")
    ap.add_argument("--out", default="recomp/sm_dispatch_worklist.json")
    ap.add_argument("--min-hits", type=int, default=3,
                    help="min leading table entries that must be known function "
                         "entries to confirm a site")
    args = ap.parse_args()

    rom = Path(args.rom).read_bytes()
    if len(rom) % 0x8000 == 0x200:
        rom = rom[0x200:]
    known = parse_known_funcs(Path(args.gen))
    existing = load_existing_dispatch(Path(args.cfg_dir))
    sym_tables = {}
    for t in json.loads(Path(args.symbols).read_text())["func_ptr_tables"]:
        sym_tables[(t["bank"], t["addr16"])] = (t["name"], t["count"])

    n_banks = (len(rom) + 0x7FFF) // 0x8000
    sites: List[Dict] = []
    for bank in range(n_banks):
        kb = known.get(bank, set())
        if not kb:
            continue
        base = bank * 0x8000
        bank_bytes = rom[base:base + 0x8000]
        for i in range(len(bank_bytes) - 2):
            op = bank_bytes[i]
            if op not in _ALL_OPS:
                continue
            tbl = bank_bytes[i + 1] | (bank_bytes[i + 2] << 8)
            if not (0x8000 <= tbl <= 0xFFFF):
                continue
            entries = reconstruct_table(rom, bank, tbl)
            if not entries:
                continue
            hits = sum(1 for w in entries[:_PROBE] if w in kb)
            indexed = op in _INDEXED
            # single-indirect (6C/DC) only needs its one pointer to be a func;
            # indexed needs several leading entries to look like a table.
            need = 1 if not indexed else args.min_hits
            if hits < need:
                continue
            pc = 0x8000 + i
            name, dcount = sym_tables.get((bank, tbl), (None, None))
            if name is None:
                # Indexed dispatch often points a few bytes into the decomp's
                # table label (pre-scaled index base). Attribute to the nearest
                # decomp table at-or-below the operand within its span.
                best = None
                for (b, a), (nm, c) in sym_tables.items():
                    if b != bank or a > tbl:
                        continue
                    span = 2 * (c or 0) + 4
                    if tbl - a <= span and (best is None or a > best[0]):
                        best = (a, nm, c, tbl - a)
                if best is not None:
                    name = f"{best[1]}+{best[3]:#x}" if best[3] else best[1]
                    if dcount is None and best[2]:
                        dcount = best[2]
            count = dcount if dcount else (1 if not indexed else len(entries))
            sites.append({
                "site_bank": bank,
                "site_pc": f"{pc:04x}",
                "site_pc24": f"{(bank | 0x80):02x}{pc:04x}",
                "opcode": _ALL_OPS[op],
                "table_addr": f"{tbl:04x}",
                "table_name": name,
                "indexed": indexed,
                "recon_len": len(entries),
                "probe_hits": hits,
                "count": count,
                "first_targets": [f"{w:04x}" for w in entries[:6]],
                "already_authorized": (bank, pc) in existing,
                "suggested": (
                    f"indirect_dispatch {pc:04x} {count} idx:X tables:{tbl:04x}"
                    if indexed else
                    f"# {_ALL_OPS[op]} @ ${(bank|0x80):02x}{pc:04x} -> ptr ${tbl:04x} "
                    f"(single indirect; author by hand)"),
            })

    sites.sort(key=lambda c: (c["site_bank"], c["site_pc"]))
    new_idx = [s for s in sites if s["indexed"] and not s["already_authorized"]]
    payload = {
        "_comment": (
            "Indirect-dispatch SITES found by scanning ROM for FC/7C/6C/DC and "
            "confirming the operand points at a jump table whose entries are "
            "known function entries (g_dispatch_table). 'count' is a heuristic "
            "run-length (or the decomp table count when matched) — verify "
            "against the disassembly before authorizing. indexed=true sites are "
            "the indirect_dispatch idx:X worklist."),
        "summary": {
            "sites_total": len(sites),
            "indexed_sites": sum(1 for s in sites if s["indexed"]),
            "single_indirect_sites": sum(1 for s in sites if not s["indexed"]),
            "indexed_already_authorized":
                sum(1 for s in sites if s["indexed"] and s["already_authorized"]),
            "indexed_new_worklist": len(new_idx),
            "named_by_decomp": sum(1 for s in sites if s["table_name"]),
        },
        "sites": sites,
    }
    Path(args.out).write_text(json.dumps(payload, indent=2), encoding="utf-8")
    s = payload["summary"]
    print(f"{s['sites_total']} confirmed dispatch sites "
          f"({s['indexed_sites']} indexed, {s['single_indirect_sites']} single); "
          f"indexed: {s['indexed_already_authorized']} authorized, "
          f"{s['indexed_new_worklist']} NEW; "
          f"{s['named_by_decomp']} named by decomp. wrote {args.out}",
          file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
