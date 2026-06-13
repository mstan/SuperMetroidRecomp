"""Harvest symbols from the snesrev/sm decomp into Super Metroid cfgs.

The decomp at https://github.com/snesrev/sm annotates every function
definition AND every named ROM table with its original SNES PC in a
trailing `// 0x` comment, e.g.:

    void VerifySRAM(void) {  // 0x808261
    static Func_V *const kIrqHandlers[14] = {  // 0x80986A
    static const uint16 kHudTilemaps[32] = {  // 0x8099CF

This is the SM counterpart to tools/ingest_zelda3_decomp.py. It differs
from the zelda3 ingester in two ways:

  1. Comment format. zelda3 writes `// 8080c9` (bare 6 hex); sm writes
     `// 0x808261` (0x-prefixed). The DEFN_RE here matches the 0x form.

  2. Three symbol classes, not one. The decomp names:
       - functions          -> emitted as `func <Name> <pc> end:<next>`
                               (active cfg directives; what gets recompiled)
       - func-pointer tables -> the indirect_dispatch TARGETS. Captured to
                               <output>/sm_decomp_symbols.json so the
                               dispatch-authorization worklist can be driven
                               from decomp ground truth (table addr + element
                               count + name) instead of guessed by hand.
       - data tables         -> captured to the same JSON (debug/readability).
     The table classes are NOT injected as cfg directives: they sit at DATA
     addresses, and emitting them as `func`/`name` could mislead the decoder
     into treating data as code. They are recorded as ground truth; turning a
     func-pointer table into an authorized `indirect_dispatch` still requires
     correlating it with the JSR/JMP (tbl,X) SITE in the ROM (a separate,
     ROM-scanning step).

The `func` sections are idempotent: each bank cfg's auto-ingested block is
delimited by markers and replaced wholesale on every run. Hand-written `func`
lines OUTSIDE the markers always win (their PC is suppressed from the auto
block) so attributes like end:/exit_mx/tail_call survive a regen. This
matches the zelda3 ingester's contract exactly.

LoROM bank mirror: sm's PC comments use the $80-$FF half of the mirror
(0x808261 = $80:8261); for cfg purposes these normalise to the $00-$7F
physical-bank form ($00:8261), the space the recompiler works in.

Usage (run from the SuperMetroidRecomp repo root):
    python snesrecomp/tools/ingest_sm_decomp.py
        [--decomp refs/snesrev-sm]
        [--output recomp]
        [--dry-run]
"""
from __future__ import annotations

import argparse
import json
import re
import sys
from collections import defaultdict
from pathlib import Path
from typing import Dict, List, Tuple

# Function-definition opening-brace line:
#   <ret-type words/stars...> <Name>(<args>) {  // 0x<pc6>
#
# SM has ~700 instruction-handler signatures with a two-word pointer
# return like `const uint16 *Foo(...)` that the zelda3 ingester's
# single-type-word pattern misses. Rather than a nested-quantifier
# pattern (which catastrophically backtracks on long non-matching lines),
# we capture the whole "head" before `(` with a single linear char-class
# and pluck the function name as the last identifier of the head in code
# (see harvest()). Control-flow heads (if/for/...) are filtered there.
DEFN_RE = re.compile(
    r'^\s*'
    r'(?P<head>[A-Za-z_][A-Za-z0-9_ \t\*]*?)'   # ret-type(s) + name (linear)
    r'\(\s*[^)]*\)\s*\{'                         # ( args ) {
    r'\s*//\s*0x'                                # // 0x
    r'(?P<pc>[0-9a-fA-F]{6})'                    # pc6
    r'\s*$'
)

# Heads whose trailing identifier is a control-flow keyword are not
# function definitions even if they carry a trailing `// 0x` comment.
_NOT_A_FUNC_NAME = {
    "if", "for", "while", "switch", "do", "else", "return", "sizeof",
}

# A line that closes a signature: `...) {  // 0xXXXXXX`. Used to trigger the
# backward multi-line join in harvest().
_CLOSE_RE = re.compile(r'\)\s*\{\s*//\s*0x[0-9a-fA-F]{6}\s*$')

