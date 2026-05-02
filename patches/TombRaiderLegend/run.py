"""Tomb Raider Legend — hash stability test orchestrator.

Usage:
  python patches/TombRaiderLegend/run.py test-hash --build
"""

import argparse
import os
import random
import shutil
import subprocess
import sys
import time
from pathlib import Path

# Resolve paths relative to this script
SCRIPT_DIR = Path(__file__).resolve().parent
REPO_ROOT = SCRIPT_DIR.parent.parent
PROXY_DIR = SCRIPT_DIR / "proxy"

# Add repo root to path so livetools and config are importable
sys.path.insert(0, str(REPO_ROOT))
from config import (
    GAME_DIR,
    GAME_EXE,
    LAUNCHER,
    NVIDIA_SCREENSHOT_DIR as SCREENSHOTS_SRC,
    PROXY_LOG,
)
from patches.TombRaiderLegend import launcher as stable_launcher
SCREENSHOTS_DIR = SCRIPT_DIR / "screenshots"
NIGHTLY_MOD_FILE = GAME_DIR / "rtx-remix" / "mods" / "trl-nightly" / "mod.usda"
DEFAULT_LAUNCH_CHAPTER = 2
DEFAULT_POST_LOAD_SEQUENCE = "ESC WAIT:3000 W WAIT:3000 RETURN"
DEFAULT_POST_LOAD_SETTLE_SECONDS = 3.0
_MIN_CAPTURE_SIGNAL = 32
_MIN_CAPTURE_STDDEV = 1.0


def collect_screenshots(max_age_seconds=120, limit=3, after_ts=None,
                        destination_dir=None):
    """Copy the most recent `limit` screenshots from NVIDIA capture folder.

    Taking only the most recent files ensures the copied images belong to the
    current camera-pan capture pass instead of stale overlay screenshots from
    a prior run.
    """
    if not SCREENSHOTS_SRC.exists():
        print(f"WARNING: Screenshot folder not found: {SCREENSHOTS_SRC}")
        return []

    now = time.time()
    files = sorted(SCREENSHOTS_SRC.iterdir(),
                   key=lambda f: f.stat().st_mtime, reverse=True)
    files = [f for f in files
             if f.suffix.lower() in (".png", ".jpg", ".bmp")
             and (now - f.stat().st_mtime) < max_age_seconds
             and (after_ts is None or f.stat().st_mtime > after_ts)]
    files = files[:limit]

    if not files:
        print("No recent screenshots found in NVIDIA capture folder.")
        return []

    target_dir = Path(destination_dir) if destination_dir else SCREENSHOTS_DIR
    target_dir.mkdir(parents=True, exist_ok=True)
    collected = []
    for f in files:
        dest = target_dir / f.name
        shutil.copy2(str(f), str(dest))
        collected.append(dest)
        print(f"  Screenshot: {f.name}")

    print(f"Collected {len(collected)} screenshots (last {max_age_seconds}s, "
          f"limit={limit}) to {target_dir}/")
    return collected


def _image_has_signal(image) -> bool:
    """Reject blank/near-black frames from flaky fullscreen capture paths."""
    from PIL import ImageStat

    rgb = image.convert("RGB")
    stat = ImageStat.Stat(rgb)
    mean_brightness = sum(float(v) for v in stat.mean) / 3.0
    max_stddev = max(float(v) for v in stat.stddev)
    max_value = max(channel[1] for channel in rgb.getextrema())
    return (
        max_value >= _MIN_CAPTURE_SIGNAL
        or max_stddev >= _MIN_CAPTURE_STDDEV
        or mean_brightness >= 2.0
    )


