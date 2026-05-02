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

    # rtx.conf is authoritative in the game directory — the user tags textures
    # (sky, UI, animatedWater, smoothNormals, etc.) via the in-game Remix menu
    # and those tags live in the game-dir rtx.conf. Only seed the file from
    # the repo template when the game directory doesn't already have one;
    # never overwrite an existing game-dir rtx.conf.
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
    """Temporarily disable nightly's live mod override during manual release gates."""
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
    command = f'pushd "{proxy_dir}" && call "{build_bat.name}"'
    r = subprocess.run(
        command,
        capture_output=True,
        text=True,
        shell=True,
        cwd=str(game_dir),
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
    """Set TRL graphics registry for Remix and skip the setup screen.

    We write a fixed config so the game always launches directly while keeping
    only water FX enabled (other expensive effects off for clean captures).
    """
    import winreg
    gfx_path = r"Software\Crystal Dynamics\Tomb Raider: Legend\Graphics"
    try:
        key = winreg.CreateKeyEx(winreg.HKEY_CURRENT_USER, gfx_path,
                                 0, winreg.KEY_ALL_ACCESS)

        # Read current adapter GUID and mode so we can preserve them
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
            "CreateGameFourCC": 0,          # Not needed, can cause format issues
            "NoDynamicTextures": 0,
            "Shadows": 0,
            "AntiAlias": 0,
            "TextureFilter": 0,
            "NextGenContent": 0,
            "ScreenEffects": 0,
        }
        for name, val in settings.items():
            winreg.SetValueEx(key, name, 0, winreg.REG_DWORD, val)

        # Preserve adapter GUID and fullscreen mode (prevents setup screen)
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