# Named-table opening-brace line. Two flavours:
#   func-ptr: static Func_V *const kName[14] = {  // 0x80986A
#   data:     static const uint16 kName[32]  = {  // 0x8099CF
TABLE_RE = re.compile(
    r'^\s*(?:static\s+)?'
    r'(?:const\s+)?'                       # optional leading const (data)
    r'(?P<type>[A-Za-z_]\w*)'              # type word (e.g. Func_V, uint16)
    r'(?:\s*\*\s*const\s+|\s+)'            # '*const ' (func-ptr) | ws (data)
    r'(?P<name>[A-Za-z_]\w*)'              # table name
    r'\s*\[(?P<count>\d*)\]\s*=\s*\{'      # [N] = {
    r'\s*//\s*0x(?P<pc>[0-9a-fA-F]{6})\s*$'
)

INGEST_BEGIN = (
    "# >>> AUTO-INGESTED FROM sm DECOMP "
    "— do not hand-edit between markers >>>"
)
INGEST_END = "# <<< END AUTO-INGESTED <<<"

_SKIP_DIR_PARTS = {"assets", "other", ".git", ".github", "build", "saves"}


def harvest(decomp_root: Path):
    """Walk decomp .c files.

    Returns (funcs, tables):
      funcs  : list of (bank, addr16, name)
      tables : list of dict(addr24, bank, addr16, name, kind, count)
    """
    funcs: List[Tuple[int, int, str]] = []
    tables: List[Dict] = []
    seen_func_pc = set()
    seen_table_pc = set()
    for path in sorted(decomp_root.rglob("*.c")):
        if any(part in _SKIP_DIR_PARTS for part in path.parts):
            continue
        with open(path, encoding="utf-8", errors="replace") as fp:
            lines = fp.readlines()
            for i, line in enumerate(lines):
                mf = DEFN_RE.match(line)
                if not mf and _CLOSE_RE.search(line) and "= {" not in line:
                    # Multi-line signature: the opening `<type> Name(` is on a
                    # previous physical line. Join backward (collapsing
                    # whitespace) until DEFN_RE matches or the window is spent.
                    joined = line
                    for k in range(1, 6):
                        if i - k < 0:
                            break
                        joined = lines[i - k].rstrip("\n") + " " + joined
                        cand = re.sub(r"\s+", " ", joined).strip()
                        mf = DEFN_RE.match(cand)
                        if mf:
                            break
                if mf:
                    idents = re.findall(r'[A-Za-z_]\w*', mf.group("head"))
                    if not idents or idents[-1] in _NOT_A_FUNC_NAME:
                        continue
                    name = idents[-1]
                    pc24 = int(mf.group("pc"), 16)
                    if pc24 in seen_func_pc:
                        continue
                    seen_func_pc.add(pc24)
                    bank = (pc24 >> 16) & 0x7F
                    addr = pc24 & 0xFFFF
                    if not (0x8000 <= addr <= 0xFFFF):
                        continue
                    funcs.append((bank, addr, name))
                    continue
                mt = TABLE_RE.match(line)
                if mt:
                    pc24 = int(mt.group("pc"), 16)
                    if pc24 in seen_table_pc:
                        continue
                    seen_table_pc.add(pc24)
                    bank = (pc24 >> 16) & 0x7F
                    addr = pc24 & 0xFFFF
                    if not (0x8000 <= addr <= 0xFFFF):
                        continue
                    kind = ("func_ptr" if mt.group("type").startswith("Func_")
                            else "data")
                    cnt = mt.group("count")
                    tables.append({
                        "addr24": pc24,
                        "bank": bank,
                        "addr16": addr,
                        "name": mt.group("name"),
                        "kind": kind,
                        "count": int(cnt) if cnt else None,
                    })
    return funcs, tables


