import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from pathlib import Path


def test_view_proj_lock_absorbs_camera_jitter(repo_root: Path):
    """TRL writes View/Proj to game memory every frame with small sub-pixel
    jitter even when the player is not moving. Without filtering, the proxy
    re-issues SetTransform with drifting values and Remix re-hashes geometry
    every frame (visible as puzzle-piece seams + earthquake texture snap).

    The proxy must snapshot View/Proj and keep the snapshot across frames
    when the delta is below H4_VP_LOCK_THRESHOLD. See
    patches/TombRaiderLegend/findings.md section 'Water Diagnostic 2026-04-16'.
    """
    source_path = repo_root / "patches" / "TombRaiderLegend" / "proxy" / "d3d9_device.c"
    source = source_path.read_text(encoding="utf-8")

    marker = "H4 fix: lock View/Proj across frames to absorb sub-pixel camera jitter"
    assert marker in source, (
        "H4 fix marker missing — TRL_ApplyTransformOverrides should lock "
        "View/Proj values when the live delta is below H4_VP_LOCK_THRESHOLD."
    )
    assert "H4_VP_LOCK_THRESHOLD" in source, "H4_VP_LOCK_THRESHOLD define missing"
    assert "vpLockValid" in source, "vpLockValid device-state flag missing"
