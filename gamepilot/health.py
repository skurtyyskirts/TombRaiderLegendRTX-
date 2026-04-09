"""Preflight health checks — verify prerequisites before running the agent."""
from __future__ import annotations

import os
import shutil
import subprocess
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent

# Minimum versions / requirements
REQUIRED_PYTHON = (3, 9)
IS_WINDOWS = sys.platform == "win32"


class CheckResult:
    """Result of a single health check."""

    __slots__ = ("name", "ok", "message", "fatal")

    def __init__(self, name: str, ok: bool, message: str, fatal: bool = True):
        self.name = name
        self.ok = ok
        self.message = message
        self.fatal = fatal

    def __repr__(self) -> str:
        status = "PASS" if self.ok else ("FAIL" if self.fatal else "WARN")
        return f"[{status}] {self.name}: {self.message}"


def check_python_version() -> CheckResult:
    v = sys.version_info[:2]
    ok = v >= REQUIRED_PYTHON
    msg = f"{v[0]}.{v[1]}" + ("" if ok else f" (need {REQUIRED_PYTHON[0]}.{REQUIRED_PYTHON[1]}+)")
    return CheckResult("Python version", ok, msg)


def check_platform() -> CheckResult:
    if IS_WINDOWS:
        return CheckResult("Platform", True, "Windows")
    return CheckResult("Platform", False, f"{sys.platform} — gamepilot requires Windows for game control", fatal=True)


def check_pillow() -> CheckResult:
    try:
        from PIL import Image
        return CheckResult("Pillow", True, f"v{Image.__version__}")
    except ImportError:
        return CheckResult("Pillow", False, "not installed (pip install Pillow)")


def check_numpy() -> CheckResult:
    try:
        import numpy as np
        return CheckResult("numpy", True, f"v{np.__version__}")
    except ImportError:
        return CheckResult("numpy", False, "not installed (pip install numpy)")


def check_claude_cli() -> CheckResult:
    """Check that the Claude CLI is available and responds."""
    path = shutil.which("claude")
    if not path:
        return CheckResult("Claude CLI", False, "not found in PATH")

    try:
        r = subprocess.run(
            ["claude", "--version"],
            capture_output=True, text=True, timeout=10,
        )
        version = r.stdout.strip() or r.stderr.strip()
        if r.returncode == 0:
            return CheckResult("Claude CLI", True, version.split("\n")[0])
        return CheckResult("Claude CLI", False, f"exit {r.returncode}: {version[:100]}")
    except subprocess.TimeoutExpired:
        return CheckResult("Claude CLI", False, "timed out (10s)")
    except Exception as e:
        return CheckResult("Claude CLI", False, str(e))


def check_game_dir() -> CheckResult:
    """Check that the game directory exists and contains trl.exe."""
    sys.path.insert(0, str(REPO_ROOT))
    try:
        from config import GAME_DIR, GAME_EXE, LAUNCHER
    except ImportError:
        return CheckResult("Game directory", False, "config.py not found")

    if not GAME_DIR.exists():
        return CheckResult("Game directory", False, f"not found: {GAME_DIR}")
    if not GAME_EXE.exists():
        return CheckResult("Game executable", False, f"trl.exe not found in {GAME_DIR}")
    if not LAUNCHER.exists():
        return CheckResult("Game launcher", False, f"NvRemixLauncher32.exe not found", fatal=False)
    return CheckResult("Game directory", True, str(GAME_DIR))


def check_proxy_dll() -> CheckResult:
    """Check that the proxy DLL is deployed to the game directory."""
    sys.path.insert(0, str(REPO_ROOT))
    try:
        from config import GAME_DIR
    except ImportError:
        return CheckResult("Proxy DLL", False, "config.py not found")

    dll = GAME_DIR / "d3d9.dll"
    if dll.exists():
        size_kb = dll.stat().st_size / 1024
        return CheckResult("Proxy DLL", True, f"d3d9.dll ({size_kb:.0f} KB)")
    return CheckResult("Proxy DLL", False, "d3d9.dll not in game directory", fatal=False)


def check_nvidia_screenshot_dir() -> CheckResult:
    """Check that the NVIDIA screenshot directory is configured."""
    sys.path.insert(0, str(REPO_ROOT))
    try:
        from config import NVIDIA_SCREENSHOT_DIR
    except ImportError:
        return CheckResult("NVIDIA screenshots", False, "config.py not found", fatal=False)

    if NVIDIA_SCREENSHOT_DIR.exists():
        return CheckResult("NVIDIA screenshots", True, str(NVIDIA_SCREENSHOT_DIR))
    return CheckResult("NVIDIA screenshots", False, f"not found: {NVIDIA_SCREENSHOT_DIR}", fatal=False)


def check_livetools() -> CheckResult:
    """Check that livetools is importable."""
    if not IS_WINDOWS:
        return CheckResult("livetools", False, "requires Windows (ctypes.windll)", fatal=False)
    try:
        from livetools.gamectl import find_hwnd_by_exe
        return CheckResult("livetools", True, "gamectl importable")
    except (ImportError, AttributeError) as e:
        return CheckResult("livetools", False, f"import failed: {e}")


def run_all_checks(verbose: bool = True) -> tuple[list[CheckResult], bool]:
    """Run all health checks.

    Returns:
        (results, all_fatal_passed) — list of results and whether all fatal checks passed.
    """
    checks = [
        check_python_version,
        check_platform,
        check_pillow,
        check_numpy,
        check_claude_cli,
        check_game_dir,
        check_proxy_dll,
        check_nvidia_screenshot_dir,
        check_livetools,
    ]

    results = [check() for check in checks]
    all_fatal_passed = all(r.ok for r in results if r.fatal)

    if verbose:
        print("GamePilot Health Check")
        print("=" * 50)
        for r in results:
            print(f"  {r}")
        print("=" * 50)
        if all_fatal_passed:
            print("  All critical checks passed.")
        else:
            failed = [r for r in results if not r.ok and r.fatal]
            print(f"  {len(failed)} critical check(s) FAILED — agent cannot run.")
        print()

    return results, all_fatal_passed