def _capture_window_client_image(hwnd):
    import ctypes
    from PIL import ImageGrab

    class RECT(ctypes.Structure):
        _fields_ = [
            ("left", ctypes.c_long),
            ("top", ctypes.c_long),
            ("right", ctypes.c_long),
            ("bottom", ctypes.c_long),
        ]

    class POINT(ctypes.Structure):
        _fields_ = [
            ("x", ctypes.c_long),
            ("y", ctypes.c_long),
        ]

    user32 = ctypes.windll.user32
    rect = RECT()
    if not user32.GetClientRect(hwnd, ctypes.byref(rect)):
        return None

    origin = POINT(0, 0)
    if not user32.ClientToScreen(hwnd, ctypes.byref(origin)):
        return None

    width = rect.right - rect.left
    height = rect.bottom - rect.top
    if width <= 0 or height <= 0:
        return None

    return ImageGrab.grab(
        bbox=(origin.x, origin.y, origin.x + width, origin.y + height),
        all_screens=True,
    )


def capture_window_image(hwnd, destination: Path, *, prefer_nvidia: bool = True,
                         attempts: int = 3):
    """Capture a usable game frame and save it to `destination`."""
    from gamepilot.capture import capture_nvidia
    from livetools.gamectl import find_hwnd_by_exe, focus_hwnd

    for attempt in range(1, attempts + 1):
        refreshed_hwnd = find_hwnd_by_exe("trl.exe")
        if refreshed_hwnd:
            hwnd = refreshed_hwnd
        if not hwnd:
            print(f"WARNING: Capture attempt {attempt}/{attempts} could not find the game window")
            time.sleep(0.4)
            continue

        focus_hwnd(hwnd)
        time.sleep(0.25)
        image = _capture_window_client_image(hwnd)
        if image is None or not _image_has_signal(image):
            image = capture_nvidia(hwnd) if prefer_nvidia else None
        if image is None:
            print(f"WARNING: Capture attempt {attempt}/{attempts} failed for {destination.name}")
            time.sleep(0.4)
            continue
        if not _image_has_signal(image):
            print(f"WARNING: Capture attempt {attempt}/{attempts} produced a blank frame for {destination.name}")
            time.sleep(0.4)
            continue

        destination.parent.mkdir(parents=True, exist_ok=True)
        image.save(destination)
        print(f"  Capture: {destination.name}")
        return destination

    print(f"WARNING: Could not capture a usable frame for {destination.name}")
    return None


def wait_for_fresh_proxy_log(after_ts: float, timeout_seconds: int = 70) -> bool:
    """Wait for a proxy log that was written by the current run."""
    print("\n=== Waiting for proxy diagnostics ===")
    print("Proxy log appears ~50s after device creation.")
    wait_started_at = time.time()
    for i in range(timeout_seconds):
        if PROXY_LOG.exists():
            modified_at = PROXY_LOG.stat().st_mtime
            if modified_at >= after_ts:
                print(f"Proxy log ready: {PROXY_LOG}")
                return True
        time.sleep(1)
        if i % 10 == 9:
            elapsed = int(time.time() - wait_started_at)
            print(f"  ...{elapsed}s waiting for proxy log")

    print(f"WARNING: Proxy log not refreshed after {timeout_seconds}s")
    return False


def deploy_runtime_bundle(proxy_dir=PROXY_DIR, proxy_ini_path=None,
                          rtx_conf_path=None, game_dir=GAME_DIR):
    """Deploy authoritative TRL runtime files to the live game directory."""
    proxy_dir = Path(proxy_dir)
    proxy_ini_path = Path(proxy_ini_path) if proxy_ini_path else proxy_dir / "proxy.ini"
    rtx_conf_path = Path(rtx_conf_path) if rtx_conf_path else SCRIPT_DIR / "rtx.conf"

    dll_src = proxy_dir / "d3d9.dll"
    if not dll_src.exists():
        raise FileNotFoundError(f"Built proxy DLL not found: {dll_src}")
    if not proxy_ini_path.exists():
        raise FileNotFoundError(f"Proxy config not found: {proxy_ini_path}")

    shutil.copy2(str(dll_src), str(game_dir / "d3d9.dll"))
    shutil.copy2(str(proxy_ini_path), str(game_dir / "proxy.ini"))

    game_rtx_conf = game_dir / "rtx.conf"
    if rtx_conf_path.exists() and not game_rtx_conf.exists():
        shutil.copy2(str(rtx_conf_path), str(game_rtx_conf))
        print(f"Deployed d3d9.dll + proxy.ini + rtx.conf (seeded) to {game_dir.name}/")
    elif game_rtx_conf.exists():
        print(f"Deployed d3d9.dll + proxy.ini to {game_dir.name}/ "
              f"(preserved existing rtx.conf with runtime texture tags)")
    else:
        print(f"Deployed d3d9.dll + proxy.ini to {game_dir.name}/ (no rtx.conf template)")


