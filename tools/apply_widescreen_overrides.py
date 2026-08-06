#!/usr/bin/env python3
"""Inject runtime-gated Super Metroid horizontal range extensions.

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
EPROJ_MARKER = "/*SM-WS-EPROJ-CULL*/"
PROJ_MARKER = "/*SM-WS-PROJ-CULL*/"
PROJ_FAR_MARKER = "/*SM-WS-PROJ-FAR-CULL*/"
ATMOS_MARKER = "/*SM-WS-ATMOS-CULL*/"
SCREEN_X_MARKER = "/*SM-WS-SCREEN-X-CULL*/"
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

EPROJ_SIMPLE_RULES = {
    "EprojPreInstr_NorfairLavaquakeRocks_Inner2": (1, 1),
    "EprojPreInstrHelper_SpikeShootingPlantSpikes_Func2": (1, 1),
    "EprojPreInstrHelper_DBF2_Func2": (1, 1),
    "sub_86DFA0": (1, 1),
    "sub_86E0B0": (1, 1),
    "sub_86EC18": (1, 1),
    "CheckIfEprojIsOffScreen": (1, 0),
}
PROJECTILE_RULES = {
    "ProjPreInstr_IceSba2": ("SmWidescreenProjectileSbaInView", (1, 0)),
    "ProjPreInstr_SpeedEcho": ("SmWidescreenProjectileCenterInView", (1, 0)),
    "ProjPreInstr_PlasmaSbaFunc_2": (
        "SmWidescreenProjectileSbaInView", (1, 0)),
}
PROJECTILE_FAR_RULES = {
    "DeleteProjectileIfFarOffScreen": "SmWidescreenProjectileFarInView",
}
ATMOSPHERIC_RULES = {
    "AtmosphericTypeFunc_1_FootstepSplash": "SmWidescreenAtmosphericXInView",
}
SCREEN_X_RULES = {
    "Samus_ArmCannon_Draw": (1, 0, 1, 0),
}
CUSTOM_OAM_WRITERS = {
    "bank_14_B0F9": (1, 3, True),
    "bank_80_896E": (0, 4, False),
    "bank_81_89AE": (0, 3, False),
    "bank_81_8A5F": (0, 3, False),
    "bank_81_8AB8": (0, 3, False),
    "bank_81_8B22": (0, 3, False),
    "bank_81_8B96": (0, 3, False),
    "bank_94_B0AA": (0, 3, False),
    "bank_94_B0F9": (1, 3, True),
    "bank_94_B14B": (0, 3, False),
}
HDMA_EFFECT_BOUNDS = {
    # (0xff00 masks, camera-X reads, camera-Y reads, emitted variants)
    #
    # The bank-88 forms come from the attract profile. The canonical bank-08
    # aliases are explicit widescreen roots and can have more live M/X
    # variants; audit both forms because they execute the same HDMA bounds
    # calculations through different LoROM mirrors.
    "bank_88_8896": (0, 2, 2, 1),   # CalculateXrayHdmaTable
    "bank_88_8C62": (1, 1, 1, 1),   # CalculatePowerBombHdmaObjectTablePtrs
    "bank_88_8F56": (1, 1, 1, 1),   # CalculatePowerBombHdmaTablePointers
    "bank_88_A42F": (1, 1, 1, 1),   # CalculateCrystalFlashHdmaObjectTablePtrs
    "bank_88_D9A1": (2, 2, 2, 1),   # HdmaobjPreInstr_RainBg3Scroll
    "bank_88_DA47": (2, 1, 1, 1),   # HdmaobjPreInstr_SporesBG3Xscroll
    "bank_88_DB36": (2, 1, 1, 1),   # HdmaobjPreInstr_FogBG3Scroll
    "bank_88_DF94": (0, 1, 2, 1),   # HdmaobjPreInstr_DF94
    "bank_88_E987": (0, 1, 1, 1),   # sub_88E987
    "CalculateXrayHdmaTable": (0, 2, 2, 4),
    "CalculatePowerBombHdmaObjectTablePtrs": (1, 1, 1, 2),
    "CalculatePowerBombHdmaTablePointers": (1, 1, 1, 2),
    "CalculateCrystalFlashHdmaObjectTablePtrs": (1, 1, 1, 2),
    "HdmaobjPreInstr_RainBg3Scroll": (2, 2, 2, 2),
    "HdmaobjPreInstr_SporesBG3Xscroll": (2, 1, 1, 2),
    "HdmaobjPreInstr_FogBG3Scroll": (2, 1, 1, 2),
    "HdmaobjPreInstr_DF94": (0, 1, 2, 2),
    "sub_88E987": (0, 1, 1, 4),
}
HDMA_POINTER_WRITERS = {
    "bank_88_8C62",
    "bank_88_8F56",
    "bank_88_A42F",
    "CalculatePowerBombHdmaObjectTablePtrs",
    "CalculatePowerBombHdmaTablePointers",
    "CalculateCrystalFlashHdmaObjectTablePtrs",
}
RESIDUAL_CAMERA_X_RISK = {
    # These functions still combine camera-X reads with native-looking masks,
    # OAM writes, or branch constants after the explicit widescreen patches.
    # Each entry has been audited as non-culling or covered by a more specific
    # verifier above. Keep this list exact so new risky generated shapes cannot
    # slip in without being reviewed.
    "AtmosphericTypeFunc_Common": ("bank10_v2.c", 2, 2, 0, 0, 2, 38),
    # Canonical low-bank twins of the audited bank_88_* HDMA calculators
    # below: same code bodies materialized at the cfg-canonical $08 addresses
    # by the widescreen root scan. Non-culling for the same reasons.
    "CalculateCrystalFlashHdmaObjectTablePtrs": ("bank08_v2.c", 2, 2, 0, 0, 8, 102),
    "CalculatePowerBombHdmaObjectTablePtrs": ("bank08_v2.c", 2, 2, 0, 0, 8, 102),
    "CalculatePowerBombHdmaTablePointers": ("bank08_v2.c", 2, 2, 0, 0, 8, 102),
    "CalculateXrayHdmaTable": ("bank08_v2.c", 4, 0, 0, 0, 28, 300),
    "DrawGrappleOams3": ("bank14_v2.c", 2, 0, 0, 6, 2, 78),
    "HdmaobjPreInstr_DF94": ("bank08_v2.c", 2, 0, 0, 0, 14, 60),
    "HdmaobjPreInstr_FogBG3Scroll": ("bank08_v2.c", 2, 4, 0, 0, 6, 51),
    "HdmaobjPreInstr_RainBg3Scroll": ("bank08_v2.c", 2, 4, 0, 0, 6, 87),
    "HdmaobjPreInstr_SporesBG3Xscroll": ("bank08_v2.c", 2, 4, 0, 0, 6, 55),
    "sub_88E987": ("bank08_v2.c", 4, 0, 0, 0, 8, 152),
    "HandleGrappleBeamFlare": ("bank1b_v2.c", 4, 4, 0, 0, 24, 204),
    "HandleGrappleBeamGfx": ("bank14_v2.c", 1, 1, 0, 0, 10, 95),
    "ProjectileTrail_Func5": ("bank1b_v2.c", 4, 16, 0, 0, 28, 436),
    "SamusBottomDrawn_0_Standing": ("bank10_v2.c", 2, 0, 0, 6, 2, 70),
    "Samus_CalcSpritemapPos_Crouch": ("bank10_v2.c", 2, 2, 0, 0, 6, 80),
    "Samus_CalcSpritemapPos_Default": ("bank10_v2.c", 2, 2, 0, 0, 2, 64),
    "Samus_CalcSpritemapPos_Special": ("bank10_v2.c", 1, 1, 0, 0, 6, 27),
    "Samus_CalcSpritemapPos_Standing": ("bank10_v2.c", 2, 0, 0, 0, 10, 124),
    "Samus_DrawEcho": ("bank10_v2.c", 1, 0, 0, 0, 3, 60),
    "WriteEnemyOams": ("bank20_v2.c", 4, 4, 8, 0, 60, 318),
    "bank_14_B0F9": ("bank14_v2.c", 1, 1, 0, 3, 2, 47),
    "bank_80_A4BB": ("bank80_v2.c", 1, 0, 0, 0, 4, 4),
    "bank_80_AE4E": ("bank80_v2.c", 1, 0, 0, 0, 1, 2),
    "bank_80_AE7E": ("bank80_v2.c", 1, 0, 0, 0, 1, 18),
    "bank_80_AEC2": ("bank80_v2.c", 1, 0, 0, 0, 1, 30),
    "bank_80_AF89": ("bank80_v2.c", 1, 0, 0, 0, 3, 68),
    "bank_88_8896": ("bank88_v2.c", 1, 0, 0, 0, 7, 75),
    "bank_88_8C62": ("bank88_v2.c", 1, 1, 0, 0, 4, 51),
    "bank_88_8F56": ("bank88_v2.c", 1, 1, 0, 0, 4, 51),
    "bank_88_A42F": ("bank88_v2.c", 1, 1, 0, 0, 4, 51),
    "bank_88_D9A1": ("bank88_v2.c", 1, 2, 0, 0, 3, 45),
    "bank_88_DA47": ("bank88_v2.c", 1, 2, 0, 0, 3, 29),
    "bank_88_DB36": ("bank88_v2.c", 1, 2, 0, 0, 3, 27),
    "bank_88_DF94": ("bank88_v2.c", 1, 0, 0, 0, 7, 30),
    "bank_88_E987": ("bank88_v2.c", 1, 0, 0, 0, 2, 38),
    "bank_94_AFBA": ("bank94_v2.c", 1, 1, 0, 0, 10, 95),
    "bank_94_B0F9": ("bank94_v2.c", 1, 1, 0, 3, 2, 47),
    "bank_94_B14B": ("bank94_v2.c", 1, 0, 0, 3, 1, 38),
    "bank_9B_A3CC": ("bank9b_v2.c", 1, 4, 0, 0, 7, 109),
    # All four provable variants of the $9B HandleGrappleBeamFlare mirror are
    # materialized via the widescreen root scan (was 2 profile-reached ones).
    "bank_9B_C036": ("bank9b_v2.c", 4, 4, 0, 0, 24, 204),
    "CalculateCrystalFlashHdmaObjectTablePtrs":
        ("bank08_v2.c", 2, 2, 0, 0, 8, 102),
    "CalculatePowerBombHdmaObjectTablePtrs":
        ("bank08_v2.c", 2, 2, 0, 0, 8, 102),
    "CalculatePowerBombHdmaTablePointers":
        ("bank08_v2.c", 2, 2, 0, 0, 8, 102),
    "CalculateXrayHdmaTable":
        ("bank08_v2.c", 4, 0, 0, 0, 28, 300),
    "HdmaobjPreInstr_DF94":
        ("bank08_v2.c", 2, 0, 0, 0, 14, 60),
    "HdmaobjPreInstr_FogBG3Scroll":
        ("bank08_v2.c", 2, 4, 0, 0, 6, 51),
    "HdmaobjPreInstr_RainBg3Scroll":
        ("bank08_v2.c", 2, 4, 0, 0, 6, 87),
    "HdmaobjPreInstr_SporesBG3Xscroll":
        ("bank08_v2.c", 2, 4, 0, 0, 6, 55),
    "sub_88E987":
        ("bank08_v2.c", 4, 0, 0, 0, 8, 152),
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


def verify_enemy_oam_margin_window(gen_dir: Path) -> int:
    """Check that enemy OAM emission still covers the 16:9 side margins.

    The MMX widescreen work showed that widened lifetime/culling is not enough:
    generated metasprite writers can still drop tiles at the native 4:3 edge.
    Super Metroid's WriteEnemyOams accepts tile screen-X after adding 0x80 and
    rejecting only when bits outside the low 9-bit signed window are set
    (mask 0xfe00). That preserves tiles roughly in [-128,383], which covers
    the 43px 16:9 budget on both sides. Fail if regen changes that contract.
    """
    path = gen_dir / "bank20_v2.c"
    if not path.exists():
      raise SystemExit(f"apply_widescreen_overrides: missing {path}")
    text = path.read_text(encoding="utf-8")
    variants = 0
    for match in FUNC_RE.finditer(text):
      if match.group("base") != "WriteEnemyOams":
        continue
      start, end = function_extent(text, match.start())
      body = text[start:end]
      masks = len(re.findall(r"uint16\s+_v\d+\s+=\s+0xfe00;", body))
      if masks != 2:
        raise SystemExit(
            "apply_widescreen_overrides: WriteEnemyOams OAM X window "
            f"changed in {match.group(0).split('(')[0].strip()}: "
            f"expected 2 mask checks, got {masks}"
        )
      variants += 1
    if variants != 4:
      raise SystemExit(
          "apply_widescreen_overrides: expected 4 WriteEnemyOams variants, "
          f"got {variants}"
      )
    return variants


def verify_grapple_oam_margin_window(gen_dir: Path) -> int:
    """Check that grapple's custom OAM writers do not reintroduce 4:3 culls."""
    path = gen_dir / "bank14_v2.c"
    if not path.exists():
      raise SystemExit(f"apply_widescreen_overrides: missing {path}")
    text = path.read_text(encoding="utf-8")
    counts = {"DrawGrappleOams": 0, "DrawGrappleOams3": 0}
    for match in FUNC_RE.finditer(text):
      base = match.group("base")
      if base not in counts:
        continue
      start, end = function_extent(text, match.start())
      body = text[start:end]
      masks = re.findall(
          r"uint(?:8|16)\s+_v\d+\s+=\s+0x(?:fe|ff)00;", body)
      if masks:
        raise SystemExit(
            "apply_widescreen_overrides: grapple OAM writer gained a "
            f"native X window mask in {match.group(0).split('(')[0].strip()}"
        )
      oam_writes = len(re.findall(r"cpu_write16\([^\n]+0x037[0-2]", body))
      if oam_writes != 3:
        raise SystemExit(
            "apply_widescreen_overrides: grapple OAM writer shape changed in "
            f"{match.group(0).split('(')[0].strip()}: expected 3 OAM writes, "
            f"got {oam_writes}"
        )
      counts[base] += 1
    if counts != {"DrawGrappleOams": 2, "DrawGrappleOams3": 2}:
      raise SystemExit(
          "apply_widescreen_overrides: expected 2 variants for each grapple "
          f"OAM writer, got {counts}"
      )
    return sum(counts.values())


