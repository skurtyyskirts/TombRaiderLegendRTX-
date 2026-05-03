"""Build archive helper — captures all required diagnostic files.

Captures the DLL, logs, rtx.conf, and user.conf alongside optional files
(console.log, screenshots, extra logs). user.conf is required because Remix
UI overrides stored there silently affect rendering without appearing in rtx.conf.

Required files (warning if missing, but archive continues):
  - d3d9.dll, build.log, rtx.conf, user.conf

Optional files (copied if present, no warning if absent):
  - console.log, remix-dxvk.log, bridge.log, screenshots/

Usage:
    from automation.archive_utils import archive_build
    archive_build(build_number=78, game_dir="...", archive_root="TRL tests")
"""
from __future__ import annotations

import shutil
import datetime
from pathlib import Path
from typing import Optional


# Files that must be present in every archive.
REQUIRED_FILES = [
    "d3d9.dll",
    "build.log",
    "rtx.conf",
    "user.conf",  # Remix UI override file — REQUIRED per issue #146
]

OPTIONAL_FILES = [
    "console.log",
    "remix-dxvk.log",
    "bridge.log",
]


def archive_build(
    build_number: int,
    game_dir: str | Path,
    archive_root: str | Path,
    *,
    extra_files: Optional[list[str]] = None,
    dry_run: bool = False,
) -> Path:
    """
    Create a numbered build archive under archive_root.

    Wipes any existing archive for the same build number to prevent stale
    artifacts from contaminating re-runs.

    Returns the Path to the created archive directory.
    """
    game_dir = Path(game_dir)
    archive_root = Path(archive_root)
    build_tag = f"build-{build_number:03d}"
    archive_dir = archive_root / build_tag

    if dry_run:
        print(f"[archive] DRY RUN — would create {archive_dir}")
    else:
        if archive_dir.exists():
            shutil.rmtree(archive_dir)
        archive_dir.mkdir(parents=True)

    missing_required: list[str] = []

    # Copy required files — warn if any are absent.
    for fname in REQUIRED_FILES:
        src = game_dir / fname
        if src.is_file():
            dst = archive_dir / fname
            if not dry_run:
                shutil.copy2(src, dst)
            verb = "would copy" if dry_run else "copied"
            print(f"[archive] {verb} {fname}")
        else:
            missing_required.append(fname)
            print(f"[archive] WARNING: required file missing: {fname}")
            if fname == "user.conf":
                print(
                    "[archive] NOTE: user.conf records Remix UI overrides "
                    "(texture categories, developer settings). Without it, "
                    "rendering differences between builds cannot be fully audited."
                )

    # Copy extra files supplied by the caller — silently skip if absent.
    for fname in (extra_files or []):
        src = game_dir / fname
        if src.is_file():
            dst = archive_dir / fname
            if not dry_run:
                shutil.copy2(src, dst)
            verb = "would copy" if dry_run else "copied"
            print(f"[archive] {verb} extra: {fname}")

    # Copy optional files — silently skip if absent.
    for fname in OPTIONAL_FILES:
        src = game_dir / fname
        if src.is_file():
            dst = archive_dir / fname
            if not dry_run:
                shutil.copy2(src, dst)
            verb = "would copy" if dry_run else "copied"
            print(f"[archive] {verb} optional: {fname}")

    # Copy screenshots directory if present.
    ss_src = game_dir / "screenshots"
    if ss_src.is_dir():
        ss_dst = archive_dir / "screenshots"
        if not dry_run:
            shutil.copytree(ss_src, ss_dst)
        verb = "would copy" if dry_run else "copied"
        print(f"[archive] {verb} screenshots/")

    # Write archive manifest.
    manifest_path = archive_dir / "MANIFEST.txt"
    timestamp = datetime.datetime.utcnow().isoformat()
    manifest_lines = [
        f"build: {build_tag}",
        f"archived: {timestamp} UTC",
        f"game_dir: {game_dir}",
        "",
        "files:",
    ]
    if not dry_run:
        for item in sorted(archive_dir.rglob("*")):
            if item.is_file() and item != manifest_path:
                manifest_lines.append(f"  {item.relative_to(archive_dir)}")
        manifest_path.write_text("\n".join(manifest_lines) + "\n")

    if missing_required:
        print(f"[archive] WARNING: {len(missing_required)} required file(s) missing: {missing_required}")
    else:
        print(f"[archive] SUCCESS — {build_tag} archived to {archive_dir}")

    return archive_dir