def suspend_nightly_mod_override() -> Path | None:
    if not NIGHTLY_MOD_FILE.exists():
        return None

    disabled_path = NIGHTLY_MOD_FILE.with_suffix(".usda.disabled")
    if disabled_path.exists():
        disabled_path.unlink()
    NIGHTLY_MOD_FILE.replace(disabled_path)
    print(f"Temporarily disabled nightly mod override: {disabled_path.name}")
    return disabled_path


def restore_nightly_mod_override(disabled_path: Path | None) -> None:
    if not disabled_path or not disabled_path.exists():
        return
    disabled_path.replace(NIGHTLY_MOD_FILE)
    print(f"Restored nightly mod override: {NIGHTLY_MOD_FILE.name}")


def build_proxy_bundle(proxy_dir=PROXY_DIR, proxy_ini_path=None,
                       rtx_conf_path=None, game_dir=GAME_DIR):
    """Build and deploy a TRL proxy bundle with the game directory as cwd."""
    proxy_dir = Path(proxy_dir)
    build_bat = proxy_dir / "build.bat"
    if not build_bat.exists():
        raise FileNotFoundError(f"build.bat not found: {build_bat}")

    print("\n=== Building proxy ===")
    r = subprocess.run(
        ["cmd.exe", "/c", build_bat.name],
        capture_output=True,
        text=True,
        shell=False,
        cwd=str(proxy_dir),
    )
    print(r.stdout)
    if r.returncode != 0:
        raise RuntimeError(f"BUILD FAILED:\n{r.stderr}")

    deploy_runtime_bundle(
        proxy_dir=proxy_dir,
        proxy_ini_path=proxy_ini_path,
        rtx_conf_path=rtx_conf_path,
        game_dir=game_dir,
    )


def set_graphics_config():
    import winreg
    gfx_path = r"Software\Crystal Dynamics\Tomb Raider: Legend\Graphics"
    try:
        key = winreg.CreateKeyEx(winreg.HKEY_CURRENT_USER, gfx_path,
                                 0, winreg.KEY_ALL_ACCESS)
        try:
            adapter_guid, _ = winreg.QueryValueEx(key, "AdapterIdentifier")
        except FileNotFoundError:
            adapter_guid = ""
        try:
            mode_id, _ = winreg.QueryValueEx(key, "FullscreenModeID")
        except FileNotFoundError:
            mode_id = 0

        settings = {
            "Fullscreen": 1,
            "Width": 3840,
            "Height": 2160,
            "Refresh": 240,
            "EnableFSAA": 0,
            "EnableFullscreenEffects": 0,
            "EnableDepthOfField": 0,
            "EnableVSync": 0,
            "EnableShadows": 0,
            "EnableWaterFX": 1,
            "EnableReflection": 0,
            "UseShader20": 0,
            "UseShader30": 0,
            "BestTextureFilter": 2,
            "DisableHardwareVP": 0,
            "Disable32BitTextures": 0,
            "ExtendedDialog": 1,
            "AdapterID": 0,
            "DisablePureDevice": 0,
            "DontDeferShaderCreation": 1,
            "AlwaysRenderZPassFirst": 0,
            "CreateGameFourCC": 0,
            "NoDynamicTextures": 0,
            "Shadows": 0,
            "AntiAlias": 0,
            "TextureFilter": 0,
            "NextGenContent": 0,
            "ScreenEffects": 0,
        }
        for name, val in settings.items():
            winreg.SetValueEx(key, name, 0, winreg.REG_DWORD, val)

        if adapter_guid:
            winreg.SetValueEx(key, "AdapterIdentifier", 0,
                              winreg.REG_SZ, adapter_guid)
        if mode_id:
            winreg.SetValueEx(key, "FullscreenModeID", 0,
                              winreg.REG_DWORD, mode_id)

        key.Close()
        print("Graphics config set (setup dialog bypassed)")
    except Exception as e:
        print(f"WARNING: Could not set graphics config: {e}")