def verify_custom_oam_margin_window(gen_dir: Path) -> int:
    """Check other custom OAM writers learned from the MMX audit.

    These are not normal enemy metasprites. Most contain no viewport mask at
    all; the two B0F9 variants have a single 0xff00 mask, but it happens before
    camera X is read and guards native vertical visibility only.
    """
    counts = {name: 0 for name in CUSTOM_OAM_WRITERS}
    for path in sorted(gen_dir.glob("*.c")):
        text = path.read_text(encoding="utf-8")
        for match in FUNC_RE.finditer(text):
            base = match.group("base")
            if base not in CUSTOM_OAM_WRITERS:
                continue
            start, end = function_extent(text, match.start())
            body = text[start:end]
            expected_masks, expected_writes, vertical_mask = (
                CUSTOM_OAM_WRITERS[base]
            )
            masks = re.findall(
                r"uint(?:8|16)\s+_v\d+\s+=\s+0x(?:fe|ff)00;", body)
            if len(masks) != expected_masks:
                raise SystemExit(
                    "apply_widescreen_overrides: custom OAM writer mask "
                    f"changed in {match.group(0).split('(')[0].strip()}: "
                    f"expected {expected_masks}, got {len(masks)}"
                )
            if expected_masks and "0xfe00" in body:
                raise SystemExit(
                    "apply_widescreen_overrides: custom OAM writer gained a "
                    f"native signed X mask in "
                    f"{match.group(0).split('(')[0].strip()}"
                )
            if vertical_mask:
                mask_pos = body.find("0xff00")
                cam_x_pos = body.find("0x0911")
                cam_y_pos = body.find("0x0915")
                if cam_y_pos < 0 or cam_x_pos < 0 or not (
                    cam_y_pos < mask_pos < cam_x_pos
                ):
                    raise SystemExit(
                        "apply_widescreen_overrides: custom OAM mask is no "
                        "longer the expected vertical check in "
                        f"{match.group(0).split('(')[0].strip()}"
                    )
            oam_writes = len(re.findall(
                r"cpu_write(?:8|16)\([^\n]+0x037[0-9a-f]", body,
                re.IGNORECASE,
            ))
            if oam_writes != expected_writes:
                raise SystemExit(
                    "apply_widescreen_overrides: custom OAM writer shape "
                    f"changed in {match.group(0).split('(')[0].strip()}: "
                    f"expected {expected_writes} OAM writes, got {oam_writes}"
                )
            counts[base] += 1
    expected_counts = {
        "bank_14_B0F9": 1,
        "bank_80_896E": 1,
        "bank_81_89AE": 2,
        "bank_81_8A5F": 1,
        "bank_81_8AB8": 1,
        "bank_81_8B22": 1,
        "bank_81_8B96": 1,
        "bank_94_B0AA": 1,
        "bank_94_B0F9": 1,
        "bank_94_B14B": 1,
    }
    if counts != expected_counts:
        raise SystemExit(
            "apply_widescreen_overrides: expected custom OAM writer variants "
            f"{expected_counts}, got {counts}"
        )
    return sum(counts.values())