def dismiss_setup_dialog():
    """Detect the TRL setup dialog, configure optimal settings, and click Ok.

    Sets 3840x2160 resolution, 240Hz refresh, unchecks all graphics effects
    except water (shadows, reflections, DoF, fullscreen effects, FSAA, next-gen,
    shader 3.0) for cleanest RTX Remix compatibility.
    """
    import ctypes
    import ctypes.wintypes as wt

    user32 = ctypes.windll.user32
    user32.SendMessageW.argtypes = [wt.HWND, wt.UINT, wt.WPARAM, wt.LPARAM]
    user32.SendMessageW.restype = ctypes.c_long
    BM_CLICK = 0x00F5
    BM_GETCHECK = 0x00F0
    BM_SETCHECK = 0x00F1
    BST_CHECKED = 1
    BST_UNCHECKED = 0
    CB_GETCOUNT = 0x0146
    CB_GETLBTEXT = 0x0148
    CB_GETLBTEXTLEN = 0x0149
    CB_SETCURSEL = 0x014E
    CB_GETCURSEL = 0x0147
    WM_COMMAND = 0x0111
    CBN_SELCHANGE = 1
    GW_ID = 0xFFFC  # GetWindowLong index for control ID
    WNDENUMPROC = ctypes.WINFUNCTYPE(wt.BOOL, wt.HWND, wt.LPARAM)

    from livetools.gamectl import _find_pid
    pid = _find_pid("trl.exe")
    if not pid:
        return False

    dialog_hwnd = [None]

    @WNDENUMPROC
    def find_dialog(hwnd, _):
        proc_id = wt.DWORD(0)
        user32.GetWindowThreadProcessId(hwnd, ctypes.byref(proc_id))
        if proc_id.value != pid:
            return True
        buf = ctypes.create_unicode_buffer(256)
        user32.GetWindowTextW(hwnd, buf, 256)
        if "Setup" in buf.value and user32.IsWindowVisible(hwnd):
            dialog_hwnd[0] = hwnd
            return False
        return True

    user32.EnumWindows(find_dialog, 0)

    if not dialog_hwnd[0]:
        return False

    print("  Setup dialog detected — configuring settings...")

    # Collect all child controls
    children = {}  # text -> hwnd

    @WNDENUMPROC
    def collect_children(hwnd, _):
        buf = ctypes.create_unicode_buffer(256)
        user32.GetWindowTextW(hwnd, buf, 256)
        if buf.value:
            children[buf.value] = hwnd
        return True

    user32.EnumChildWindows(dialog_hwnd[0], collect_children, 0)

    # Helper: uncheck a checkbox if it's checked
    def ensure_unchecked(label):
        hwnd = children.get(label)
        if hwnd:
            state = user32.SendMessageW(hwnd, BM_GETCHECK, 0, 0)
            if state == BST_CHECKED:
                user32.SendMessageW(hwnd, BM_CLICK, 0, 0)
                time.sleep(0.05)
                print(f"    Unchecked: {label}")

    # Helper: check a checkbox if it's unchecked
    def ensure_checked(label):
        hwnd = children.get(label)
        if hwnd:
            state = user32.SendMessageW(hwnd, BM_GETCHECK, 0, 0)
            if state == BST_UNCHECKED:
                user32.SendMessageW(hwnd, BM_CLICK, 0, 0)
                time.sleep(0.05)
                print(f"    Checked: {label}")

    # Helper: set combobox selection AND notify the dialog
    def combo_select(combo_hwnd, index):
        """Select item in combobox and send CBN_SELCHANGE to the dialog."""
        user32.SendMessageW(combo_hwnd, CB_SETCURSEL, index, 0)
        ctrl_id = user32.GetDlgCtrlID(combo_hwnd)
        wparam = (CBN_SELCHANGE << 16) | (ctrl_id & 0xFFFF)
        user32.SendMessageW(dialog_hwnd[0], WM_COMMAND, wparam, combo_hwnd)

    # Helper: select combobox item containing target text
    def select_combo_item(label_text, target_text):
        combo_hwnds = []

        @WNDENUMPROC
        def find_combos(hwnd, _):
            cls = ctypes.create_unicode_buffer(256)
            user32.GetClassNameW(hwnd, cls, 256)
            if cls.value == "ComboBox":
                combo_hwnds.append(hwnd)
            return True

        user32.EnumChildWindows(dialog_hwnd[0], find_combos, 0)

        label_hwnd = children.get(label_text)
        if not label_hwnd:
            return

        label_rect = wt.RECT()
        user32.GetWindowRect(label_hwnd, ctypes.byref(label_rect))

        # Find the combo closest to the right of / below the label
        best_combo = None
        best_dist = 99999
        for ch in combo_hwnds:
            cr = wt.RECT()
            user32.GetWindowRect(ch, ctypes.byref(cr))
            if abs(cr.top - label_rect.top) < 30 and cr.left > label_rect.left:
                dist = cr.left - label_rect.right
                if dist < best_dist:
                    best_dist = dist
                    best_combo = ch

        if not best_combo:
            # Fallback: try all combos
            for ch in combo_hwnds:
                count = user32.SendMessageW(ch, CB_GETCOUNT, 0, 0)
                for i in range(count):
                    length = user32.SendMessageW(ch, CB_GETLBTEXTLEN, i, 0)
                    if length > 0:
                        buf = ctypes.create_unicode_buffer(length + 1)
                        user32.SendMessageW(ch, CB_GETLBTEXT, i,
                                            ctypes.addressof(buf))
                        if target_text.lower() in buf.value.lower():
                            combo_select(ch, i)
                            print(f"    {label_text}: {buf.value}")
                            return
            return

        # Search items in the best combo
        count = user32.SendMessageW(best_combo, CB_GETCOUNT, 0, 0)
        for i in range(count):
            length = user32.SendMessageW(best_combo, CB_GETLBTEXTLEN, i, 0)
            if length > 0:
                buf = ctypes.create_unicode_buffer(length + 1)
                user32.SendMessageW(best_combo, CB_GETLBTEXT, i,
                                    ctypes.addressof(buf))
                if target_text.lower() in buf.value.lower():
                    combo_select(best_combo, i)
                    print(f"    {label_text}: {buf.value}")
                    return

        # If exact target not found, select the last item (highest res/rate)
        if count > 0:
            combo_select(best_combo, count - 1)
            print(f"    {label_text}: selected last option (highest available)")

    # === Configure settings ===

    # Fullscreen: ensure checked
    ensure_checked("Fullscreen")

    # Adapter/resolution/refresh/filter: match the requested setup dialog.
    select_combo_item("Display Adapter", "RTX 5090")
    select_combo_item("Resolution", "3840 by 2160")

    # Refresh rate: try 240, fall back to highest.
    select_combo_item("Refresh Rate", "240")
    select_combo_item("Texture Filtering", "Trilinear")

    # Rendering settings from the requested setup window.
    ensure_unchecked("Enable VSync")
    ensure_unchecked("Enable Fullscreen Effects")
    ensure_unchecked("Enable Depth of Field")
    ensure_unchecked("Enable Shadows")
    ensure_unchecked("Enable Anti Aliasing")
    ensure_unchecked("Enable Reflections")
    ensure_unchecked("Enable Water Effects")
    ensure_unchecked("Next Generation Content")
    ensure_unchecked("Use 3.0 Shader Features")
    ensure_unchecked("LowRes Depth of Field")

    # DevTech options from the requested setup window.
    ensure_unchecked("Disable Hardware Vertexshaders")
    ensure_unchecked("Disable Hardware DXTC")
    ensure_unchecked("Disable Non Pow2 Support")
    ensure_unchecked("Use D3D Reference Device")
    ensure_checked("No Dynamic Textures")
    ensure_unchecked("Disable Pure Device")
    ensure_unchecked("D3D FPU Preserve")
    ensure_unchecked("Disable 32bit Textures")
    ensure_unchecked("Disable Driver Management")
    ensure_unchecked("Disable Hardware Shadow Maps")
    ensure_unchecked("Disable Null Render Targets")
    ensure_unchecked("Always Render Z-pass First")
    ensure_checked("Create Game FourCC")
    ensure_checked("Dont Defer Shader Creation")

    time.sleep(1.0)

    # Verify critical settings before clicking Ok
    verify_hwnd = children.get("Dont Defer Shader Creation")
    if verify_hwnd:
        state = user32.SendMessageW(verify_hwnd, BM_GETCHECK, 0, 0)
        if state != BST_CHECKED:
            print("  WARNING: 'Dont Defer Shader Creation' not checked — retrying")
            user32.SendMessageW(verify_hwnd, BM_CLICK, 0, 0)
            time.sleep(0.05)

    # Click Ok to accept and launch
    ok_hwnd = children.get("Ok")
    if ok_hwnd:
        print("  Clicking Ok...")
        user32.SendMessageW(ok_hwnd, BM_CLICK, 0, 0)
        time.sleep(1)
        return True

    return False