def write_tr7_arg(chapter=DEFAULT_LAUNCH_CHAPTER):
    drive = os.path.splitdrive(str(GAME_DIR))[0]
    arg_dir = Path(f"{drive}/TR7/GAME/PC")
    arg_dir.mkdir(parents=True, exist_ok=True)
    arg_file = arg_dir / "TR7.arg"
    arg_file.write_text(f"-NOMAINMENU -CHAPTER {chapter}")
    print(f"Wrote TR7.arg: -NOMAINMENU -CHAPTER {chapter}")


def kill_game():
    for image_name in ("trl.exe", "NvRemixLauncher32.exe", "NvRemixBridge.exe"):
        subprocess.run(["taskkill", "/f", "/im", image_name],
                       capture_output=True)
    time.sleep(2)


def require_live_game_window(hwnd=None, *, context="automation"):
    from livetools.gamectl import find_hwnd_by_exe

    refreshed_hwnd = find_hwnd_by_exe("trl.exe")
    if refreshed_hwnd:
        return refreshed_hwnd
    raise RuntimeError(f"Game window disappeared before {context}")


def launch_game(chapter=DEFAULT_LAUNCH_CHAPTER,
                post_load_sequence=DEFAULT_POST_LOAD_SEQUENCE,
                post_load_settle_seconds=DEFAULT_POST_LOAD_SETTLE_SECONDS):
    route = stable_launcher.choose_launch_route(
        force_continue=stable_launcher.has_checkpoint()
    )

    if (chapter != DEFAULT_LAUNCH_CHAPTER or
            post_load_sequence != DEFAULT_POST_LOAD_SEQUENCE or
            post_load_settle_seconds != DEFAULT_POST_LOAD_SETTLE_SECONDS):
        print("NOTE: Ignoring legacy chapter/cutscene launch parameters; "
              "using stable Peru launcher route")

    print(f"Using stable launch route: {route}")
    try:
        hwnd = stable_launcher.launch_game()
        stable_launcher.navigate_to_peru(hwnd, route=route)
        return require_live_game_window(hwnd, context="Peru gameplay automation")
    except SystemExit:
        raise
    except RuntimeError as exc:
        print(f"ERROR: {exc}")
        sys.exit(1)


def build_proxy():
    try:
        build_proxy_bundle(
            proxy_dir=PROXY_DIR,
            proxy_ini_path=PROXY_DIR / "proxy.ini",
            rtx_conf_path=SCRIPT_DIR / "rtx.conf",
            game_dir=GAME_DIR,
        )
    except Exception as exc:
        print(f"ERROR: {exc}")
        sys.exit(1)


def set_debug_view(idx):
    import re
    rtx_conf = GAME_DIR / "rtx.conf"
    if not rtx_conf.exists():
        return
    text = rtx_conf.read_text()
    new_text = re.sub(
        r'rtx\.debugView\.debugViewIdx\s*=\s*\d+',
        f'rtx.debugView.debugViewIdx = {idx}',
        text
    )
    if new_text != text:
        rtx_conf.write_text(new_text)