def verify_projectile_trail_sign_extension(gen_dir: Path) -> int:
    """Check projectile trails are using signed offset extension, not culling."""
    path = gen_dir / "bank1b_v2.c"
    if not path.exists():
        raise SystemExit(f"apply_widescreen_overrides: missing {path}")
    text = path.read_text(encoding="utf-8")
    variants = 0
    for match in FUNC_RE.finditer(text):
        if match.group("base") != "ProjectileTrail_Func5":
            continue
        start, end = function_extent(text, match.start())
        body = text[start:end]
        targets = ("A455", "A46F", "A489", "A4A2")
        for target in targets:
            branch = re.search(
                rf"if \(cpu->_flag_N == 1\).*goto L_{target}_(M[01]X[01]);",
                body,
            )
            if not branch:
                raise SystemExit(
                    "apply_widescreen_overrides: projectile trail signed "
                    f"offset branch to L_{target} changed"
                )
            suffix = branch.group(1)
            label = re.search(
                rf"^  L_{target}_{suffix}:(?P<section>.*?)"
                rf"^  L_[0-9A-F]+_{suffix}:",
                body,
                re.MULTILINE | re.DOTALL,
            )
            if not label or "0xff00" not in label.group("section"):
                raise SystemExit(
                    "apply_widescreen_overrides: projectile trail signed "
                    f"offset extension at L_{target}_{suffix} changed"
                )
        if len(re.findall(r"uint16\s+_v\d+\s+=\s+0xff00;", body)) != 4:
            raise SystemExit(
                "apply_widescreen_overrides: projectile trail offset "
                f"extension count changed in "
                f"{match.group(0).split('(')[0].strip()}"
            )
        variants += 1
    if variants != 4:
        raise SystemExit(
            "apply_widescreen_overrides: expected 4 ProjectileTrail_Func5 "
            f"variants, got {variants}"
        )
    return variants


