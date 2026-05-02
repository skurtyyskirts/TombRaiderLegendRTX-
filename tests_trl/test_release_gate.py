import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

import json
from pathlib import Path

from PIL import Image

from patches.TombRaiderLegend.run import (
    count_capture_markers,
    evaluate_release_gate,
    generate_random_movement_legacy,
    release_gate_frame_ready,
)
from patches.TombRaiderLegend.nightly.manifests import load_nightly_config


def _write_passing_log(tmp_path: Path) -> Path:
    config = load_nightly_config()
    log_path = tmp_path / "ffp_proxy.log"
    log_lines = list(config.required_patch_tokens)
    log_lines.extend(["p=0", "q=0", "FrameCpuMs=6.0", "FrameCpuMs=6.2"])
    log_path.write_text("\n".join(log_lines) + "\n", encoding="utf-8")
    return log_path


def test_release_gate_passes_known_good_fixture(repo_root: Path, tmp_path: Path) -> None:
    build_dir = repo_root / "TRL tests" / "build-019-miracle-both-lights-stable-hashes"
    report = evaluate_release_gate(
        [
            build_dir / "hash-debug-1-start.png",
            build_dir / "hash-debug-2-mid-strafe.png",
            build_dir / "hash-debug-3-end-strafe.png",
        ],
        [
            build_dir / "clean-render-1-start.png",
            build_dir / "clean-render-2-mid-strafe.png",
            build_dir / "clean-render-3-end-strafe.png",
        ],
        _write_passing_log(tmp_path),
        crashed=False,
    )

    assert report["hash_stability"]["passed"] is True
    assert report["lights"]["passed"] is True
    assert report["movement"]["passed"] is True
    assert report["log"]["passed"] is True
    assert report["passed"] is True


def test_release_gate_fails_known_unstable_hash_fixture(repo_root: Path, tmp_path: Path) -> None:
    good_build = repo_root / "TRL tests" / "build-019-miracle-both-lights-stable-hashes"
    unstable_build = repo_root / "TRL tests" / "build-017-fixed-culling-nops" / "screenshots"
    report = evaluate_release_gate(
        [
            unstable_build / "01-hash-baseline.png",
            unstable_build / "02-hash-after-A.png",
            unstable_build / "03-hash-after-D.png",
        ],
        [
            good_build / "clean-render-1-start.png",
            good_build / "clean-render-2-mid-strafe.png",
            good_build / "clean-render-3-end-strafe.png",
        ],
        _write_passing_log(tmp_path),
        crashed=False,
    )

    assert report["hash_stability"]["passed"] is False
    assert report["passed"] is False


def test_release_gate_fails_when_clean_screenshots_lose_stage_lights(repo_root: Path, tmp_path: Path) -> None:
    good_build = repo_root / "TRL tests" / "build-019-miracle-both-lights-stable-hashes"
    lights_fail = repo_root / "TRL tests" / "build-038-fallback-light-diagnostic-both-lights-gone"
    report = evaluate_release_gate(
        [
            good_build / "hash-debug-1-start.png",
            good_build / "hash-debug-2-mid-strafe.png",
            good_build / "hash-debug-3-end-strafe.png",
        ],
        [
            lights_fail / "clean-render-1-both-lights-near-stage.png",
            lights_fail / "clean-render-2-neutral-no-lights.png",
            lights_fail / "clean-render-3-neutral-no-lights.png",
        ],
        _write_passing_log(tmp_path),
        crashed=False,
    )

    assert report["lights"]["passed"] is False
    assert report["passed"] is False


def test_release_gate_fails_when_clean_frames_do_not_move(repo_root: Path, tmp_path: Path) -> None:
    build_dir = repo_root / "TRL tests" / "build-019-miracle-both-lights-stable-hashes"
    clean_frame = build_dir / "clean-render-1-start.png"
    report = evaluate_release_gate(
        [
            build_dir / "hash-debug-1-start.png",
            build_dir / "hash-debug-2-mid-strafe.png",
            build_dir / "hash-debug-3-end-strafe.png",
        ],
        [clean_frame, clean_frame, clean_frame],
        _write_passing_log(tmp_path),
        crashed=False,
    )

    assert report["movement"]["passed"] is False
    assert report["passed"] is False


def test_release_gate_fails_when_hash_evidence_is_missing(repo_root: Path, tmp_path: Path) -> None:
    build_dir = repo_root / "TRL tests" / "build-019-miracle-both-lights-stable-hashes"
    report = evaluate_release_gate(
        [],
        [
            build_dir / "clean-render-1-start.png",
            build_dir / "clean-render-2-mid-strafe.png",
            build_dir / "clean-render-3-end-strafe.png",
        ],
        _write_passing_log(tmp_path),
        crashed=False,
    )

    assert report["hash_stability"]["passed"] is False
    assert report["passed"] is False


def test_release_gate_randomized_sequence_emits_three_capture_points() -> None:
    assert count_capture_markers(generate_random_movement_legacy()) == 3


def test_release_gate_default_macro_defines_three_capture_points(repo_root: Path) -> None:
    macro_path = repo_root / "patches" / "TombRaiderLegend" / "macros.json"
    macro_data = json.loads(macro_path.read_text(encoding="utf-8"))
    assert count_capture_markers(macro_data["test_session"]["steps"]) == 3


def test_release_gate_frame_ready_accepts_known_good_fixture(repo_root: Path) -> None:
    fixture = repo_root / "TRL tests" / "build-019-miracle-both-lights-stable-hashes" / "clean-render-1-start.png"
    assert release_gate_frame_ready(fixture) is True


def test_release_gate_frame_ready_rejects_black_transition_frame(tmp_path: Path) -> None:
    image = Image.new("RGB", (256, 256), (0, 0, 0))
    image.putpixel((10, 10), (255, 255, 255))
    path = tmp_path / "black.png"
    image.save(path)
    assert release_gate_frame_ready(path) is False
