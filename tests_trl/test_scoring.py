import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from pathlib import Path

from PIL import Image, ImageDraw

from patches.TombRaiderLegend.nightly.manifests import load_scene_manifest
from patches.TombRaiderLegend.nightly.executor import NightlyExecutor
from patches.TombRaiderLegend.nightly.model import CandidateSpec, NightlyConfig, Rect, SceneDefinition
from patches.TombRaiderLegend.nightly.scoring import evaluate_hash_stability, evaluate_sky_frames, evaluate_water_motion


def _scene_map():
    return {scene.scene_id: scene for scene in load_scene_manifest()}


def test_sky_roi_scoring_uses_trl_fixture_images(repo_root: Path) -> None:
    scenes = _scene_map()
    scene = scenes["bolivia_sky_vista"]
    clean = repo_root / "TRL tests" / "build-071-hash-stability-FAIL-lights-missing" / "phase2-clean-center.png"
    contaminated = repo_root / "TRL tests" / "build-071-hash-stability-FAIL-lights-missing" / "phase1-hash-center.png"

    clean_result = evaluate_sky_frames([clean, clean, clean], scene.rois["sky"], scene.thresholds)
    contaminated_result = evaluate_sky_frames([contaminated, contaminated, contaminated], scene.rois["sky"], scene.thresholds)

    assert clean_result[0] is True
    assert clean_result[1] >= scene.thresholds["sky_non_void_min_pct"]
    assert clean_result[2] <= scene.thresholds["sky_contamination_max_pct"]
    assert contaminated_result[0] is False
    assert contaminated_result[2] > scene.thresholds["sky_contamination_max_pct"]


def test_hash_stability_scoring_detects_pixel_drift(tmp_path: Path) -> None:
    scene = _scene_map()["bolivia_stage_baseline"]
    roi = scene.rois["hash_stability"]
    paths = []
    for index, offset in enumerate((0, 0, 12), start=1):
        image = Image.new("RGB", (320, 240), (20, 20, 20))
        draw = ImageDraw.Draw(image)
        draw.rectangle((60 + offset, 60, 220 + offset, 180), fill=(180, 180, 180))
        path = tmp_path / f"hash_{index}.png"
        image.save(path)
        paths.append(path)

    retention = evaluate_hash_stability(paths, roi)
    assert retention < scene.thresholds["hash_retention_min_pct"]


def test_water_motion_scoring_detects_moving_water(tmp_path: Path) -> None:
    scene = _scene_map()["bolivia_waterfall"]
    paths = []
    for index in range(5):
        image = Image.new("RGB", (400, 300), (70, 70, 70))
        draw = ImageDraw.Draw(image)
        draw.rectangle((40, 80, 160, 250), fill=(82, 82, 82))
        draw.rectangle((250, 80, 372, 250), fill=(70, 95, 120))
        for stripe in range(0, 150, 18):
            x1 = 250 + ((stripe + index * 7) % 120)
            draw.rectangle((x1, 80, min(x1 + 10, 372), 250), fill=(170, 210, 235))
        path = tmp_path / f"water_{index}.png"
        image.save(path)
        paths.append(path)

    result = evaluate_water_motion(paths, scene.rois["water"], scene.rois["background"], scene.thresholds)
    assert result.ratio >= scene.thresholds["water_motion_ratio_min"]


def test_water_motion_scoring_caps_static_background_ratio(tmp_path: Path) -> None:
    scene = _scene_map()["bolivia_waterfall"]
    paths = []
    for index in range(5):
        image = Image.new("RGB", (400, 300), (70, 70, 70))
        draw = ImageDraw.Draw(image)
        draw.rectangle((40, 80, 160, 250), fill=(82, 82, 82))
        draw.rectangle((250, 80, 372, 250), fill=(70, 95, 120))
        for stripe in range(0, 150, 18):
            x1 = 250 + ((stripe + index * 9) % 120)
            draw.rectangle((x1, 80, min(x1 + 10, 372), 250), fill=(170, 210, 235))
        path = tmp_path / f"water_cap_{index}.png"
        image.save(path)
        paths.append(path)

    result = evaluate_water_motion(
        paths,
        scene.rois["water"],
        scene.rois["background"],
        {
            "water_motion_abs_min": 1.0,
            "water_background_floor": 0.5,
            "water_motion_ratio_cap": 8.0,
        },
    )

    assert result.ratio <= 8.0


def test_score_candidate_ignores_scenes_without_hash_roi(tmp_path: Path) -> None:
    config = NightlyConfig(
        default_hours=1,
        candidate_limit=1,
        keep_top_candidates=1,
        max_source_mutation_rounds=0,
        max_source_candidates_per_round=0,
        required_patch_tokens=["PatchA"],
        review_rules={},
        runtime={},
        publication={},
        automation={},
        mutation_classes={},
    )
    scenes = [
        SceneDefinition(
            scene_id="stage",
            label="Stage",
            checkpoint_file="peru_checkpoint.dat",
            bootstrap_goals=[],
            rois={"hash_stability": Rect(0.2, 0.2, 0.8, 0.8)},
            thresholds={"hash_retention_min_pct": 98.0},
        ),
        SceneDefinition(
            scene_id="sky-only",
            label="Sky",
            checkpoint_file="peru_checkpoint.dat",
            bootstrap_goals=[],
            rois={"sky": Rect(0.1, 0.1, 0.9, 0.4)},
            thresholds={"sky_non_void_min_pct": 0.0, "sky_contamination_max_pct": 100.0},
        ),
    ]
    executor = NightlyExecutor(config, scenes, dry_run=True)
    spec = CandidateSpec(candidate_id="candidate", mutation_class="config_only", description="test")

    stage_hash_paths = []
    for index in range(3):
        image = Image.new("RGB", (320, 240), (20, 20, 20))
        draw = ImageDraw.Draw(image)
        draw.rectangle((60, 60, 220, 180), fill=(180, 180, 180))
        path = tmp_path / f"stage_hash_{index}.png"
        image.save(path)
        stage_hash_paths.append(str(path))

    sky_clean_path = tmp_path / "sky_clean.png"
    Image.new("RGB", (320, 240), (80, 80, 80)).save(sky_clean_path)

    result = executor._score_candidate(
        spec,
        {
            "crashed": False,
            "log_path": None,
            "scenes": {
                "stage": {"hash": stage_hash_paths, "clean": []},
                "sky-only": {"hash": [], "clean": [str(sky_clean_path)]},
            },
        },
        tmp_path / "candidate",
    )

    assert result.hash_retention_pct == 100.0