def emit_per_bank(funcs, output_dir: Path, dry_run: bool = False) -> None:
    by_bank: Dict[int, List[Tuple[int, str]]] = defaultdict(list)
    for bank, addr, name in funcs:
        by_bank[bank].append((addr, name))

    ingest_section_re = re.compile(
        re.escape(INGEST_BEGIN) + r".*?" + re.escape(INGEST_END) + r"\n?",
        flags=re.DOTALL,
    )
    func_decl_re = re.compile(r'^\s*func\s+(\S+)\s+([0-9a-fA-F]+)\b')
    # A pristine auto-emitted func line: exactly `func <Name> <hex> end:<hex>`,
    # nothing else. Anything richer (extra attrs) is hand-authored.
    auto_func_re = re.compile(
        r'^func\s+\S+\s+[0-9a-fA-F]{1,4}\s+end:[0-9a-fA-F]{1,5}\s*$')
    block_re = re.compile(
        re.escape(INGEST_BEGIN) + r"\n(.*?)" + re.escape(INGEST_END),
        flags=re.DOTALL,
    )

    for bank in sorted(by_bank):
        items = sorted(by_bank[bank])
        seen = set()
        dedup: List[Tuple[int, str]] = []
        for addr, name in items:
            if addr in seen:
                continue
            seen.add(addr)
            dedup.append((addr, name))

        cfg_path = output_dir / f"bank{bank:02x}.cfg"
        harvest_pcs = {addr for addr, _ in dedup}
        hand_pcs: set = set()
        hand_block = ""
        if cfg_path.exists():
            existing = cfg_path.read_text(encoding="utf-8")
            hand_block = ingest_section_re.sub("", existing)
            for ln in hand_block.splitlines():
                m = func_decl_re.match(ln)
                if not m:
                    continue
                try:
                    hand_pcs.add(int(m.group(2), 16) & 0xFFFF)
                except ValueError:
                    pass

            # SAFETY GUARD. The marker contract is "auto func entries only
            # between markers". In practice the SM cfgs (Codex bring-up)
            # interleaved hand-tuned directives — indirect_dispatch, hle_func,
            # and `func` decls for nullsub dispatch targets the decomp omits —
            # INSIDE the markers. Wholesale-replacing such a block destroys
            # that work. Detect a "dirty" block and REFUSE to rewrite it,
            # rather than silently clobber. A dirty line is anything in the
            # block that is not blank, not a comment, not a pristine
            # `func <Name> <hex> end:<hex>` line, OR a pristine func line at a
            # PC the decomp does not define (a hand-added stub decl).
            bm = block_re.search(existing)
            if bm is not None:
                dirty = []
                for ln in bm.group(1).splitlines():
                    s = ln.strip()
                    if not s or s.startswith("#"):
                        continue
                    if auto_func_re.match(s):
                        pc = int(s.split()[2], 16) & 0xFFFF
                        if pc in harvest_pcs:
                            continue
                        dirty.append(ln)  # func at non-decomp PC = hand stub
                    else:
                        dirty.append(ln)  # directive / richer func line
                if dirty:
                    print(
                        f"SKIP {cfg_path}: marker block has {len(dirty)} "
                        f"hand-authored line(s) (e.g. {dirty[0].strip()[:60]!r}). "
                        f"Move them OUTSIDE the markers, then re-run.",
                        file=sys.stderr)
                    continue

        filtered = [(addr, name) for (addr, name) in dedup
                    if addr not in hand_pcs]

        section_lines = [
            INGEST_BEGIN,
            "# Source: snesrev/sm decomp PC comments.",
            "# Regenerate via: python tools/ingest_sm_decomp.py",
            f"# {len(filtered)} entries "
            f"({len(dedup) - len(filtered)} suppressed by hand-declared func).",
        ]
        for i, (addr, name) in enumerate(filtered):
            next_addr = filtered[i + 1][0] if i + 1 < len(filtered) else 0x10000
            section_lines.append(
                f"func {name} {addr:04x} end:{next_addr:04x}")
        section_lines.append(INGEST_END)
        new_section = "\n".join(section_lines) + "\n"

        if cfg_path.exists():
            existing = hand_block.rstrip() + "\n\n"
            new_content = existing + new_section
        else:
            new_content = (
                f"# bank{bank:02x}.cfg — auto-created by "
                f"ingest_sm_decomp.py\n\n"
                f"bank = {bank:02x}\n\n"
                f"{new_section}"
            )

        if dry_run:
            print(f"[dry-run] {cfg_path}: {len(filtered)} entries "
                  f"({len(dedup) - len(filtered)} suppressed)")
        else:
            cfg_path.write_text(new_content, encoding="utf-8")
            print(f"wrote {cfg_path}: {len(filtered)} entries "
                  f"({len(dedup) - len(filtered)} suppressed by hand-declared)")