def verify_hdma_effect_bounds(gen_dir: Path) -> int:
    """Check audited HDMA effect bound calculators keep their known shape.

    These functions clip or generate HDMA tables for X-ray, power bomb,
    crystal flash, rain, spores, and fog. They intentionally still contain
    native vertical checks and table clipping math; this verifier prevents
    mistaking those paths for OAM culls and catches regenerated shape changes.
    """
    counts = {name: 0 for name in HDMA_EFFECT_BOUNDS}
    for path in sorted(gen_dir.glob("*.c")):
        text = path.read_text(encoding="utf-8")
        for match in FUNC_RE.finditer(text):
            base = match.group("base")
            if base not in HDMA_EFFECT_BOUNDS:
                continue
            start, end = function_extent(text, match.start())
            body = text[start:end]
            expected_ff_masks, expected_cam_x, expected_cam_y, _count = (
                HDMA_EFFECT_BOUNDS[base]
            )
            if "0xfe00" in body or "0x037" in body:
                raise SystemExit(
                    "apply_widescreen_overrides: HDMA effect path gained a "
                    f"sprite-style mask/OAM write in "
                    f"{match.group(0).split('(')[0].strip()}"
                )
            ff_masks = len(re.findall(
                r"uint16\s+_v\d+\s+=\s+0xff00;", body))
            if ff_masks != expected_ff_masks:
                raise SystemExit(
                    "apply_widescreen_overrides: HDMA effect mask count "
                    f"changed in {match.group(0).split('(')[0].strip()}: "
                    f"expected {expected_ff_masks}, got {ff_masks}"
                )
            cam_x = body.count("0x0911")
            cam_y = body.count("0x0915")
            if cam_x != expected_cam_x or cam_y != expected_cam_y:
                raise SystemExit(
                    "apply_widescreen_overrides: HDMA effect camera usage "
                    f"changed in {match.group(0).split('(')[0].strip()}: "
                    f"expected x/y {expected_cam_x}/{expected_cam_y}, got "
                    f"{cam_x}/{cam_y}"
                )
            if "0x18c0" not in body.lower() and base in HDMA_POINTER_WRITERS:
                raise SystemExit(
                    "apply_widescreen_overrides: HDMA table pointer writer "
                    f"changed in {match.group(0).split('(')[0].strip()}"
                )
            counts[base] += 1
    expected_counts = {
        name: spec[3] for name, spec in HDMA_EFFECT_BOUNDS.items()
    }
    if counts != expected_counts:
        raise SystemExit(
            "apply_widescreen_overrides: expected HDMA effect variants "
            f"{expected_counts}, got {counts}"
        )
    return sum(counts.values())


