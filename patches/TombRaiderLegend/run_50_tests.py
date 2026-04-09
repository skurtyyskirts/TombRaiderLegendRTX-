"""Run 50 stable-hash tests, commit + push each one to GitHub.

Usage:
  python patches/TombRaiderLegend/run_50_tests.py
  python patches/TombRaiderLegend/run_50_tests.py --start 5   (resume from build #5)
  python patches/TombRaiderLegend/run_50_tests.py --total 50   (default: 50)
"""

import argparse
import os
import subprocess
import sys
import time
from pathlib import Path

SCRIPT_DIR = Path(__file__).resolve().parent
REPO_ROOT = SCRIPT_DIR.parent.parent
SCREENSHOTS_DIR = SCRIPT_DIR / "screenshots"
sys.path.insert(0, str(REPO_ROOT))
from config import NVIDIA_SCREENSHOT_DIR as NVIDIA_DIR

os.chdir(str(REPO_ROOT))


def count_existing_builds():
    """Count how many 'test: stable hash build #' commits exist."""
    r = subprocess.run(
        ["git", "log", "--oneline", "--all", "--grep=stable hash build #"],
        capture_output=True, text=True
    )
    return len([l for l in r.stdout.strip().splitlines() if l])


def clear_screenshots():
    """Remove old screenshots so each build starts fresh."""
    if SCREENSHOTS_DIR.exists():
        for f in SCREENSHOTS_DIR.iterdir():
            if f.suffix.lower() in (".png", ".jpg", ".bmp"):
                f.unlink()


def run_test():
    """Run the two-phase test. Returns True on success."""
    r = subprocess.run(
        [sys.executable, str(SCRIPT_DIR / "run.py"), "test"],
        cwd=str(REPO_ROOT),
        timeout=600  # 10 min max per test
    )
    return r.returncode == 0


def rename_screenshots(build_num):
    """Rename screenshots with build number prefix and phase label.

    Screenshots are sorted by modification time. The first group (from
    Phase 1) are hash-debug views, the second group (from Phase 2) are
    clean renders. Each file keeps its original timestamp suffix.
    """
    if not SCREENSHOTS_DIR.exists():
        return

    files = sorted(
        [f for f in SCREENSHOTS_DIR.iterdir()
         if f.suffix.lower() in (".png", ".jpg", ".bmp")],
        key=lambda f: f.stat().st_mtime
    )
    if not files:
        return

    # Split into two phases by time gap (Phase 2 starts after game relaunch)
    # Find the largest time gap between consecutive screenshots
    if len(files) > 1:
        gaps = [(files[i+1].stat().st_mtime - files[i].stat().st_mtime, i+1)
                for i in range(len(files) - 1)]
        biggest_gap_idx = max(gaps, key=lambda x: x[0])[1]
    else:
        biggest_gap_idx = len(files)

    hash_files = files[:biggest_gap_idx]
    clean_files = files[biggest_gap_idx:]

    prefix = f"build-{build_num:02d}"

    for i, f in enumerate(hash_files, 1):
        # Keep original timestamp portion after "Screenshot"
        orig = f.name
        ts_part = orig.split("Screenshot")[-1] if "Screenshot" in orig else ""
        new_name = f"{prefix}-hash-view-{i}_Screenshot{ts_part}"
        f.rename(f.parent / new_name)
        print(f"  {orig} -> {new_name}")

    for i, f in enumerate(clean_files, 1):
        orig = f.name
        ts_part = orig.split("Screenshot")[-1] if "Screenshot" in orig else ""
        new_name = f"{prefix}-clean-view-{i}_Screenshot{ts_part}"
        f.rename(f.parent / new_name)
        print(f"  {orig} -> {new_name}")

    print(f"  Renamed {len(hash_files)} hash + {len(clean_files)} clean screenshots")


def commit_and_push(build_num, total):
    """Stage screenshots + logs, commit, push."""
    # Stage screenshots
    subprocess.run(["git", "add", str(SCREENSHOTS_DIR)], cwd=str(REPO_ROOT))

    # Stage proxy log if present
    log_file = SCRIPT_DIR / "ffp_proxy.log"
    if log_file.exists():
        subprocess.run(["git", "add", str(log_file)], cwd=str(REPO_ROOT))

    # Check if there's anything to commit
    r = subprocess.run(
        ["git", "diff", "--cached", "--quiet"],
        cwd=str(REPO_ROOT)
    )
    if r.returncode == 0:
        print(f"  No changes to commit for build #{build_num}")
        return False

    msg = (
        f"test: stable hash build #{build_num}/{total}\n\n"
        f"Automated two-phase test run (hash debug + clean render).\n"
        f"Asset hash rule: indices,texcoords,geometrydescriptor\n\n"
        f"Co-Authored-By: Claude Opus 4.6 (1M context) <noreply@anthropic.com>"
    )
    subprocess.run(
        ["git", "commit", "-m", msg],
        cwd=str(REPO_ROOT)
    )

    # Push to skurtyyskirts repo (master -> main)
    r = subprocess.run(
        ["git", "push", "origin", "master:main"],
        cwd=str(REPO_ROOT),
        capture_output=True, text=True
    )
    if r.returncode != 0:
        print(f"  Push failed: {r.stderr}")
        # Try force push (user has approved this)
        subprocess.run(
            ["git", "push", "--force", "origin", "master:main"],
            cwd=str(REPO_ROOT)
        )

    return True


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--start", type=int, default=1,
                        help="Starting build number (default: 1)")
    parser.add_argument("--total", type=int, default=50,
                        help="Total builds to reach (default: 50)")
    args = parser.parse_args()

    existing = count_existing_builds()
    start = max(args.start, existing + 1)
    total = args.total

    if start > total:
        print(f"Already have {existing} builds (target: {total}). Done!")
        return

    print(f"=== Stable Hash Test Automation ===")
    print(f"Existing builds: {existing}")
    print(f"Will run builds #{start} through #{total}")
    print(f"Total remaining: {total - start + 1}")
    print()

    successes = 0
    failures = 0

    for build_num in range(start, total + 1):
        print(f"\n{'='*60}")
        print(f"  BUILD #{build_num}/{total}  "
              f"(successes={successes}, failures={failures})")
        print(f"{'='*60}\n")

        clear_screenshots()

        try:
            ok = run_test()
        except subprocess.TimeoutExpired:
            print(f"  TIMEOUT on build #{build_num}")
            # Kill game and continue
            subprocess.run(["taskkill", "/f", "/im", "trl.exe"],
                           capture_output=True)
            failures += 1
            time.sleep(5)
            continue
        except Exception as e:
            print(f"  ERROR on build #{build_num}: {e}")
            failures += 1
            time.sleep(5)
            continue

        if ok:
            successes += 1
            rename_screenshots(build_num)
            committed = commit_and_push(build_num, total)
            if committed:
                print(f"  Build #{build_num} committed and pushed!")
            else:
                print(f"  Build #{build_num} test passed but no new screenshots")
        else:
            failures += 1
            print(f"  Build #{build_num} FAILED (game crashed or macro error)")
            # Still rename and commit whatever screenshots we got
            rename_screenshots(build_num)
            commit_and_push(build_num, total)

        # Brief pause between runs
        time.sleep(3)

    print(f"\n{'='*60}")
    print(f"  COMPLETE: {successes} successes, {failures} failures")
    print(f"{'='*60}")


if __name__ == "__main__":
    main()
