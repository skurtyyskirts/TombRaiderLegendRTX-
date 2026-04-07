"""Screenshot evaluation — detect red and green stage lights via pixel heuristics."""
from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path

import numpy as np
from PIL import Image

# Calibration screenshot paths (relative to repo root)
REPO_ROOT = Path(__file__).resolve().parent.parent
PASS_SCREENSHOT = (
    REPO_ROOT / "TombRaiderLegendRTX-" / "TRL tests"
    / "build-019-miracle-both-lights-stable-hashes" / "clean-render-1-start.png"
)
FAIL_SCREENSHOT = (
    REPO_ROOT / "TombRaiderLegendRTX-" / "TRL tests"
    / "build-038-fallback-light-diagnostic-both-lights-gone" / "clean-render-2-neutral-no-lights.png"
)

# Detection grid: divide image into cells and scan for color dominance
GRID_SIZE = 16
# Minimum brightness for a cell to be considered (0-255)
MIN_BRIGHTNESS = 25
# Minimum color dominance score (how much one channel exceeds others)
MIN_DOMINANCE = 15


@dataclass
class LightDetection:
    red_found: bool
    green_found: bool
    red_score: float  # max dominance score across all cells
    green_score: float
    red_cells: int  # number of cells with red dominance
    green_cells: int


@dataclass
class Verdict:
    passed: bool
    red_visible: list[bool]
    green_visible: list[bool]
    confidence: float
    crashed: bool = False
    details: list[LightDetection] | None = None


def detect_lights(image_path: str | Path) -> LightDetection:
    """Scan a screenshot for red and green light regions.

    Divides the image into a grid and checks each cell for color dominance.
    The Peru stage lights are distinctly red and green against a dark scene,
    making simple channel analysis reliable.
    """
    img = Image.open(image_path).convert("RGB")
    arr = np.array(img, dtype=np.float32)
    h, w = arr.shape[:2]
    cell_h, cell_w = h // GRID_SIZE, w // GRID_SIZE

    best_red = 0.0
    best_green = 0.0
    red_cells = 0
    green_cells = 0

    for row in range(GRID_SIZE):
        for col in range(GRID_SIZE):
            cell = arr[
                row * cell_h : (row + 1) * cell_h,
                col * cell_w : (col + 1) * cell_w,
            ]
            mean_r = cell[:, :, 0].mean()
            mean_g = cell[:, :, 1].mean()
            mean_b = cell[:, :, 2].mean()
            brightness = (mean_r + mean_g + mean_b) / 3.0

            if brightness < MIN_BRIGHTNESS:
                continue

            red_dom = mean_r - max(mean_g, mean_b)
            green_dom = mean_g - max(mean_r, mean_b)

            if red_dom > MIN_DOMINANCE:
                red_cells += 1
                best_red = max(best_red, red_dom)

            if green_dom > MIN_DOMINANCE:
                green_cells += 1
                best_green = max(best_green, green_dom)

    return LightDetection(
        red_found=red_cells >= 2,
        green_found=green_cells >= 2,
        red_score=best_red,
        green_score=best_green,
        red_cells=red_cells,
        green_cells=green_cells,
    )


def evaluate_screenshots(screenshot_paths: list[str | Path]) -> Verdict:
    """Evaluate a set of screenshots for light visibility.

    Args:
        screenshot_paths: List of 3 screenshot paths (near, mid, far positions).

    Returns:
        Verdict with pass/fail and per-screenshot details.
    """
    if not screenshot_paths:
        return Verdict(
            passed=False, red_visible=[], green_visible=[],
            confidence=0.0, crashed=True,
        )

    detections = [detect_lights(p) for p in screenshot_paths]
    red_visible = [d.red_found for d in detections]
    green_visible = [d.green_found for d in detections]

    all_red = all(red_visible)
    all_green = all(green_visible)
    passed = all_red and all_green

    # Confidence based on detection strength
    if not detections:
        confidence = 0.0
    else:
        avg_red = sum(d.red_score for d in detections) / len(detections)
        avg_green = sum(d.green_score for d in detections) / len(detections)
        # Normalize: 50 dominance = 1.0 confidence
        confidence = min(1.0, (avg_red + avg_green) / 100.0)

    return Verdict(
        passed=passed,
        red_visible=red_visible,
        green_visible=green_visible,
        confidence=confidence,
        details=detections,
    )


def calibrate() -> bool:
    """Validate detection against known-good and known-bad screenshots.

    Returns True if both calibrations pass, False otherwise.
    """
    if not PASS_SCREENSHOT.exists():
        print(f"[calibration] Missing pass screenshot: {PASS_SCREENSHOT}")
        return False
    if not FAIL_SCREENSHOT.exists():
        print(f"[calibration] Missing fail screenshot: {FAIL_SCREENSHOT}")
        return False

    good = detect_lights(PASS_SCREENSHOT)
    bad = detect_lights(FAIL_SCREENSHOT)

    print(f"[calibration] PASS image: red={good.red_found} ({good.red_score:.1f}, "
          f"{good.red_cells} cells), green={good.green_found} ({good.green_score:.1f}, "
          f"{good.green_cells} cells)")
    print(f"[calibration] FAIL image: red={bad.red_found} ({bad.red_score:.1f}, "
          f"{bad.red_cells} cells), green={bad.green_found} ({bad.green_score:.1f}, "
          f"{bad.green_cells} cells)")

    pass_ok = good.red_found and good.green_found
    fail_ok = not (bad.red_found and bad.green_found)

    if pass_ok and fail_ok:
        print("[calibration] OK — thresholds validated")
        return True

    if not pass_ok:
        print("[calibration] FAIL — could not detect lights in known-good screenshot")
    if not fail_ok:
        print("[calibration] FAIL — false positive on known-bad screenshot")
    return False
