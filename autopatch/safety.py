"""Backup and rollback for proxy source and game DLLs."""
from __future__ import annotations

import shutil
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
PROXY_DIR = REPO_ROOT / "proxy"
PROXY_SRC = PROXY_DIR / "d3d9_device.c"
PROXY_MAIN = PROXY_DIR / "d3d9_main.c"
PROXY_WRAPPER = PROXY_DIR / "d3d9_wrapper.c"
PROXY_INI = PROXY_DIR / "proxy.ini"
GAME_DIR = REPO_ROOT / "Tomb Raider Legend"

# All proxy source files that may be modified during promote_to_source
_PROXY_FILES = [PROXY_SRC, PROXY_MAIN, PROXY_WRAPPER, PROXY_INI]


def backup_proxy(iteration_id: str) -> Path:
    """Back up all proxy source files before editing. Returns backup directory."""
    bak_dir = PROXY_DIR / f"backups" / f"autopatch_{iteration_id}"
    bak_dir.mkdir(parents=True, exist_ok=True)
    for src in _PROXY_FILES:
        if src.exists():
            shutil.copy2(src, bak_dir / src.name)
    return bak_dir


def restore_proxy(iteration_id: str) -> bool:
    """Restore proxy source files from a previous backup."""
    bak_dir = PROXY_DIR / "backups" / f"autopatch_{iteration_id}"
    if not bak_dir.exists():
        return False
    for src in _PROXY_FILES:
        bak_file = bak_dir / src.name
        if bak_file.exists():
            shutil.copy2(bak_file, src)
    return True


def backup_game_dll() -> Path:
    """Back up the proxy DLL and INI from the game directory before tracer swap."""
    src_dll = GAME_DIR / "d3d9.dll"
    src_ini = GAME_DIR / "proxy.ini"
    dst_dll = GAME_DIR / "d3d9.dll.proxy_backup"
    dst_ini = GAME_DIR / "proxy.ini.proxy_backup"
    if src_dll.exists():
        shutil.copy2(src_dll, dst_dll)
    if src_ini.exists():
        shutil.copy2(src_ini, dst_ini)
    return dst_dll


def restore_game_dll() -> bool:
    """Restore the proxy DLL and INI after tracer capture."""
    bak_dll = GAME_DIR / "d3d9.dll.proxy_backup"
    bak_ini = GAME_DIR / "proxy.ini.proxy_backup"
    dst_dll = GAME_DIR / "d3d9.dll"
    dst_ini = GAME_DIR / "proxy.ini"
    restored = False
    if bak_dll.exists():
        # Sanity check: the proxy DLL is much smaller than the tracer (~24KB vs ~160KB).
        # If the backup is tracer-sized, refuse to restore — it's not the real proxy.
        bak_size = bak_dll.stat().st_size
        if bak_size > 100_000:
            print(f"[safety] WARNING: backup d3d9.dll is {bak_size} bytes "
                  f"(expected <100KB for proxy). Refusing to restore — "
                  f"backup may be the tracer, not the proxy.")
            return False
        shutil.copy2(bak_dll, dst_dll)
        bak_dll.unlink()
        restored = True
    else:
        print("[safety] WARNING: d3d9.dll.proxy_backup not found, cannot restore")
    if bak_ini.exists():
        shutil.copy2(bak_ini, dst_ini)
        bak_ini.unlink()
        restored = True
    return restored
