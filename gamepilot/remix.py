"""RTX Remix runtime manager — swap between regular and debug runtimes."""
from __future__ import annotations

import shutil
from pathlib import Path

from config import GAME_DIR, REMIX_DEBUG_RUNTIME as DEBUG_RUNTIME
RUNTIME_MARKER = GAME_DIR / ".runtime_type"

# Files that live in the game root (not .trex) and differ between runtimes
ROOT_RUNTIME_FILES = [
    "d3d9.dll", "d3d9.pdb",
    "NvRemixLauncher32.exe", "NvRemixLauncher32.pdb",
    "d3d8_off.dll", "dxwrapper.dll", "dxwrapper.ini",
]


def get_active_runtime() -> str:
    """Return 'regular', 'debug', or 'unknown'."""
    if RUNTIME_MARKER.exists():
        return RUNTIME_MARKER.read_text().strip()
    return "regular"  # assume regular if no marker


def _backup_current(label: str) -> Path:
    """Back up the current runtime files to a named subfolder."""
    backup_dir = GAME_DIR / f".runtime_backup_{label}"
    if backup_dir.exists():
        shutil.rmtree(backup_dir)
    backup_dir.mkdir()

    # Back up .trex
    trex_src = GAME_DIR / ".trex"
    if trex_src.exists():
        shutil.copytree(trex_src, backup_dir / ".trex", dirs_exist_ok=True)

    # Back up root runtime files
    for fname in ROOT_RUNTIME_FILES:
        src = GAME_DIR / fname
        if src.exists():
            shutil.copy2(src, backup_dir / fname)

    print(f"[remix] Backed up current runtime to {backup_dir.name}/")
    return backup_dir


def _restore_from(source_dir: Path) -> None:
    """Restore runtime files from a source directory."""
    # Restore .trex
    trex_src = source_dir / ".trex"
    trex_dst = GAME_DIR / ".trex"
    if trex_src.exists():
        if trex_dst.exists():
            shutil.rmtree(trex_dst)
        shutil.copytree(trex_src, trex_dst)

    # Restore root files
    for fname in ROOT_RUNTIME_FILES:
        src = source_dir / fname
        if src.exists():
            shutil.copy2(src, GAME_DIR / fname)


def swap_to_debug() -> bool:
    """Switch the game to the debug Remix runtime.

    Backs up the regular runtime, copies debug runtime files in.
    Returns True on success.
    """
    if not DEBUG_RUNTIME.exists():
        print(f"[remix] ERROR: Debug runtime not found at {DEBUG_RUNTIME}")
        return False

    debug_trex = DEBUG_RUNTIME / ".trex"
    if not debug_trex.exists():
        print(f"[remix] ERROR: No .trex in debug runtime at {DEBUG_RUNTIME}")
        return False

    current = get_active_runtime()
    if current == "debug":
        print("[remix] Already on debug runtime")
        return True

    # Back up regular
    _backup_current("regular")

    # Copy debug runtime
    _restore_from(DEBUG_RUNTIME)

    RUNTIME_MARKER.write_text("debug")
    print("[remix] Switched to DEBUG runtime")
    return True


def swap_to_regular() -> bool:
    """Switch back to the regular Remix runtime.

    Restores from the backup created when switching to debug.
    Returns True on success.
    """
    current = get_active_runtime()
    if current == "regular":
        print("[remix] Already on regular runtime")
        return True

    backup_dir = GAME_DIR / ".runtime_backup_regular"
    if not backup_dir.exists():
        print("[remix] ERROR: No regular runtime backup found")
        return False

    # Back up debug state first
    _backup_current("debug")

    # Restore regular
    _restore_from(backup_dir)

    RUNTIME_MARKER.write_text("regular")
    print("[remix] Switched to REGULAR runtime")
    return True