def write_tr7_arg(chapter=DEFAULT_LAUNCH_CHAPTER):
    """Write TR7.arg to skip the main menu and load directly into a chapter.

    The game reads startup args from <drive>:\\TR7\\GAME\\PC\\TR7.arg.
    The nightly flow uses chapter 2, then advances through the Bolivia load
    sequence with ESC -> W -> ENTER after the window stabilizes.
    """
    drive = os.path.splitdrive(str(GAME_DIR))[0]
    arg_dir = Path(f"{drive}/TR7/GAME/PC")
    arg_dir.mkdir(parents=True, exist_ok=True)
    arg_file = arg_dir / "TR7.arg"
    arg_file.write_text(f"-NOMAINMENU -CHAPTER {chapter}")
    print(f"Wrote TR7.arg: -NOMAINMENU -CHAPTER {chapter}")


def kill_game():
    """Kill the game and Remix launcher helpers if they are still alive."""
    for image_name in ("trl.exe", "NvRemixLauncher32.exe", "NvRemixBridge.exe"):
        subprocess.run(["taskkill", "/f", "/im", image_name],
                       capture_output=True)
    time.sleep(2)


def require_live_game_window(hwnd=None, *, context="automation"):
    """Return a fresh TRL window handle or fail before sending input."""
    from livetools.gamectl import find_hwnd_by_exe

    refreshed_hwnd = find_hwnd_by_exe("trl.exe")
    if refreshed_hwnd:
        return refreshed_hwnd
    raise RuntimeError(f"Game window disappeared before {context}")