def verify_residual_camera_x_risk(gen_dir: Path) -> int:
    """Check the remaining camera-X/native-shape audit surface is reviewed."""
    found: dict[str, dict[str, object]] = {}
    for path in sorted(gen_dir.glob("*.c")):
        text = path.read_text(encoding="utf-8")
        for match in FUNC_RE.finditer(text):
            start, end = function_extent(text, match.start())
            body = text[start:end]
            if "0x0911" not in body or "SM-WS-" in body:
                continue
            ff_masks = body.count("0xff00")
            fe_masks = body.count("0xfe00")
            oam_writes = len(re.findall(
                r"cpu_write(?:8|16)\([^\n]+0x037[0-9a-f]",
                body,
                re.IGNORECASE,
            ))
            branches = len(re.findall(
                r"if \(cpu->_flag_[NZCV] == [01]\)", body))
            native_constants = len(re.findall(
                r"0x0?100|0x0?1ff|0x0?200|0x0?300|0x0?ff\b|0x00ff",
                body,
                re.IGNORECASE,
            ))
            if not (ff_masks or fe_masks or oam_writes or
                    (branches and native_constants)):
                continue
            rec = found.setdefault(match.group("base"), {
                "files": set(),
                "variants": 0,
                "ff": 0,
                "fe": 0,
                "oam": 0,
                "branches": 0,
                "native": 0,
            })
            rec["files"].add(path.name)  # type: ignore[union-attr]
            rec["variants"] += 1  # type: ignore[operator]
            rec["ff"] += ff_masks  # type: ignore[operator]
            rec["fe"] += fe_masks  # type: ignore[operator]
            rec["oam"] += oam_writes  # type: ignore[operator]
            rec["branches"] += branches  # type: ignore[operator]
            rec["native"] += native_constants  # type: ignore[operator]

    expected = {
        name: {
            "files": {spec[0]},
            "variants": spec[1],
            "ff": spec[2],
            "fe": spec[3],
            "oam": spec[4],
            "branches": spec[5],
            "native": spec[6],
        }
        for name, spec in RESIDUAL_CAMERA_X_RISK.items()
    }
    if found != expected:
        def normalize(rows: dict[str, dict[str, object]]) -> dict[str, tuple]:
            return {
                name: (
                    tuple(sorted(row["files"])), row["variants"], row["ff"],
                    row["fe"], row["oam"], row["branches"], row["native"],
                )
                for name, row in sorted(rows.items())
            }
        raise SystemExit(
            "apply_widescreen_overrides: residual camera-X audit surface "
            f"changed: expected {normalize(expected)}, "
            f"got {normalize(found)}"
        )
    return sum(int(row["variants"]) for row in found.values())


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


def patch_eproj_function(body: str, base: str) -> tuple[str, int]:
    flags = EPROJ_SIMPLE_RULES[base]
    marker_count = body.count(EPROJ_MARKER)
    if marker_count:
        if marker_count != len(flags):
            raise RuntimeError(f"{base}: partial eproj widescreen patch")
        return body, marker_count

    count = 0
    for flag in flags:
        branch = re.compile(
            rf"if \(cpu->_flag_N == {flag}\)(?P<tail> \{{ cpu->cycles \+= 1; "
            rf"cpu->master_cycles \+= (?:8|\(g_memsel \? 6 : 8\)); "
            rf"goto L_[0-9A-F]+_M[01]X[01]; \}})"
        )
        body, n = branch.subn(
            rf"if (cpu->_flag_N == {flag} && "
            rf"!SmWidescreenEprojCenterInView(cpu) {EPROJ_MARKER})\g<tail>",
            body,
            count=1,
        )
        if n != 1:
            raise RuntimeError(f"{base}: expected eproj branch not found")
        count += 1
    return body, count


