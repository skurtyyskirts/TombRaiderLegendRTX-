import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from pathlib import Path


def test_animated_tex0_detects_vs_driven_uv_scroll(repo_root: Path):
    """TRL's water UV animation lives entirely in the vertex shader — it never
    issues SetTransform and TEXTURETRANSFORMFLAGS stage 0 is always DISABLE.
    The animation signal is VS constant c6 (y/z/w components). Without this
    detection path, water draws fall to the null-VS route and lose motion.

    See patches/TombRaiderLegend/findings.md section 'Water Diagnostic 2026-04-16'."""
    source_path = repo_root / "patches" / "TombRaiderLegend" / "proxy" / "d3d9_device.c"
    source = source_path.read_text(encoding="utf-8")

    expected_marker = "animatedTex0: detect VS-driven UV scroll via c6.y/z/w"
    assert expected_marker in source, (
        "H3 fix marker missing — TRL_DrawHasAnimatedTexture0 must recognise "
        "TRL's VS-driven water UV scroll (c6 non-zero)."
    )
