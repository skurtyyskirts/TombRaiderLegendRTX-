"""Build artifact validator — enforces md5sum + stat verification.

Metrics are computed from direct file I/O (hashlib + Path.stat).
All reported values must come from the returned ArtifactMetrics, not
from free-form text generation.

Usage:
    python automation/build_validator.py [path/to/d3d9.dll]

Runs against the build-041 regression baseline by default.
"""
from __future__ import annotations

import hashlib
import sys
from pathlib import Path
from typing import NamedTuple


class ArtifactMetrics(NamedTuple):
    path: str
    md5: str
    size_bytes: int

    def __str__(self) -> str:
        return f"{self.path}: md5={self.md5}  size={self.size_bytes:,} bytes"


# Build-041 confirmed baseline.
# md5 stores a partial prefix — run the validator on the build-041 DLL and
# replace with the full 32-hex hash printed by compute_md5().
BUILD_041_BASELINE = ArtifactMetrics(
    path="d3d9.dll",
    md5="9016bcdd",  # partial prefix — replace with full hash after confirmation
    size_bytes=1_183_232,
)


def compute_md5(file_path: str | Path) -> str:
    """Compute MD5 by reading the file directly — no subprocess, no hallucination."""
    h = hashlib.md5()
    with open(file_path, "rb") as f:
        for chunk in iter(lambda: f.read(65536), b""):
            h.update(chunk)
    return h.hexdigest()


def get_file_metrics(file_path: str | Path) -> ArtifactMetrics:
    """Return ArtifactMetrics from actual file I/O."""
    p = Path(file_path)
    if not p.is_file():
        raise FileNotFoundError(f"Artifact not found: {file_path}")
    return ArtifactMetrics(path=str(p), md5=compute_md5(p), size_bytes=p.stat().st_size)


def assert_metrics_match(actual: ArtifactMetrics, expected: ArtifactMetrics) -> None:
    """Raise AssertionError if size or MD5 do not match.

    Uses prefix comparison when the baseline stores < 32 hex chars;
    uses full comparison when a complete 32-char MD5 is stored.
    """
    if actual.size_bytes != expected.size_bytes:
        raise AssertionError(
            f"Size mismatch: expected {expected.size_bytes:,} bytes, "
            f"got {actual.size_bytes:,} bytes\n"
            f"Actual:   {actual}\n"
            f"Expected: {expected}"
        )
    actual_md5 = actual.md5.strip().lower()
    expected_md5 = expected.md5.strip().lower()
    if not expected_md5:
        raise AssertionError("Baseline MD5 is empty — fill in BUILD_041_BASELINE with the full hash.")
    if actual_md5[: len(expected_md5)] != expected_md5:
        raise AssertionError(
            f"MD5 mismatch: expected {expected_md5!r}, "
            f"got prefix {actual_md5[:len(expected_md5)]!r}\n"
            f"Actual:   {actual}\n"
            f"Expected: {expected}"
        )


def validate_build_artifact(
    dll_path: str | Path,
    baseline: ArtifactMetrics | None = None,
    *,
    verbose: bool = True,
) -> ArtifactMetrics:
    """Validate a build artifact. Callers MUST report the returned values verbatim."""
    metrics = get_file_metrics(dll_path)
    if verbose:
        print(f"[validator] {metrics}")

    if baseline is not None:
        assert_metrics_match(metrics, baseline)
        if verbose:
            print(f"[validator] PASS — matches baseline {baseline.md5[:8]}... {baseline.size_bytes:,} bytes")

    return metrics


def main() -> int:
    dll_path = sys.argv[1] if len(sys.argv) > 1 else "d3d9.dll"

    try:
        metrics = validate_build_artifact(dll_path, baseline=BUILD_041_BASELINE, verbose=True)
        print(f"\nValidation complete. Report these verbatim values:")
        print(f"  md5  = {metrics.md5}")
        print(f"  size = {metrics.size_bytes:,} bytes")
        return 0
    except FileNotFoundError as e:
        print(f"[validator] ERROR: {e}", file=sys.stderr)
        return 2
    except AssertionError as e:
        print(f"[validator] FAIL: {e}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
