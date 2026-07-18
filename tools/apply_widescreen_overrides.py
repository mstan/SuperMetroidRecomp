#!/usr/bin/env python3
"""Inject runtime-gated Super Metroid horizontal enemy-range extensions.

Generated banks are deliberately untracked and regenerated from the ROM. This
tool applies small, idempotent edits at stable ROM basic-block labels, following
SuperMarioWorldRecomp's opt-in override model without hand-editing generated C.
With g_ws_active false, every original branch is evaluated unchanged.
"""

from __future__ import annotations

import argparse
import re
from pathlib import Path


MARKER = "/*SM-WS-CULL*/"
RULES = {
    "DetermineWhichEnemiesToProcess": (
        ("8F00", "8F54", "SmWidescreenEnemyBoxInView"),
        ("8F0C", "8F54", "SmWidescreenEnemyBoxInView"),
    ),
    "CheckIfEnemyIsOnScreen": (
        ("AD70", "AD9F", "SmWidescreenEnemyCenterInView"),
        ("AD7B", "AD9F", "SmWidescreenEnemyCenterInView"),
    ),
    "EnemyWithNormalSpritesIsOffScreen": (
        ("ADE7", "AE24", "SmWidescreenEnemyBoxInView"),
        ("ADF7", "AE24", "SmWidescreenEnemyBoxInView"),
    ),
}

# Manifest-driven emission may retain the cfg name or use the canonical LoROM
# mirror name, and it emits only processor-mode variants proven reachable.
# Patch every emitted form instead of assuming four synthetic M/X variants.
EMITTED_NAMES = {
    "DetermineWhichEnemiesToProcess": {
        "DetermineWhichEnemiesToProcess", "bank_20_8EB6", "bank_A0_8EB6",
    },
    "CheckIfEnemyIsOnScreen": {
        "CheckIfEnemyIsOnScreen", "bank_20_AD70", "bank_A0_AD70",
    },
    "EnemyWithNormalSpritesIsOffScreen": {
        "EnemyWithNormalSpritesIsOffScreen", "bank_20_ADE7", "bank_A0_ADE7",
    },
}
LOGICAL_NAME = {
    emitted: logical
    for logical, emitted_names in EMITTED_NAMES.items()
    for emitted in emitted_names
}

FUNC_RE = re.compile(
    r"^RecompReturn\s+(?P<base>[A-Za-z0-9_]+)_M[01]X[01]"
    r"\(CpuState \*cpu\) \{",
    re.MULTILINE,
)


def function_extent(text: str, start: int) -> tuple[int, int]:
    brace = text.find("{", start)
    if brace < 0:
        raise RuntimeError("function opening brace not found")
    depth = 0
    for pos in range(brace, len(text)):
        if text[pos] == "{":
            depth += 1
        elif text[pos] == "}":
            depth -= 1
            if depth == 0:
                return start, pos + 1
    raise RuntimeError("unterminated generated function")


def patch_function(body: str, base: str) -> tuple[str, int]:
    count = 0
    for block, target, helper in RULES[base]:
        label = re.search(rf"^  L_{block}_M[01]X[01]:$", body, re.MULTILINE)
        if not label:
            raise RuntimeError(f"{base}: block L_{block} not found")
        next_label = re.search(r"^  L_[0-9A-F]+_M[01]X[01]:$",
                               body[label.end():], re.MULTILINE)
        segment_end = (label.end() + next_label.start()) if next_label else len(body)
        segment = body[label.start():segment_end]
        if MARKER in segment:
            count += 1
            continue
        branch = re.compile(
            rf"if \(cpu->_flag_N == 1\)(?P<tail> \{{ cpu->cycles \+= 1; "
            rf"cpu->master_cycles \+= (?:8|\(g_memsel \? 6 : 8\)); "
            rf"goto L_{target}_M[01]X[01]; \}})"
        )
        replaced, n = branch.subn(
            rf"if (cpu->_flag_N == 1 && !{helper}(cpu) {MARKER})\g<tail>",
            segment,
            count=1,
        )
        if n != 1:
            raise RuntimeError(f"{base}: expected branch in L_{block} not found")
        body = body[:label.start()] + replaced + body[segment_end:]
        count += 1
    return body, count


def restore(text: str) -> tuple[str, int]:
    pattern = re.compile(
        r" && !SmWidescreenEnemy(?:Center|Box)InView\(cpu\) "
        + re.escape(MARKER)
    )
    return pattern.subn("", text)


def process_file(path: Path, do_restore: bool) -> tuple[int, dict[str, int]]:
    text = path.read_text(encoding="utf-8")
    if do_restore:
        restored, n = restore(text)
        if n:
            path.write_text(restored, encoding="utf-8", newline="")
        return n, {}

    original = text
    matches = [m for m in FUNC_RE.finditer(text)
               if m.group("base") in LOGICAL_NAME]
    counts = {name: 0 for name in RULES}
    for match in reversed(matches):
        logical_name = LOGICAL_NAME[match.group("base")]
        start, end = function_extent(text, match.start())
        body, n = patch_function(text[start:end], logical_name)
        text = text[:start] + body + text[end:]
        counts[logical_name] += n
    # Preserve bank object timestamps after the first injection. This matters
    # now that generated compilation is sharded per bank: an ordinary build
    # should not rebuild bank $A0 just because the verification target ran.
    if text != original:
        path.write_text(text, encoding="utf-8", newline="")
    return sum(counts.values()), counts


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--gen-dir", type=Path, default=Path("src/gen"))
    parser.add_argument("--restore", action="store_true")
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()

    if not args.gen_dir.is_dir():
        parser.error(f"generated source directory not found: {args.gen_dir}")

    totals = {name: 0 for name in RULES}
    changed = 0
    for path in sorted(args.gen_dir.glob("*.c")):
        n, counts = process_file(path, args.restore)
        changed += n
        for name, count in counts.items():
            totals[name] += count

    if args.restore:
        print(f"apply_widescreen_overrides: restored {changed} branch(es)")
        return 0

    # Each emitted function form has two horizontal branches. Exact processor
    # mode analysis can legitimately prune impossible variants, but every
    # target family must remain present and every emitted form must be patched.
    if any(count == 0 or count % len(RULES[name]) != 0
           for name, count in totals.items()):
        raise SystemExit(
            f"apply_widescreen_overrides: incomplete coverage: {totals}"
        )
    if args.check:
        print(f"apply_widescreen_overrides: verified {sum(totals.values())} branches")
    else:
        print(f"apply_widescreen_overrides: injected {sum(totals.values())} branches")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