def camera_pan_and_screenshot(hwnd, phase_name):
    from livetools.gamectl import send_key, move_mouse_relative, focus_hwnd

    print(f"\n--- {phase_name}: Camera pan + screenshots ---")
    hwnd = require_live_game_window(hwnd, context=f"{phase_name} camera automation")
    focus_hwnd(hwnd)
    time.sleep(0.5)
    capture_started_at = time.time()

    print("  Screenshot: center")
    hwnd = require_live_game_window(hwnd, context=f"{phase_name} center screenshot")
    send_key("]", hold_ms=50)
    time.sleep(1.5)

    print("  Camera pan: LEFT")
    for _ in range(10):
        hwnd = require_live_game_window(hwnd, context=f"{phase_name} left camera pan")
        move_mouse_relative(-30, 0)
        time.sleep(0.1)
    time.sleep(0.5)

    print("  Screenshot: left")
    hwnd = require_live_game_window(hwnd, context=f"{phase_name} left screenshot")
    send_key("]", hold_ms=50)
    time.sleep(1.5)

    print("  Camera pan: RIGHT")
    for _ in range(20):
        hwnd = require_live_game_window(hwnd, context=f"{phase_name} right camera pan")
        move_mouse_relative(30, 0)
        time.sleep(0.1)
    time.sleep(0.5)

    print("  Screenshot: right")
    hwnd = require_live_game_window(hwnd, context=f"{phase_name} right screenshot")
    send_key("]", hold_ms=50)
    time.sleep(1.5)

    return collect_screenshots(max_age_seconds=30, limit=3, after_ts=capture_started_at)


def do_test_hash_stability(build_first=False, quick=False):
    if build_first:
        build_proxy()

    set_graphics_config()
    disabled_nightly_mod = suspend_nightly_mod_override()

    try:
        print("\n=== Phase 1: Hash Debug Screenshots (view 277) ===")
        set_debug_view(277)
        kill_game()
        phase1_started_at = time.time()
        hwnd = launch_game()
        hash_shots = camera_pan_and_screenshot(hwnd, "Phase 1 — Hash Debug")

        log_ready = wait_for_fresh_proxy_log(after_ts=phase1_started_at)
        if log_ready and PROXY_LOG.exists():
            dest = SCRIPT_DIR / "ffp_proxy.log"
            shutil.copy2(str(PROXY_LOG), str(dest))
            print(f"Proxy log copied to {dest}")

        from livetools.gamectl import find_hwnd_by_exe
        crashed_p1 = not find_hwnd_by_exe("trl.exe")
        if crashed_p1:
            print("WARNING: Game crashed during Phase 1!")
        kill_game()

        print("\n=== Phase 2: Clean Render Screenshots (view 0) ===")
        set_debug_view(0)
        hwnd2 = launch_game()
        clean_shots = camera_pan_and_screenshot(hwnd2, "Phase 2 — Clean Render")

        from livetools.gamectl import find_hwnd_by_exe
        crashed_p2 = not find_hwnd_by_exe("trl.exe")
        if crashed_p2:
            print("WARNING: Game crashed during Phase 2!")
        kill_game()

        crashed = crashed_p1 or crashed_p2
        print(f"\n{'='*60}")
        print(f"  HASH STABILITY TEST COMPLETE")
        print(f"  Crashed: {crashed}")
        print(f"  Hash debug screenshots: {len(hash_shots)}")
        print(f"  Clean render screenshots: {len(clean_shots)}")
        print(f"{'='*60}")

        return not crashed
    finally:
        restore_nightly_mod_override(disabled_nightly_mod)


def release_gate_frame_ready(path: Path) -> bool:
    from PIL import Image, ImageStat
    image = Image.open(path).convert("RGB")
    stat = ImageStat.Stat(image)
    mean_brightness = sum(float(v) for v in stat.mean) / 3.0
    return mean_brightness >= 2.0


