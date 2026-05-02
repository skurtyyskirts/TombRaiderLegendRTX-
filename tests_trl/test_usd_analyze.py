import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from pathlib import Path

from patches.TombRaiderLegend import usd_analyze


def test_usd_analyze_extracts_and_diffs_hashes(tmp_path: Path) -> None:
    capture_a = tmp_path / "capture_2026-01-01_00-00-00.usd"
    capture_b = tmp_path / "capture_2026-01-01_00-00-01.usd"
    capture_a.write_bytes(
        b'def Xform "mesh_AAAAAAAAAAAAAAAA" {}\n'
        b'def Material "mat_BBBBBBBBBBBBBBBB" {}\n'
        b'def Shader "tex_CCCCCCCCCCCCCCCC" {}\n'
        b'def Skeleton "skel_DDDDDDDDDDDDDDDD" {}\n'
    )
    capture_b.write_bytes(
        b'def Xform "mesh_AAAAAAAAAAAAAAAA" {}\n'
        b'def Xform "mesh_EEEEEEEEEEEEEEEE" {}\n'
        b'def Material "mat_BBBBBBBBBBBBBBBB" {}\n'
    )

    summary = usd_analyze.summarize_capture(capture_a)
    diff = usd_analyze.diff_captures(capture_a, capture_b)

    assert summary.mesh_hashes == ["AAAAAAAAAAAAAAAA"]
    assert summary.material_hashes == ["BBBBBBBBBBBBBBBB"]
    assert summary.texture_hashes == ["CCCCCCCCCCCCCCCC"]
    assert summary.skeleton_hashes == ["DDDDDDDDDDDDDDDD"]
    assert diff["stable"] == ["AAAAAAAAAAAAAAAA"]
    assert diff["added"] == ["EEEEEEEEEEEEEEEE"]
    assert diff["removed"] == []


def test_usd_analyze_reports_stability_across_capture_dir(tmp_path: Path) -> None:
    first = tmp_path / "capture_1.usd"
    second = tmp_path / "capture_2.usd"
    third = tmp_path / "capture_3.usd"
    first.write_bytes(b'mesh_AAAAAAAAAAAAAAAA mesh_BBBBBBBBBBBBBBBB')
    second.write_bytes(b'mesh_AAAAAAAAAAAAAAAA mesh_CCCCCCCCCCCCCCCC')
    third.write_bytes(b'mesh_AAAAAAAAAAAAAAAA mesh_BBBBBBBBBBBBBBBB')

    report = usd_analyze.analyze_capture_stability(tmp_path)

    assert report["capture_count"] == 3
    assert report["stable_meshes"] == ["AAAAAAAAAAAAAAAA"]
    assert "BBBBBBBBBBBBBBBB" in report["transient_meshes"]