def patch_projectile_function(body: str, base: str) -> tuple[str, int]:
    helper, flags = PROJECTILE_RULES[base]
    marker_count = body.count(PROJ_MARKER)
    if marker_count:
        if marker_count != len(flags):
            raise RuntimeError(f"{base}: partial projectile widescreen patch")
        return body, marker_count

    count = 0
    for flag in flags:
        branch = re.compile(
            rf"if \(cpu->_flag_N == {flag}\)(?P<tail> \{{ cpu->cycles \+= 1; "
            rf"cpu->master_cycles \+= (?:8|\(g_memsel \? 6 : 8\)); "
            rf"goto L_[0-9A-F]+_M[01]X[01]; \}})"
        )
        body, n = branch.subn(
            rf"if (cpu->_flag_N == {flag} && "
            rf"!{helper}(cpu) {PROJ_MARKER})\g<tail>",
            body,
            count=1,
        )
        if n != 1:
            raise RuntimeError(f"{base}: expected projectile branch not found")
        count += 1
    return body, count


def patch_projectile_far_function(body: str, base: str) -> tuple[str, int]:
    helper = PROJECTILE_FAR_RULES[base]
    marker_count = body.count(PROJ_FAR_MARKER)
    if marker_count:
        if marker_count != 2:
            raise RuntimeError(f"{base}: partial projectile far patch")
        return body, marker_count

    if "uint16 _v6 = 0xffc0;" not in body or "uint16 _v8 = 0x140;" not in body:
        return body, 0

    left_branch = re.compile(
        rf"if \(cpu->_flag_N == 1\)(?P<tail> \{{ cpu->cycles \+= 1; "
        rf"cpu->master_cycles \+= (?:8|\(g_memsel \? 6 : 8\)); "
        rf"goto L_B17E_M[01]X[01]; \}})"
    )
    body, n = left_branch.subn(
        rf"if (cpu->_flag_N == 1 && !{helper}(cpu) "
        rf"{PROJ_FAR_MARKER})\g<tail>",
        body,
        count=1,
    )
    if n != 1:
        raise RuntimeError(f"{base}: expected far-left projectile branch not found")

    right_branch = re.compile(
        rf"if \(cpu->_flag_N == 1\)(?P<tail> \{{ cpu->cycles \+= 1; "
        rf"cpu->master_cycles \+= (?:8|\(g_memsel \? 6 : 8\)); "
        rf"goto L_B184_M[01]X[01]; \}})"
    )
    body, n = right_branch.subn(
        rf"if (cpu->_flag_N == 1 || {helper}(cpu) "
        rf"{PROJ_FAR_MARKER})\g<tail>",
        body,
        count=1,
    )
    if n != 1:
        raise RuntimeError(f"{base}: expected far-right projectile branch not found")

    return body, 2


def patch_atmospheric_function(body: str, base: str) -> tuple[str, int]:
    helper = ATMOSPHERIC_RULES[base]
    marker_count = body.count(ATMOS_MARKER)
    if marker_count:
        if marker_count != 2:
            raise RuntimeError(f"{base}: partial atmospheric patch")
        return body, marker_count

    if "uint16 _v13 = 0x4;" not in body or "uint16 _v16 = 0x100;" not in body:
        return body, 0

    count = 0
    patches = (
        ("8AC5", "8ADF", 1),
        ("8ADF", "8AE4", 0),
    )
    for block, next_block, flag in patches:
        label = re.search(rf"^  L_{block}_M[01]X[01]:$", body, re.MULTILINE)
        if not label:
            raise RuntimeError(f"{base}: block L_{block} not found")
        next_label = re.search(rf"^  L_{next_block}_M[01]X[01]:$",
                               body[label.end():], re.MULTILINE)
        if not next_label:
            raise RuntimeError(f"{base}: block L_{next_block} not found")
        segment_end = label.end() + next_label.start()
        segment = body[label.start():segment_end]
        branch = re.compile(
            rf"if \(cpu->_flag_N == {flag}\)(?P<tail> \{{ cpu->cycles \+= 1; "
            rf"cpu->master_cycles \+= (?:8|\(g_memsel \? 6 : 8\)); "
            rf"goto L_8B13_M[01]X[01]; \}})"
        )
        replaced, n = branch.subn(
            rf"if (cpu->_flag_N == {flag} && "
            rf"!{helper}(cpu) {ATMOS_MARKER})\g<tail>",
            segment,
            count=1,
        )
        if n != 1:
            raise RuntimeError(f"{base}: expected atmospheric branch in L_{block} not found")
        body = body[:label.start()] + replaced + body[segment_end:]
        count += 1
    return body, count