def count_capture_markers(steps: list) -> int:
    return sum(1 for step in steps if step == "]")


def generate_random_movement_legacy() -> list:
    hold_a = random.randint(300, 1200)
    hold_d = random.randint(300, 1200)
    return [
        "]",
        f"A:{hold_a}",
        "WAIT:200",
        "]",
        f"D:{hold_d}",
        "WAIT:200",
        "]",
    ]


def evaluate_release_gate(
    hash_screenshots: list,
    clean_screenshots: list,
    log_path,
    *,
    crashed: bool = False,
) -> dict:
    import numpy as np
    from PIL import Image
    from patches.TombRaiderLegend.nightly.scoring import evaluate_hash_stability
    from patches.TombRaiderLegend.nightly.model import Rect
    from patches.TombRaiderLegend.nightly.logs import parse_proxy_log
    from patches.TombRaiderLegend.nightly.manifests import load_nightly_config

    _HASH_ROI = Rect(0.18, 0.18, 0.82, 0.88)
    _HASH_PASS_PCT = 50.0
    if not hash_screenshots:
        hash_retention = 0.0
    else:
        hash_retention = evaluate_hash_stability(hash_screenshots, _HASH_ROI)
    hash_stability_passed = hash_retention >= _HASH_PASS_PCT

    _LIGHT_MIN_PCT = 1.0
    lights_passed = bool(clean_screenshots)
    for path in clean_screenshots:
        arr = np.asarray(Image.open(path).convert("RGB"), dtype=np.float32)
        r, g, b = arr[:, :, 0], arr[:, :, 1], arr[:, :, 2]
        red_pct = float(((r > 80) & (r > g * 2.0) & (r > b * 2.0)).mean() * 100.0)
        green_pct = float(((g > 80) & (g > r * 2.0) & (g > b * 1.5)).mean() * 100.0)
        if red_pct < _LIGHT_MIN_PCT or green_pct < _LIGHT_MIN_PCT:
            lights_passed = False
            break

    _MOVEMENT_MIN_DIFF = 0.5
    movement_passed = False
    if len(clean_screenshots) >= 2:
        frames = [
            np.asarray(Image.open(p).convert("RGB"), dtype=np.float32)
            for p in clean_screenshots
        ]
        for a, b in zip(frames, frames[1:]):
            if float(np.abs(a - b).mean()) > _MOVEMENT_MIN_DIFF:
                movement_passed = True
                break

    try:
        tokens = load_nightly_config().required_patch_tokens
    except Exception:
        tokens = []
    summary = parse_proxy_log(log_path, tokens)
    log_passed = (
        summary.all_required_patches_present
        and summary.max_passthrough == 0
        and summary.max_xform_blocked == 0
    )

    overall_passed = (
        not crashed
        and hash_stability_passed
        and lights_passed
        and movement_passed
        and log_passed
    )

    return {
        "hash_stability": {"passed": hash_stability_passed, "retention_pct": hash_retention},
        "lights": {"passed": lights_passed},
        "movement": {"passed": movement_passed},
        "log": {"passed": log_passed},
        "passed": overall_passed,
    }


def main():
    parser = argparse.ArgumentParser(
        description="TRL RTX hash stability test orchestrator")
    sub = parser.add_subparsers(dest="mode")

    # --- Hash-only screening ---
    test_hash_p = sub.add_parser("test-hash",
                                 help="Hash stability test (camera-only, no WASD)")
    test_hash_p.add_argument("--build", action="store_true",
                             help="Build and deploy proxy before testing")
    test_hash_p.add_argument("--quick", action="store_true",
                             help="Skip dx9tracer phase (Phase 4)")

    args = parser.parse_args()

    if args.mode == "test-hash":
        raise SystemExit(0 if do_test_hash_stability(
            build_first=args.build,
            quick=getattr(args, 'quick', False),
        ) else 1)
    else:
        parser.print_help()


if __name__ == "__main__":
    main()