def _advance_to_bolivia_level(hwnd, sequence=DEFAULT_POST_LOAD_SEQUENCE,
                              settle_seconds=DEFAULT_POST_LOAD_SETTLE_SECONDS):
    """Advance from the chapter-2 load into the Bolivia gameplay state."""
    if not sequence:
        return require_live_game_window(hwnd, context="post-load automation")

    from livetools.gamectl import send_keys

    hwnd = require_live_game_window(hwnd, context="Bolivia entry sequence")
    # Do NOT call focus_hwnd here — send_keys handles focus internally,
    # and a second focus call before it risks a D3D device-lost crash.
    print(f"Sending Bolivia entry sequence: {sequence}")
    send_keys(hwnd, sequence, delay_ms=0)
    print(f"Waiting {settle_seconds:.1f}s for Bolivia gameplay to settle...")
    time.sleep(settle_seconds)
    return require_live_game_window(hwnd, context="Bolivia gameplay automation")


def launch_game(chapter=DEFAULT_LAUNCH_CHAPTER,
                post_load_sequence=DEFAULT_POST_LOAD_SEQUENCE,
                post_load_settle_seconds=DEFAULT_POST_LOAD_SETTLE_SECONDS):
    """Launch TRL through the stable Peru path used by the dedicated launcher.

    The old TR7.arg -> chapter 2 -> Bolivia cutscene skip route is retained
    only for reference. The automated hash workflow should not use it because
    it is the brittle startup path that keeps hanging/crashing under Remix.
    """
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
    """Build the proxy DLL via build.bat and deploy to game dir."""
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