def patch_screen_x_function(body: str, base: str) -> tuple[str, int]:
    flags = SCREEN_X_RULES[base]
    marker_count = body.count(SCREEN_X_MARKER)
    if marker_count:
        if marker_count != len(flags):
            raise RuntimeError(f"{base}: partial screen-x widescreen patch")
        return body, marker_count

    count = 0
    for flag in flags:
        branch = re.compile(
            rf"if \(cpu->_flag_N == {flag}\)(?P<tail> \{{ cpu->cycles \+= 1; "
            rf"cpu->master_cycles \+= (?:8|\(g_memsel \? 6 : 8\)); "
            rf"goto L_[0-9A-F]+_M[01]X[01]; \}})"
        )
        body, n = branch.subn(
            rf"if (cpu->_flag_N == {flag} && "
            rf"!SmWidescreenScreenXInView(cpu) {SCREEN_X_MARKER})\g<tail>",
            body,
            count=1,
        )
        if n != 1:
            raise RuntimeError(f"{base}: expected screen-x branch not found")
        count += 1
    return body, count


def restore(text: str) -> tuple[str, int]:
    enemy_pattern = re.compile(
        r" && !SmWidescreenEnemy(?:Center|Box)InView\(cpu\) "
        + re.escape(MARKER)
    )
    text, enemy_count = enemy_pattern.subn("", text)
    eproj_pattern = re.compile(
        r" && !SmWidescreenEprojCenterInView\(cpu\) "
        + re.escape(EPROJ_MARKER)
    )
    text, eproj_count = eproj_pattern.subn("", text)
    proj_pattern = re.compile(
        r" && !SmWidescreenProjectile(?:Center|Sba)InView\(cpu\) "
        + re.escape(PROJ_MARKER)
    )
    text, proj_count = proj_pattern.subn("", text)
    proj_far_left_pattern = re.compile(
        r" && !SmWidescreenProjectileFarInView\(cpu\) "
        + re.escape(PROJ_FAR_MARKER)
    )
    text, proj_far_left_count = proj_far_left_pattern.subn("", text)
    proj_far_right_pattern = re.compile(
        r" \|\| SmWidescreenProjectileFarInView\(cpu\) "
        + re.escape(PROJ_FAR_MARKER)
    )
    text, proj_far_right_count = proj_far_right_pattern.subn("", text)
    atmos_pattern = re.compile(
        r" && !SmWidescreenAtmosphericXInView\(cpu\) "
        + re.escape(ATMOS_MARKER)
    )
    text, atmos_count = atmos_pattern.subn("", text)
    screen_x_pattern = re.compile(
        r" && !SmWidescreenScreenXInView\(cpu\) "
        + re.escape(SCREEN_X_MARKER)
    )
    text, screen_x_count = screen_x_pattern.subn("", text)
    return (
        text,
        enemy_count + eproj_count + proj_count + proj_far_left_count +
        proj_far_right_count + atmos_count + screen_x_count,
    )


