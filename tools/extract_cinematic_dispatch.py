#!/usr/bin/env python3
"""Extract the cinematic indirect-dispatch catalogue for bank $8B.

Cross-references three ground-truth sources:
  1. The snesrev/sm decomp's Call* dispatcher switches (behavioral
     oracle: which handler addresses each pointer-dispatch site can
     legally reach) — parsed from src/sm_8b.c + the fn* address
     constants in src/funcs.h.
  2. The ROM itself (literal oracle): every indirect-control-transfer
     opcode in bank $8B (6C JMP (abs), 7C JMP (abs,X), FC JSR (abs,X),
     DC JML [abs]) with its operand, plus detection of the
     `PEA <resume-1>` ptr-call idiom immediately preceding a JMP.
  3. The game cfg's func decls (recomp/bank0b.cfg) to attribute each
     site to its containing function.

Output: one line per site, ready to be turned into
`indirect_dispatch <site> <count> ptrcall targets:<...>` cfg lines.
The Call*->site pairing is by containing function name match between
the cfg decl and the decomp function that invokes the Call* wrapper —
printed for human review, NOT auto-written to the cfg.

Usage:
    python tools/extract_cinematic_dispatch.py \
        --rom "Super Metroid (Japan, USA) (En,Ja).sfc" \
        --decomp F:/Projects/sm --cfg recomp/bank0b.cfg
"""
import argparse
import pathlib
import re
import sys

BANK = 0x8B

INDIRECT_OPS = {
    0x6C: ('JMP (abs)', 3),
    0x7C: ('JMP (abs,X)', 3),
    0xFC: ('JSR (abs,X)', 3),
    0xDC: ('JML [abs]', 3),
}


def lorom_off(bank: int, pc: int) -> int:
    return ((bank & 0x7F) * 0x8000) + (pc - 0x8000)


def parse_fn_defines(funcs_h: pathlib.Path) -> dict:
    """fn<Name> -> 24-bit address, for bank $8B constants."""
    out = {}
    pat = re.compile(r'#define\s+(fn\w+)\s+0x([0-9A-Fa-f]{6})')
    for line in funcs_h.read_text(encoding='utf-8', errors='replace').splitlines():
        m = pat.match(line.strip())
        if m:
            out[m.group(1)] = int(m.group(2), 16)
    return out


def parse_call_switches(sm_8b: pathlib.Path, fn_addrs: dict) -> dict:
    """CallXxx -> sorted unique list of 16-bit handler PCs (bank $8B only)."""
    text = sm_8b.read_text(encoding='utf-8', errors='replace')
    out = {}
    # Function bodies: from `Call<Name>(` definition to the next closing
    # brace at column 0. Good enough for the generated dispatcher shape.
    for m in re.finditer(r'^[\w \*]*\b(Call\w+)\s*\([^)]*\)\s*\{', text, re.M):
        name = m.group(1)
        body_start = m.end()
        body_end = text.find('\n}', body_start)
        body = text[body_start:body_end]
        targets = set()
        for cm in re.finditer(r'case\s+(fn\w+)\s*:', body):
            fn = cm.group(1)
            addr = fn_addrs.get(fn)
            if addr is None:
                print(f"  !! {name}: no address for {fn}", file=sys.stderr)
                continue
            if (addr >> 16) & 0xFF != BANK:
                print(f"  !! {name}: {fn} = ${addr:06X} outside bank "
                      f"${BANK:02X}", file=sys.stderr)
                continue
            targets.add(addr & 0xFFFF)
        if targets:
            out[name] = sorted(targets)
    return out


def parse_cfg_funcs(cfg: pathlib.Path):
    """[(start16, end16, name)] sorted by start."""
    out = []
    pat = re.compile(r'^func\s+(\w+)\s+([0-9A-Fa-f]+)(?:\s+end:([0-9A-Fa-f]+))?')
    for line in cfg.read_text().splitlines():
        m = pat.match(line.strip())
        if m:
            name = m.group(1)
            start = int(m.group(2), 16)
            end = int(m.group(3), 16) if m.group(3) else start + 1
            out.append((start, end, name))
    out.sort()
    return out


def containing_func(funcs, pc):
    best = None
    for start, end, name in funcs:
        if start <= pc < end:
            return name
        if start <= pc and (best is None or start > best[0]):
            best = (start, end, name)
    return f"<gap, nearest {best[2]} ${best[0]:04X}>" if best else "<none>"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--rom', required=True)
    ap.add_argument('--decomp', required=True)
    ap.add_argument('--cfg', required=True)
    args = ap.parse_args()

    rom = pathlib.Path(args.rom).read_bytes()
    decomp = pathlib.Path(args.decomp)
    fn_addrs = parse_fn_defines(decomp / 'src' / 'funcs.h')
    switches = parse_call_switches(decomp / 'src' / 'sm_8b.c', fn_addrs)
    funcs = parse_cfg_funcs(pathlib.Path(args.cfg))

    print("== decomp Call* dispatcher target sets (bank $8B) ==")
    for name, targets in sorted(switches.items()):
        tl = ','.join(f"{t:04X}" for t in targets)
        print(f"  {name}: count={len(targets)} targets:{tl}")

    print("\n== bank $8B indirect-transfer sites (ROM scan) ==")
    # Linear opcode-agnostic byte scan: flags every candidate; mis-aligned
    # data hits are possible — review against the containing function.
    lo, hi = 0x8000, 0x10000
    for pc in range(lo, hi - 3):
        op = rom[lorom_off(BANK, pc)]
        if op not in INDIRECT_OPS:
            continue
        mnem, ln = INDIRECT_OPS[op]
        operand = rom[lorom_off(BANK, pc + 1)] | (rom[lorom_off(BANK, pc + 2)] << 8)
        # Only WRAM-pointer operands are plausible runtime-pointer
        # dispatches (DP/low WRAM mirror). ROM-table operands ($8000+)
        # are the decoder's ordinary table-walk problem, skip.
        if operand >= 0x2000:
            continue
        pea = ''
        if op == 0x6C and rom[lorom_off(BANK, pc - 3)] == 0xF4:
            pea_operand = (rom[lorom_off(BANK, pc - 2)]
                           | (rom[lorom_off(BANK, pc - 1)] << 8))
            resume = (pea_operand + 1) & 0xFFFF
            kind = 'next-block' if resume == pc + 3 else f'LOOPBACK ${resume:04X}'
            pea = f"  PEA ${pea_operand:04X} -> resume {kind}"
        fn = containing_func(funcs, pc)
        print(f"  ${BANK:02X}:{pc:04X}  {mnem} (${operand:04X})  in {fn}{pea}")


if __name__ == '__main__':
    main()