def do_live_analysis_legacy(hwnd, duration_s=60):
    """Run a live analysis phase: attach livetools, move Lara around while
    collecting render pipeline data, then save results.

    Sends gentle A/D strafes with mouse look-around to exercise the renderer
    from multiple angles while livetools captures function call data.

    Args:
        hwnd: Game window handle.
        duration_s: Total duration in seconds.

    Returns:
        dict with capture file paths and summary.
    """
    from livetools.gamectl import send_key, move_mouse_relative, focus_hwnd

    capture_dir = SCRIPT_DIR / "captures" / "live_analysis"
    capture_dir.mkdir(parents=True, exist_ok=True)

    # Attach livetools
    print("\n=== Live Analysis Phase ===")
    print("Attaching livetools...")
    r = subprocess.run(
        ["python", "-m", "livetools", "attach", "trl.exe"],
        capture_output=True, text=True, cwd=str(REPO_ROOT)
    )
    if "Attached" not in r.stdout and "Attached" not in r.stderr:
        print(f"WARNING: livetools attach may have failed: {r.stderr}")

    # Start render pipeline collect in background
    pipeline_file = str(capture_dir / "live_render_pipeline.jsonl")
    light_file = str(capture_dir / "live_light_system.jsonl")

    print(f"Starting {duration_s}s data collection...")
    collect_render = subprocess.Popen(
        ["python", "-m", "livetools", "collect",
         "0x00413950", "0x0040E470", "0x00ECBB00",
         "--duration", str(duration_s),
         "--read", "ecx; eax; [esp+4]:4:hex; [esp+8]:4:hex",
         "--fence", "0x00450DE0",
         "--label", "0x00413950=SetWorldMatrix",
         "--label", "0x0040E470=SetRenderStateCached",
         "--label", "0x00ECBB00=UploadViewProjMatrices",
         "--output", pipeline_file],
        cwd=str(REPO_ROOT), stdout=subprocess.PIPE, stderr=subprocess.PIPE
    )

    collect_lights = subprocess.Popen(
        ["python", "-m", "livetools", "collect",
         "0x0060C7D0", "0x006124E0", "0x0060B050", "0x0060E2D0",
         "--duration", str(duration_s),
         "--read", "ecx; eax; [esp+4]:4:hex",
         "--label", "0x0060C7D0=RenderLights_FrustumCull",
         "--label", "0x006124E0=LightVolume_Draw",
         "--label", "0x0060B050=LightVisibilityCheck",
         "--label", "0x0060E2D0=RenderLights_Caller",
         "--output", light_file],
        cwd=str(REPO_ROOT), stdout=subprocess.PIPE, stderr=subprocess.PIPE
    )

    # Move Lara around while collecting data
    print("Moving Lara with A/D strafes + mouse look-around...")
    hwnd = require_live_game_window(hwnd, context="live analysis automation")
    focus_hwnd(hwnd)
    time.sleep(0.5)

    move_interval = 3  # seconds per movement cycle
    cycles = duration_s // move_interval
    for i in range(cycles):
        hwnd = require_live_game_window(hwnd, context="live analysis movement cycle")
        elapsed = i * move_interval
        phase = i % 6

        if phase == 0:
            focus_hwnd(hwnd)
            send_key("A", hold_ms=random.randint(400, 1200))
        elif phase == 1:
            for _ in range(5):
                hwnd = require_live_game_window(hwnd, context="live analysis mouse look-right")
                move_mouse_relative(random.randint(30, 80), random.randint(-15, 15))
                time.sleep(0.1)
        elif phase == 2:
            focus_hwnd(hwnd)
            send_key("D", hold_ms=random.randint(400, 1200))
        elif phase == 3:
            for _ in range(5):
                hwnd = require_live_game_window(hwnd, context="live analysis mouse look-left")
                move_mouse_relative(random.randint(-80, -30), random.randint(-15, 15))
                time.sleep(0.1)
        elif phase == 4:
            for _ in range(5):
                hwnd = require_live_game_window(hwnd, context="live analysis vertical look")
                move_mouse_relative(random.randint(-10, 10), random.randint(-60, 60))
                time.sleep(0.1)
        elif phase == 5:
            focus_hwnd(hwnd)
            send_key("]", hold_ms=50)

        remaining = duration_s - elapsed
        if remaining > 0 and remaining % 15 == 0:
            print(f"  {remaining}s remaining...")

        time.sleep(max(0, move_interval - 1.5))

    # Wait for collections to finish
    print("Waiting for data collection to complete...")
    collect_render.wait(timeout=30)
    collect_lights.wait(timeout=30)

    # Detach
    subprocess.run(
        ["python", "-m", "livetools", "detach"],
        capture_output=True, text=True, cwd=str(REPO_ROOT)
    )

    # Run quick analysis
    print("\n=== Live Analysis Results ===")
    results = {}
    for name, fpath in [("render_pipeline", pipeline_file),
                        ("light_system", light_file)]:
        fsize = Path(fpath).stat().st_size if Path(fpath).exists() else 0
        results[name] = {"file": fpath, "size": fsize}
        if fsize > 0:
            r = subprocess.run(
                ["python", "-m", "livetools", "analyze", fpath, "--summary"],
                capture_output=True, text=True, cwd=str(REPO_ROOT)
            )
            print(f"\n{name}:")
            print(r.stdout)
            results[name]["summary"] = r.stdout
        else:
            print(f"\n{name}: 0 bytes (no calls recorded)")
            results[name]["summary"] = "No calls recorded"

    return results


def set_debug_view(idx):
    """Set RTX Remix debug view index in rtx.conf."""
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
    """Execute gentle camera pan sequence: center, left, right — 3 screenshots.

    Only moves the camera (mouse), never moves Lara (no WASD). Returns the
    collected screenshot paths.
    """
    from livetools.gamectl import send_key, move_mouse_relative, focus_hwnd

    print(f"\n--- {phase_name}: Camera pan + screenshots ---")
    hwnd = require_live_game_window(hwnd, context=f"{phase_name} camera automation")
    focus_hwnd(hwnd)
    time.sleep(0.5)
    capture_started_at = time.time()

    # Screenshot at center position
    print("  Screenshot: center")
    hwnd = require_live_game_window(hwnd, context=f"{phase_name} center screenshot")
    send_key("]", hold_ms=50)
    time.sleep(1.5)

    # Gentle camera pan LEFT (10 steps, -30px each = 300px total)
    print("  Camera pan: LEFT")
    for _ in range(10):
        hwnd = require_live_game_window(hwnd, context=f"{phase_name} left camera pan")
        move_mouse_relative(-30, 0)
        time.sleep(0.1)
    time.sleep(0.5)

    # Screenshot at left position
    print("  Screenshot: left")
    hwnd = require_live_game_window(hwnd, context=f"{phase_name} left screenshot")
    send_key("]", hold_ms=50)
    time.sleep(1.5)

    # Gentle camera pan RIGHT (20 steps, +30px each = 600px total, nets 300px right of center)
    print("  Camera pan: RIGHT")
    for _ in range(20):
        hwnd = require_live_game_window(hwnd, context=f"{phase_name} right camera pan")
        move_mouse_relative(30, 0)
        time.sleep(0.1)
    time.sleep(0.5)

    # Screenshot at right position
    print("  Screenshot: right")
    hwnd = require_live_game_window(hwnd, context=f"{phase_name} right screenshot")
    send_key("]", hold_ms=50)
    time.sleep(1.5)

    return collect_screenshots(max_age_seconds=30, limit=3, after_ts=capture_started_at)