def emit_tables_json(tables, output_dir: Path, dry_run: bool) -> None:
    """Write the func-ptr/data table symbols as machine-readable ground truth.

    func-ptr tables are the indirect_dispatch worklist input: each carries
    its ROM address, element count, and decomp name.
    """
    tables_sorted = sorted(tables, key=lambda t: t["addr24"])
    out = output_dir / "sm_decomp_symbols.json"
    payload = {
        "_comment": (
            "Named ROM tables harvested from the snesrev/sm decomp by "
            "ingest_sm_decomp.py. kind=func_ptr entries are indirect_dispatch "
            "targets (table addr + element count + name); kind=data are "
            "data-table labels. Addresses are normalised to $00-$7F physical "
            "banks (addr24 keeps the original $80+ mirror form)."
        ),
        "func_ptr_tables": [t for t in tables_sorted if t["kind"] == "func_ptr"],
        "data_tables": [t for t in tables_sorted if t["kind"] == "data"],
    }
    n_fp = len(payload["func_ptr_tables"])
    n_dt = len(payload["data_tables"])
    if dry_run:
        print(f"[dry-run] {out}: {n_fp} func-ptr tables, {n_dt} data tables")
    else:
        out.write_text(json.dumps(payload, indent=2), encoding="utf-8")
        print(f"wrote {out}: {n_fp} func-ptr tables, {n_dt} data tables")


_HAND_HEADER = (
    "# === Hand-maintained directives & stub func decls (relocated OUT of the\n"
    "# === AUTO-INGESTED block by `ingest_sm_decomp.py --migrate` so the func\n"
    "# === list below can be regenerated without clobbering this work. Keep\n"
    "# === indirect_dispatch / hle_func / non-decomp `func` stubs up here.) ===")
_AUTO_HEADER_RE = re.compile(
    r'^#\s*(Source:\s*snesrev|Regenerate via:|\d+\s+entries\b)')