def process_file(path: Path, do_restore: bool) -> tuple[
    int, dict[str, int], dict[str, int], dict[str, int], dict[str, int],
    dict[str, int], dict[str, int],
]:
    text = path.read_text(encoding="utf-8")
    if do_restore:
        restored, n = restore(text)
        if n:
            path.write_text(restored, encoding="utf-8", newline="")
        return n, {}, {}, {}, {}, {}, {}

    original = text
    matches = [m for m in FUNC_RE.finditer(text)
               if (m.group("base") in LOGICAL_NAME
                   or m.group("base") in EPROJ_SIMPLE_RULES
                   or m.group("base") in PROJECTILE_RULES
                   or m.group("base") in PROJECTILE_FAR_RULES
                   or m.group("base") in ATMOSPHERIC_RULES
                   or m.group("base") in SCREEN_X_RULES)]
    counts = {name: 0 for name in RULES}
    eproj_counts = {name: 0 for name in EPROJ_SIMPLE_RULES}
    projectile_counts = {name: 0 for name in PROJECTILE_RULES}
    projectile_far_counts = {name: 0 for name in PROJECTILE_FAR_RULES}
    atmospheric_counts = {name: 0 for name in ATMOSPHERIC_RULES}
    screen_x_counts = {name: 0 for name in SCREEN_X_RULES}
    for match in reversed(matches):
        base = match.group("base")
        start, end = function_extent(text, match.start())
        if base in LOGICAL_NAME:
            logical_name = LOGICAL_NAME[base]
            body, n = patch_function(text[start:end], logical_name)
            counts[logical_name] += n
        elif base in EPROJ_SIMPLE_RULES:
            body, n = patch_eproj_function(text[start:end], base)
            eproj_counts[base] += n
        elif base in PROJECTILE_RULES:
            body, n = patch_projectile_function(text[start:end], base)
            projectile_counts[base] += n
        elif base in PROJECTILE_FAR_RULES:
            body, n = patch_projectile_far_function(text[start:end], base)
            projectile_far_counts[base] += n
        elif base in ATMOSPHERIC_RULES:
            body, n = patch_atmospheric_function(text[start:end], base)
            atmospheric_counts[base] += n
        else:
            body, n = patch_screen_x_function(text[start:end], base)
            screen_x_counts[base] += n
        text = text[:start] + body + text[end:]
    # Preserve bank object timestamps after the first injection. This matters
    # now that generated compilation is sharded per bank: an ordinary build
    # should not rebuild bank $A0 just because the verification target ran.
    if text != original:
        path.write_text(text, encoding="utf-8", newline="")
    return (
        sum(counts.values()) + sum(eproj_counts.values()) +
        sum(projectile_counts.values()) + sum(projectile_far_counts.values()) +
        sum(atmospheric_counts.values()) + sum(screen_x_counts.values()),
        counts, eproj_counts, projectile_counts, projectile_far_counts,
        atmospheric_counts, screen_x_counts,
    )


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--gen-dir", type=Path, default=Path("src/gen"))
    parser.add_argument("--restore", action="store_true")
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()

    if not args.gen_dir.is_dir():
        parser.error(f"generated source directory not found: {args.gen_dir}")

    totals = {name: 0 for name in RULES}
    eproj_totals = {name: 0 for name in EPROJ_SIMPLE_RULES}
    projectile_totals = {name: 0 for name in PROJECTILE_RULES}
    projectile_far_totals = {name: 0 for name in PROJECTILE_FAR_RULES}
    atmospheric_totals = {name: 0 for name in ATMOSPHERIC_RULES}
    screen_x_totals = {name: 0 for name in SCREEN_X_RULES}
    changed = 0
    for path in sorted(args.gen_dir.glob("*.c")):
        (
            n, counts, eproj_counts, projectile_counts,
            projectile_far_counts, atmospheric_counts, screen_x_counts,
        ) = process_file(path, args.restore)
        changed += n
        for name, count in counts.items():
            totals[name] += count
        for name, count in eproj_counts.items():
            eproj_totals[name] += count
        for name, count in projectile_counts.items():
            projectile_totals[name] += count
        for name, count in projectile_far_counts.items():
            projectile_far_totals[name] += count
        for name, count in atmospheric_counts.items():
            atmospheric_totals[name] += count
        for name, count in screen_x_counts.items():
            screen_x_totals[name] += count

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
    if any(count == 0 or count % len(EPROJ_SIMPLE_RULES[name]) != 0
           for name, count in eproj_totals.items()):
        raise SystemExit(
            "apply_widescreen_overrides: incomplete eproj coverage: "
            f"{eproj_totals}"
        )
    if any(count == 0 or count % len(PROJECTILE_RULES[name][1]) != 0
           for name, count in projectile_totals.items()):
        raise SystemExit(
            "apply_widescreen_overrides: incomplete projectile coverage: "
            f"{projectile_totals}"
        )
    if any(count == 0 or count % 2 != 0
           for name, count in projectile_far_totals.items()):
        raise SystemExit(
            "apply_widescreen_overrides: incomplete projectile far coverage: "
            f"{projectile_far_totals}"
        )
    if any(count == 0 or count % 2 != 0
           for name, count in atmospheric_totals.items()):
        raise SystemExit(
            "apply_widescreen_overrides: incomplete atmospheric coverage: "
            f"{atmospheric_totals}"
        )
    if any(count == 0 or count % len(SCREEN_X_RULES[name]) != 0
           for name, count in screen_x_totals.items()):
        raise SystemExit(
            "apply_widescreen_overrides: incomplete screen-x coverage: "
            f"{screen_x_totals}"
        )
    enemy_oam_variants = verify_enemy_oam_margin_window(args.gen_dir)
    grapple_oam_variants = verify_grapple_oam_margin_window(args.gen_dir)
    custom_oam_variants = verify_custom_oam_margin_window(args.gen_dir)
    projectile_trail_variants = verify_projectile_trail_sign_extension(
        args.gen_dir)
    hdma_effect_variants = verify_hdma_effect_bounds(args.gen_dir)
    residual_camera_x_variants = verify_residual_camera_x_risk(args.gen_dir)
    total_branches = (
        sum(totals.values()) + sum(eproj_totals.values()) +
        sum(projectile_totals.values()) + sum(projectile_far_totals.values()) +
        sum(atmospheric_totals.values()) + sum(screen_x_totals.values())
    )
    if args.check:
        print(
            "apply_widescreen_overrides: verified "
            f"{total_branches} branches, {enemy_oam_variants} enemy OAM "
            f"variants, {grapple_oam_variants} grapple OAM variants, "
            f"{custom_oam_variants} custom OAM variants, "
            f"{projectile_trail_variants} projectile trail variants, "
            f"{hdma_effect_variants} HDMA effect variants, and "
            f"{residual_camera_x_variants} residual camera-X variants"
        )
    else:
        print(
            "apply_widescreen_overrides: injected "
            f"{total_branches} branches and verified "
            f"{enemy_oam_variants} enemy OAM variants and "
            f"{grapple_oam_variants} grapple OAM variants, "
            f"{custom_oam_variants} custom OAM variants, "
            f"{projectile_trail_variants} projectile trail variants, "
            f"{hdma_effect_variants} HDMA effect variants, and "
            f"{residual_camera_x_variants} residual camera-X variants"
        )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