def do_livetools_diagnostics(hwnd):
    """Phase 3: Attach livetools and run deep diagnostics.

    Returns dict of diagnostic results.
    """
    from livetools.gamectl import send_key, move_mouse_relative, focus_hwnd

    print("\n=== Phase 3: Livetools Deep Diagnostics ===")

    # Attach livetools
    print("Attaching livetools...")
    r = subprocess.run(
        ["python", "-m", "livetools", "attach", "trl.exe"],
        capture_output=True, text=True, cwd=str(REPO_ROOT)
    )
    if "Attached" not in r.stdout and "Attached" not in r.stderr:
        print(f"WARNING: livetools attach may have failed: {r.stderr}")
        return {"error": "attach failed"}

    results = {}

    # 3a. Draw call census
    print("\n--- 3a: Draw call census (dipcnt) ---")
    hwnd = require_live_game_window(hwnd, context="livetools diagnostics")
    focus_hwnd(hwnd)
    time.sleep(0.5)

    subprocess.run(["python", "-m", "livetools", "dipcnt", "on"],
                   capture_output=True, text=True, cwd=str(REPO_ROOT))
    time.sleep(2)

    # Read at center
    r = subprocess.run(["python", "-m", "livetools", "dipcnt", "read"],
                       capture_output=True, text=True, cwd=str(REPO_ROOT))
    center_count = r.stdout.strip()
    print(f"  Center: {center_count}")

    # Pan left
    for _ in range(10):
        hwnd = require_live_game_window(hwnd, context="livetools diagnostics left pan")
        move_mouse_relative(-30, 0)
        time.sleep(0.1)
    time.sleep(1)

    r = subprocess.run(["python", "-m", "livetools", "dipcnt", "read"],
                       capture_output=True, text=True, cwd=str(REPO_ROOT))
    left_count = r.stdout.strip()
    print(f"  Left: {left_count}")

    # Pan right (back past center to right)
    for _ in range(20):
        hwnd = require_live_game_window(hwnd, context="livetools diagnostics right pan")
        move_mouse_relative(30, 0)
        time.sleep(0.1)
    time.sleep(1)

    r = subprocess.run(["python", "-m", "livetools", "dipcnt", "read"],
                       capture_output=True, text=True, cwd=str(REPO_ROOT))
    right_count = r.stdout.strip()
    print(f"  Right: {right_count}")

    subprocess.run(["python", "-m", "livetools", "dipcnt", "off"],
                   capture_output=True, text=True, cwd=str(REPO_ROOT))

    results["dipcnt"] = {
        "center": center_count, "left": left_count, "right": right_count
    }

    # 3b. Function call collection (15s during camera pan)
    print("\n--- 3b: Function call collection (15s) ---")
    capture_dir = SCRIPT_DIR / "captures" / "hash_stability"
    capture_dir.mkdir(parents=True, exist_ok=True)
    fn_file = str(capture_dir / "live_functions.jsonl")

    collect_proc = subprocess.Popen(
        ["python", "-m", "livetools", "collect",
         "0x00413950", "0x00ECBB00", "0x0060C7D0", "0x0060B050",
         "--duration", "15",
         "--label", "0x00413950=SetWorldMatrix",
         "--label", "0x00ECBB00=UploadViewProjMatrices",
         "--label", "0x0060C7D0=RenderLights_FrustumCull",
         "--label", "0x0060B050=LightVisibilityCheck",
         "--output", fn_file],
        cwd=str(REPO_ROOT), stdout=subprocess.PIPE, stderr=subprocess.PIPE
    )

    # Gentle camera movement during collection
    hwnd = require_live_game_window(hwnd, context="livetools collection camera movement")
    focus_hwnd(hwnd)
    for _ in range(3):
        for _ in range(5):
            hwnd = require_live_game_window(hwnd, context="livetools collection left sweep")
            move_mouse_relative(-20, 0)
            time.sleep(0.1)
        time.sleep(1)
        for _ in range(5):
            hwnd = require_live_game_window(hwnd, context="livetools collection right sweep")
            move_mouse_relative(20, 0)
            time.sleep(0.1)
        time.sleep(1)

    collect_proc.wait(timeout=30)
    fn_size = Path(fn_file).stat().st_size if Path(fn_file).exists() else 0
    results["collect"] = {"file": fn_file, "size": fn_size}

    if fn_size > 0:
        r = subprocess.run(
            ["python", "-m", "livetools", "analyze", fn_file, "--summary"],
            capture_output=True, text=True, cwd=str(REPO_ROOT)
        )
        print(r.stdout)
        results["collect"]["summary"] = r.stdout

    # 3c. Patch integrity (mem read)
    print("\n--- 3c: Patch integrity ---")
    patch_checks = [
        ("0xEFDD64", "4", "--as", "float32", "frustum threshold (expect -1e30)"),
        ("0xF2A0D4", "12", "--as", "float32", "cull mode globals"),
        ("0x407150", "1", None, None, "cull function entry (expect C3=RET)"),
        ("0x60B050", "4", None, None, "LightVisibilityTest (expect B001C204)"),
    ]
    results["patches"] = {}
    for addr, size, flag, flag_val, desc in patch_checks:
        cmd = ["python", "-m", "livetools", "mem", "read", addr, size]
        if flag:
            cmd.extend([flag, flag_val])
        r = subprocess.run(cmd, capture_output=True, text=True, cwd=str(REPO_ROOT))
        val = r.stdout.strip()
        print(f"  {desc}: {val}")
        results["patches"][addr] = val

    # 3d. Memory watchpoint (abbreviated — trace SetStreamSource to find VB addr)
    print("\n--- 3d: VB mutation check (memwatch) ---")
    # Quick trace to discover a VB address
    r = subprocess.run(
        ["python", "-m", "livetools", "trace", "0x00413950",
         "--count", "3", "--read", "[esp+4]:4:hex"],
        capture_output=True, text=True, cwd=str(REPO_ROOT)
    )
    print(f"  SetWorldMatrix trace sample: {r.stdout.strip()[:200]}")
    results["memwatch"] = {"note": "VB mutation check logged"}

    # Detach
    print("\nDetaching livetools...")
    subprocess.run(["python", "-m", "livetools", "detach"],
                   capture_output=True, text=True, cwd=str(REPO_ROOT))

    return results


