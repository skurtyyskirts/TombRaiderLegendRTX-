import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from patches.TombRaiderLegend.nightly import anchors as anchors_mod


def test_anchor_refresh_refuses_sparse_capture_and_keeps_tracked_hashes(tmp_path, monkeypatch) -> None:
    manifest = {
        "capture_requirements": {"min_mesh_hash_count": 4},
        "anchor_groups": [
            {
                "id": "bolivia_stage_cluster",
                "selection": {"mode": "explicit_only", "minimum_matches": 1},
                "mesh_hashes": ["AAAAAAAAAAAAAAAA"],
            }
        ],
        "lights": [],
    }
    saved_payloads = []
    monkeypatch.setattr(anchors_mod, "load_anchor_manifest", lambda: manifest)
    monkeypatch.setattr(anchors_mod, "save_anchor_manifest", lambda payload: saved_payloads.append(payload))

    capture = tmp_path / "capture_2026-04-14.usd"
    capture.write_bytes(b'def Xform "mesh_BBBBBBBBBBBBBBBB" {}\n')

    refreshed = anchors_mod.refresh_anchor_hashes(capture, persist=True)

    assert refreshed["anchor_validation"]["status"] == "blocked"
    assert refreshed["anchor_groups"][0]["mesh_hashes"] == ["AAAAAAAAAAAAAAAA"]
    assert any(
        "capture referenced only 1 mesh hashes" in reason
        for reason in refreshed["anchor_validation"]["reasons"]
    )
    assert any(
        "matched 0/1 explicit anchor hashes" in reason
        for reason in refreshed["anchor_validation"]["reasons"]
    )
    assert saved_payloads == []