def migrate(funcs, output_dir: Path, dry_run: bool = False) -> None:
    """Relocate hand-authored lines OUT of each bank's AUTO-INGESTED block.

    The SM cfgs (Codex bring-up) interleaved indirect_dispatch directives and
    non-decomp `func` stub decls inside the markers, which blocks a safe
    regen (the safety guard in emit_per_bank refuses to overwrite them). This
    mode rewrites such a bank so the marker block holds ONLY auto func lines
    (preserved verbatim) and the hand content moves above the markers. Pure
    reorganisation — no func is renamed, dropped, or re-bounded.
    """
    by_bank: Dict[int, set] = defaultdict(set)
    for bank, addr, _ in funcs:
        by_bank[bank].add(addr)

    auto_func_re = re.compile(
        r'^func\s+\S+\s+[0-9a-fA-F]{1,4}\s+end:[0-9a-fA-F]{1,5}\s*$')

    for bank in sorted(by_bank):
        harvest_pcs = by_bank[bank]
        cfg_path = output_dir / f"bank{bank:02x}.cfg"
        if not cfg_path.exists():
            continue
        existing = cfg_path.read_text(encoding="utf-8")
        if INGEST_BEGIN not in existing or INGEST_END not in existing:
            continue
        head = existing[:existing.index(INGEST_BEGIN)]
        inner = existing[existing.index(INGEST_BEGIN) + len(INGEST_BEGIN):
                         existing.index(INGEST_END)]
        tail = existing[existing.index(INGEST_END) + len(INGEST_END):]

        auto_keep, hand_keep = [], []
        for ln in inner.splitlines():
            s = ln.strip()
            if not s:
                continue
            if _AUTO_HEADER_RE.match(s):
                continue  # regenerated below
            if auto_func_re.match(s):
                pc = int(s.split()[2], 16) & 0xFFFF
                (auto_keep if pc in harvest_pcs else hand_keep).append(ln)
            else:
                hand_keep.append(ln)

        # Only migrate banks with real regen-blockers (directives or non-decomp
        # `func` stubs). A block containing only stray comments is not a
        # blocker (the guard skips comments) — leave it untouched.
        n_dir = sum(1 for h in hand_keep if not h.strip().startswith("#")
                    and not auto_func_re.match(h.strip()))
        n_stub = sum(1 for h in hand_keep if auto_func_re.match(h.strip()))
        if n_dir == 0 and n_stub == 0:
            continue

        section = [INGEST_BEGIN,
                   "# Source: snesrev/sm decomp PC comments.",
                   "# Regenerate via: python tools/ingest_sm_decomp.py",
                   f"# {len(auto_keep)} entries "
                   f"({sum(1 for h in hand_keep if auto_func_re.match(h.strip()))} "
                   f"relocated to hand block above)."]
        section += auto_keep
        section.append(INGEST_END)
        new_content = (head.rstrip() + "\n\n" + _HAND_HEADER + "\n"
                       + "\n".join(hand_keep) + "\n\n"
                       + "\n".join(section) + "\n"
                       + (tail if tail.strip() else tail.rstrip() + "\n"))

        if dry_run:
            print(f"[dry-run] migrate {cfg_path}: relocate {n_dir} directive(s)"
                  f" + {n_stub} stub func(s); {len(auto_keep)} auto funcs kept")
        else:
            cfg_path.write_text(new_content, encoding="utf-8")
            print(f"migrated {cfg_path}: relocated {n_dir} directive(s) + "
                  f"{n_stub} stub func(s); {len(auto_keep)} auto funcs kept")


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--decomp", default="refs/snesrev-sm",
                    help="path to snesrev/sm decomp checkout")
    ap.add_argument("--output", default="recomp",
                    help="path to recomp/ dir containing bank cfg files")
    ap.add_argument("--dry-run", action="store_true")
    ap.add_argument("--migrate", action="store_true",
                    help="relocate hand-authored lines out of AUTO-INGESTED "
                         "blocks (one-time cleanup so regen is safe); does "
                         "NOT touch the func list otherwise")
    args = ap.parse_args()

    decomp_root = Path(args.decomp)
    if not decomp_root.is_dir():
        print(f"--decomp not a directory: {decomp_root}", file=sys.stderr)
        return 1
    output_dir = Path(args.output)
    if not output_dir.is_dir():
        print(f"--output not a directory: {output_dir}", file=sys.stderr)
        return 1

    funcs, tables = harvest(decomp_root)
    print(f"harvested {len(funcs)} funcs, {len(tables)} tables "
          f"({sum(1 for t in tables if t['kind']=='func_ptr')} func-ptr, "
          f"{sum(1 for t in tables if t['kind']=='data')} data)",
          file=sys.stderr)

    by_bank: Dict[int, int] = defaultdict(int)
    for bank, _, _ in funcs:
        by_bank[bank] += 1
    bank_hist = ", ".join(f"${b:02X}:{n}" for b, n in sorted(by_bank.items()))
    print(f"per-bank funcs: {bank_hist}", file=sys.stderr)

    if args.migrate:
        migrate(funcs, output_dir, dry_run=args.dry_run)
        return 0

    emit_per_bank(funcs, output_dir, dry_run=args.dry_run)
    emit_tables_json(tables, output_dir, dry_run=args.dry_run)
    return 0


if __name__ == "__main__":
    sys.exit(main())