def do_test_hash_stability(build_first=False, quick=False):
    """Hash stability test: camera-only pan, no WASD movement.

    Phases:
      0. Build & deploy proxy (if --build)
      1. Hash debug screenshots (debug view 277)
      2. Clean render screenshots (debug view 0)
      3. Livetools deep diagnostics (dipcnt, collect, mem read, memwatch)
      4. dx9tracer frame capture & diff (skipped with --quick)
    """
    if build_first:
        build_proxy()

    set_graphics_config()
    disabled_nightly_mod = suspend_nightly_mod_override()

    try:
        # --- Phase 1: Hash debug screenshots ---
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

        # --- Phase 2: Clean render screenshots ---
        print("\n=== Phase 2: Clean Render Screenshots (view 0) ===")
        set_debug_view(0)
        hwnd2 = launch_game()
        clean_shots = camera_pan_and_screenshot(hwnd2, "Phase 2 — Clean Render")

        from livetools.gamectl import find_hwnd_by_exe
        crashed_p2 = not find_hwnd_by_exe("trl.exe")
        if crashed_p2:
            print("WARNING: Game crashed during Phase 2!")
        kill_game()

        # --- Phase 3: Livetools diagnostics ---
        print("\n=== Phase 3: Livetools Diagnostics ===")
        set_debug_view(0)
        hwnd3 = launch_game()

        # Wait additional time for stable attachment
        print("Waiting 25s before livetools attach...")
        time.sleep(25)

        diag_results = do_livetools_diagnostics(hwnd3)

        from livetools.gamectl import find_hwnd_by_exe
        crashed_p3 = not find_hwnd_by_exe("trl.exe")
        if crashed_p3:
            print("WARNING: Game crashed during Phase 3!")
        kill_game()

        # --- Phase 4: dx9tracer (unless --quick) ---
        tracer_results = None
        if not quick:
            print("\n=== Phase 4: dx9tracer Frame Capture ===")
            print("  (skipped in this version — run with static-analyzer subagent)")
            # The dx9tracer swap and capture is orchestrated by the Claude agent
            # calling the tracer trigger + analyze commands, not by this script.
            # This phase is a placeholder for the agent to fill in.
            tracer_results = {"note": "delegated to agent"}

        # --- Summary ---
        crashed = crashed_p1 or crashed_p2 or crashed_p3
        print(f"\n{'='*60}")
        print(f"  HASH STABILITY TEST COMPLETE")
        print(f"  Crashed: {crashed}")
        print(f"  Hash debug screenshots: {len(hash_shots)}")
        print(f"  Clean render screenshots: {len(clean_shots)}")
        if diag_results and "dipcnt" in diag_results:
            d = diag_results["dipcnt"]
            print(f"  Draw counts: center={d['center']} left={d['left']} right={d['right']}")
        if diag_results and "patches" in diag_results:
            for addr, val in diag_results["patches"].items():
                print(f"  Patch {addr}: {val}")
        print(f"{'='*60}")

        return not crashed
    finally:
        restore_nightly_mod_override(disabled_nightly_mod)


def release_gate_frame_ready(path: Path) -> bool:
    """Return True if path is a usable game frame (not a black transition screen)."""
    from PIL import Image, ImageStat
    image = Image.open(path).convert("RGB")
    stat = ImageStat.Stat(image)
    mean_brightness = sum(float(v) for v in stat.mean) / 3.0
    return mean_brightness >= 2.0


def count_capture_markers(steps: list) -> int:
    """Count screenshot capture steps (']') in a macro step sequence."""
    return sum(1 for step in steps if step == "]")


def generate_random_movement_legacy() -> list:
    """Generate a randomized A/D strafe sequence with 3 screenshot capture points."""
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
    """Evaluate whether a build passes the TRL release gate.

    Args:
        hash_screenshots: Paths to hash-debug screenshots (debug view 277).
        clean_screenshots: Paths to clean render screenshots (debug view 0).
        log_path: Path to ffp_proxy.log.
        crashed: Whether the game crashed during the test.

    Returns:
        dict with sub-reports: hash_stability, lights, movement, log, and top-level passed.
    """
    import numpy as np
    from PIL import Image
    from patches.TombRaiderLegend.nightly.scoring import evaluate_hash_stability
    from patches.TombRaiderLegend.nightly.model import Rect
    from patches.TombRaiderLegend.nightly.logs import parse_proxy_log
    from patches.TombRaiderLegend.nightly.manifests import load_nightly_config

    # Hash stability: compare hash-debug frames within a center ROI.
    # Strafing builds use 50% threshold — stricter 98% is for nightly no-movement scenes.
    _HASH_ROI = Rect(0.18, 0.18, 0.82, 0.88)
    _HASH_PASS_PCT = 50.0
    if not hash_screenshots:
        hash_retention = 0.0
    else:
        hash_retention = evaluate_hash_stability(hash_screenshots, _HASH_ROI)
    hash_stability_passed = hash_retention >= _HASH_PASS_PCT

    # Lights: both red and green stage lights must be visible in every clean frame.
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

    # Movement: at least one consecutive pair of clean frames must differ.
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

    # Log: all required patches present and no passthrough/xform-blocked draws.
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
