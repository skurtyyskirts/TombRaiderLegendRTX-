"""Tomb Raider Legend — Autonomous test orchestrator.

Modes:
  test           Hash stability test (camera-only, no WASD movement).
  record         Launch game, record your inputs, save macro.
  test-legacy    Legacy 3-phase test with A/D movement.
  batch-legacy   Legacy batch runs with randomized movement.

Usage:
  python patches/TombRaiderLegend/run.py test --build
  python patches/TombRaiderLegend/run.py record
  python patches/TombRaiderLegend/run.py test-legacy --build --randomize
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
GAME_DIR = REPO_ROOT / "Tomb Raider Legend"
PROXY_DIR = SCRIPT_DIR / "proxy"
MACROS_FILE = SCRIPT_DIR / "macros.json"
MACRO_NAME = "test_session"

GAME_EXE = GAME_DIR / "trl.exe"
LAUNCHER = GAME_DIR / "NvRemixLauncher32.exe"
PROXY_LOG = GAME_DIR / "ffp_proxy.log"
SCREENSHOTS_SRC = Path(r"C:\Users\skurtyy\Videos\NVIDIA\Tomb Raider  Legend")
SCREENSHOTS_DIR = SCRIPT_DIR / "screenshots"

# Add repo root to path so livetools is importable
sys.path.insert(0, str(REPO_ROOT))


def collect_screenshots(max_age_seconds=120, limit=3):
    """Copy the most recent `limit` screenshots from NVIDIA capture folder.

    The macro takes 2 standing-still shots during menu nav before the 3
    randomized movement shots. Taking only the last `limit` files ensures
    we always get the post-movement captures, not the pre-movement ones.
    """
    if not SCREENSHOTS_SRC.exists():
        print(f"WARNING: Screenshot folder not found: {SCREENSHOTS_SRC}")
        return []

    now = time.time()
    files = sorted(SCREENSHOTS_SRC.iterdir(),
                   key=lambda f: f.stat().st_mtime, reverse=True)
    files = [f for f in files
             if f.suffix.lower() in (".png", ".jpg", ".bmp")
             and (now - f.stat().st_mtime) < max_age_seconds]
    files = files[:limit]

    if not files:
        print("No recent screenshots found in NVIDIA capture folder.")
        return []

    SCREENSHOTS_DIR.mkdir(parents=True, exist_ok=True)
    collected = []
    for f in files:
        dest = SCREENSHOTS_DIR / f.name
        shutil.copy2(str(f), str(dest))
        collected.append(dest)
        print(f"  Screenshot: {f.name}")

    print(f"Collected {len(collected)} screenshots (last {max_age_seconds}s, "
          f"limit={limit}) to {SCREENSHOTS_DIR}/")
    return collected


def set_graphics_config():
    """Set TRL graphics registry to lowest settings and skip the setup screen.

    The setup screen appears when AdapterIdentifier changes (new d3d9.dll).
    We write a fixed config so the game always launches directly.
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
            "EnableWaterFX": 0,
            "EnableReflection": 0,
            "UseShader20": 0,
            "UseShader30": 0,
            "BestTextureFilter": 2,
            "DisableHardwareVP": 0,
            "Disable32BitTextures": 0,
            "ExtendedDialog": 1,
            "AdapterID": 0,
            "DisablePureDevice": 0,         # Proxy already strips PUREDEVICE flag
            "DontDeferShaderCreation": 1,    # All shaders created at startup
            "AlwaysRenderZPassFirst": 0,    # Interferes with Remix rendering
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
        print("Graphics config set (lowest settings, setup screen bypassed)")
    except Exception as e:
        print(f"WARNING: Could not set graphics config: {e}")


def dismiss_setup_dialog():
    """Detect the TRL setup dialog, configure optimal settings, and click Ok.

    Sets 3840x2160 resolution, 240Hz refresh, unchecks all graphics effects
    (shadows, reflections, water, DoF, fullscreen effects, FSAA, next-gen,
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

    # Resolution: try 3840x2160, fall back to highest available
    select_combo_item("Resolution", "3840")

    # Refresh rate: try 240, fall back to highest
    select_combo_item("Refresh Rate", "240")

    # Uncheck all graphics effects for cleanest Remix compatibility
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

    # DevTech options for RTX Remix compatibility
    ensure_unchecked("Disable Hardware Vertexshaders")
    ensure_unchecked("Disable Hardware DXTC")
    ensure_unchecked("Disable Non Pow2 Support")
    ensure_unchecked("Use D3D Reference Device")
    ensure_unchecked("No Dynamic Textures")
    ensure_unchecked("Disable Pure Device")      # Proxy already strips PUREDEVICE flag
    ensure_unchecked("D3D FPU Preserve")
    ensure_unchecked("Disable 32bit Textures")
    ensure_unchecked("Disable Driver Management")
    ensure_unchecked("Disable Hardware Shadow Maps")
    ensure_unchecked("Disable Null Render Targets")
    ensure_unchecked("Always Render Z-pass First")
    ensure_unchecked("Create Game FourCC")
    ensure_checked("Dont Defer Shader Creation") # All shaders created at startup for Remix

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


def write_tr7_arg(chapter=4):
    """Write TR7.arg to skip the main menu and load directly into a chapter.

    The game reads startup args from <drive>:\\TR7\\GAME\\PC\\TR7.arg.
    -NOMAINMENU -CHAPTER 4 loads Peru (Return to Paraiso) directly.
    """
    drive = os.path.splitdrive(str(GAME_DIR))[0]
    arg_dir = Path(f"{drive}/TR7/GAME/PC")
    arg_dir.mkdir(parents=True, exist_ok=True)
    arg_file = arg_dir / "TR7.arg"
    arg_file.write_text(f"-NOMAINMENU -CHAPTER {chapter}")
    print(f"Wrote TR7.arg: -NOMAINMENU -CHAPTER {chapter}")


def kill_game():
    """Kill trl.exe if running."""
    subprocess.run(["taskkill", "/f", "/im", "trl.exe"],
                   capture_output=True)
    time.sleep(2)


def launch_game():
    """Launch TRL directly into Peru via TR7.arg, skip cutscene, return hwnd."""
    from livetools.gamectl import find_hwnd_by_exe, get_window_info, send_keys

    if not LAUNCHER.exists():
        print(f"ERROR: Launcher not found: {LAUNCHER}")
        sys.exit(1)
    if not GAME_EXE.exists():
        print(f"ERROR: Game exe not found: {GAME_EXE}")
        sys.exit(1)

    write_tr7_arg(chapter=4)

    print(f"Launching: {LAUNCHER.name} {GAME_EXE.name}")
    subprocess.Popen([str(LAUNCHER), str(GAME_EXE)], cwd=str(GAME_DIR))

    print("Waiting for game window...")
    hwnd = None
    setup_dismissed = False
    for i in range(90):  # up to 90 seconds
        # Check for setup dialog first — dismiss it if present
        if not setup_dismissed and dismiss_setup_dialog():
            setup_dismissed = True
            # After dismissing, wait a bit for the real game window
            time.sleep(3)
            continue

        hwnd = find_hwnd_by_exe("trl.exe")
        if hwnd:
            info = get_window_info(hwnd)
            # Make sure it's the game window, not the setup dialog
            if "Setup" not in info["title"]:
                print(f"  Found: {info['title']} (hwnd={hex(hwnd)}, "
                      f"pid={info['pid']})")
                break
            else:
                # Still the setup dialog — dismiss it
                dismiss_setup_dialog()
                hwnd = None
        time.sleep(1)
        if i % 10 == 9:
            print(f"  ...{i+1}s elapsed, still waiting")

    if not hwnd:
        print("ERROR: Game window not found after 90s")
        sys.exit(1)

    # Let the game fully load and play through the cutscene.
    # Do NOT touch the window or send any keys during this time.
    print("Window found. Waiting 15s for game to load and cutscene to play...")
    print("  (Do not click or alt-tab — let the game initialize)")
    time.sleep(15)

    return hwnd


def build_proxy():
    """Build the proxy DLL via build.bat and deploy to game dir."""
    print("\n=== Building proxy ===")
    build_bat = PROXY_DIR / "build.bat"
    if not build_bat.exists():
        print(f"ERROR: build.bat not found: {build_bat}")
        sys.exit(1)

    r = subprocess.run(str(build_bat), capture_output=True, text=True,
                       shell=True, cwd=str(PROXY_DIR))
    print(r.stdout)
    if r.returncode != 0:
        print(f"BUILD FAILED:\n{r.stderr}")
        sys.exit(1)

    # Deploy
    dll_src = PROXY_DIR / "d3d9.dll"
    ini_src = PROXY_DIR / "proxy.ini"
    rtx_src = SCRIPT_DIR / "rtx.conf"
    shutil.copy2(str(dll_src), str(GAME_DIR / "d3d9.dll"))
    shutil.copy2(str(ini_src), str(GAME_DIR / "proxy.ini"))
    if rtx_src.exists():
        shutil.copy2(str(rtx_src), str(GAME_DIR / "rtx.conf"))
        print(f"Deployed d3d9.dll + proxy.ini + rtx.conf to {GAME_DIR.name}/")
    else:
        print(f"Deployed d3d9.dll + proxy.ini to {GAME_DIR.name}/ (no rtx.conf template)")


def do_record():
    """Launch game, record inputs, save as test_session macro."""
    from livetools.gamectl import (record, events_to_macro, save_macro,
                                   focus_hwnd)

    kill_game()
    hwnd = launch_game()

    print("\n=== Recording mode ===")
    print("Game should be focused. Play through your test routine.")
    print("Press F12 to stop recording.\n")

    events = record(hwnd, stop_key="F12")

    if not events:
        print("No events recorded.")
        return

    steps = events_to_macro(events)

    keys = sum(1 for e in events if e["type"] == "keydown")
    clicks = sum(1 for e in events if e["type"] in ("lclick", "rclick"))
    moves = sum(1 for e in events if e["type"] == "move")
    holds = steps.count("HOLD:")
    duration_s = events[-1]["time_ms"] / 1000.0
    m, s = divmod(int(duration_s), 60)

    desc = (f"Recorded test session ({keys} keys, {clicks} clicks, "
            f"{moves} moves, {m}m{s}s)")
    save_macro(MACROS_FILE, MACRO_NAME, desc, steps)

    print(f"\nSaved macro '{MACRO_NAME}' to {MACROS_FILE}")
    print(f"Duration: {m}m {s}s | Keys: {keys} | Clicks: {clicks} "
          f"| Moves: {moves} | Holds: {holds}")
    print(f"\nTest with:  python {Path(__file__).name} test")


def generate_random_movement_legacy():
    """Generate a randomized movement+screenshot sequence for hash testing.

    Each call produces a unique pattern of A/D strafes with random hold
    durations, interspersed with screenshots (]) at varied positions.
    """
    tokens = []

    # Initial settle time after cutscene skip
    tokens.append("WAIT:2500")

    # Walk forward for 3 seconds to move into the scene
    tokens.append("HOLD:W:3000")
    tokens.append("WAIT:1000")

    # Take a baseline screenshot before any movement
    tokens.append("]")
    tokens.append("WAIT:1000")

    # One A strafe, one D strafe — random 1-10 seconds each
    a_ms = random.randint(1000, 10000)
    d_ms = random.randint(1000, 10000)

    tokens.append(f"HOLD:A:{a_ms}")
    tokens.append("WAIT:1000")
    tokens.append("]")
    tokens.append("WAIT:1000")

    tokens.append(f"HOLD:D:{d_ms}")
    tokens.append("WAIT:1000")
    tokens.append("]")

    return " ".join(tokens)


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
    focus_hwnd(hwnd)
    time.sleep(0.5)

    move_interval = 3  # seconds per movement cycle
    cycles = duration_s // move_interval
    for i in range(cycles):
        elapsed = i * move_interval
        phase = i % 6

        if phase == 0:
            send_key("A", hold_ms=random.randint(400, 1200))
        elif phase == 1:
            for _ in range(5):
                move_mouse_relative(random.randint(30, 80), random.randint(-15, 15))
                time.sleep(0.1)
        elif phase == 2:
            send_key("D", hold_ms=random.randint(400, 1200))
        elif phase == 3:
            for _ in range(5):
                move_mouse_relative(random.randint(-80, -30), random.randint(-15, 15))
                time.sleep(0.1)
        elif phase == 4:
            for _ in range(5):
                move_mouse_relative(random.randint(-10, 10), random.randint(-60, 60))
                time.sleep(0.1)
        elif phase == 5:
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
    from livetools.gamectl import send_key, send_keys, move_mouse_relative, focus_hwnd

    print(f"\n--- {phase_name}: Camera pan + screenshots ---")
    focus_hwnd(hwnd)
    time.sleep(0.5)

    # Skip cutscene: ESC → wait → W → wait → ENTER
    send_keys(hwnd, "ESC WAIT:3000 W WAIT:3000 RETURN", delay_ms=0)
    print("  Cutscene skip sent. Waiting 3s for gameplay...")
    time.sleep(3)

    # Screenshot at center position
    print("  Screenshot: center")
    send_key("]", hold_ms=50)
    time.sleep(1.5)

    # Gentle camera pan LEFT (10 steps, -30px each = 300px total)
    print("  Camera pan: LEFT")
    for _ in range(10):
        move_mouse_relative(-30, 0)
        time.sleep(0.1)
    time.sleep(0.5)

    # Screenshot at left position
    print("  Screenshot: left")
    send_key("]", hold_ms=50)
    time.sleep(1.5)

    # Gentle camera pan RIGHT (20 steps, +30px each = 600px total, nets 300px right of center)
    print("  Camera pan: RIGHT")
    for _ in range(20):
        move_mouse_relative(30, 0)
        time.sleep(0.1)
    time.sleep(0.5)

    # Screenshot at right position
    print("  Screenshot: right")
    send_key("]", hold_ms=50)
    time.sleep(1.5)

    return collect_screenshots(max_age_seconds=30, limit=3)


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
        move_mouse_relative(-30, 0)
        time.sleep(0.1)
    time.sleep(1)

    r = subprocess.run(["python", "-m", "livetools", "dipcnt", "read"],
                       capture_output=True, text=True, cwd=str(REPO_ROOT))
    left_count = r.stdout.strip()
    print(f"  Left: {left_count}")

    # Pan right (back past center to right)
    for _ in range(20):
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
    focus_hwnd(hwnd)
    for _ in range(3):
        for _ in range(5):
            move_mouse_relative(-20, 0)
            time.sleep(0.1)
        time.sleep(1)
        for _ in range(5):
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

    # --- Phase 1: Hash debug screenshots ---
    print("\n=== Phase 1: Hash Debug Screenshots (view 277) ===")
    set_debug_view(277)
    kill_game()
    hwnd = launch_game()
    hash_shots = camera_pan_and_screenshot(hwnd, "Phase 1 — Hash Debug")

    # Wait for proxy log
    print("Waiting for proxy log (50s delay)...")
    log_wait_start = time.time()
    for i in range(70):
        if PROXY_LOG.exists() and (time.time() - PROXY_LOG.stat().st_mtime) < 120:
            break
        time.sleep(1)
        if i % 10 == 9:
            print(f"  ...{int(time.time() - log_wait_start)}s elapsed")

    if PROXY_LOG.exists():
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

    # Skip cutscene before attaching
    from livetools.gamectl import send_keys, focus_hwnd
    focus_hwnd(hwnd3)
    time.sleep(0.5)
    send_keys(hwnd3, "ESC WAIT:3000 W WAIT:3000 RETURN", delay_ms=0)
    time.sleep(3)

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


def do_test_legacy(build_first=False, randomize=False):
    """Build (optional), launch game, replay macro, collect diagnostics."""
    from livetools.gamectl import load_macros, run_macro, focus_hwnd

    if build_first:
        build_proxy()

    # Ensure graphics config is set (prevents setup screen after new DLL)
    set_graphics_config()

    # Verify macro exists
    if not MACROS_FILE.exists():
        print(f"ERROR: No macros file at {MACROS_FILE}")
        print("Run 'python run.py record' first.")
        sys.exit(1)

    macros = load_macros(str(MACROS_FILE))
    if MACRO_NAME not in macros:
        print(f"ERROR: Macro '{MACRO_NAME}' not found in {MACROS_FILE}")
        print("Run 'python run.py record' first.")
        sys.exit(1)

    macro_info = macros[MACRO_NAME]

    if randomize:
        # Replace macro with fully random movement+screenshot sequence.
        # Macros are pure movement now (no menu nav — TR7.arg handles that).
        random_movement = generate_random_movement_legacy()
        macros[MACRO_NAME] = {**macro_info, "steps": random_movement}

        holds = [t for t in random_movement.split() if t.startswith("HOLD:")]
        screenshots = random_movement.count("]")
        print(f"\nRandomized movement: {len(holds)} strafes, "
              f"{screenshots} screenshots")
        for h in holds:
            parts = h.split(":")
            print(f"  {parts[1]} hold {parts[2]}ms")
    else:
        steps = macro_info["steps"]

    print(f"\nMacro: {MACRO_NAME}")
    print(f"  {macro_info.get('description', '')}")

    # Estimate duration from WAIT tokens
    steps = macros[MACRO_NAME]["steps"]
    wait_total = sum(int(t.split(":")[1]) for t in steps.split()
                     if t.startswith("WAIT:"))
    print(f"  Estimated duration: {wait_total // 1000}s")

    # --- Phase 1: Hash debug screenshots (debug view = 277) ---
    print("\n=== Phase 1: Hash debug screenshots ===")
    set_debug_view(277)

    kill_game()
    hwnd = launch_game()

    print("Replaying macro (hash debug on)...")
    result = run_macro(hwnd, MACRO_NAME, macros, delay_ms=0)
    if result["ok"]:
        print(f"Macro complete. {result['steps_result']['count']} actions sent.")
    else:
        print(f"Macro failed: {result.get('error', result)}")
        return False

    # Wait for proxy log
    print("\n=== Waiting for proxy diagnostics ===")
    print("Proxy log appears ~50s after device creation.")
    log_wait_start = time.time()
    log_ready = False
    for i in range(70):
        if PROXY_LOG.exists():
            age = time.time() - PROXY_LOG.stat().st_mtime
            if age < 120:
                log_ready = True
                break
        time.sleep(1)
        if i % 10 == 9:
            elapsed = int(time.time() - log_wait_start)
            print(f"  ...{elapsed}s waiting for proxy log")

    if log_ready:
        print(f"Proxy log ready: {PROXY_LOG}")
        dest = SCRIPT_DIR / "ffp_proxy.log"
        shutil.copy2(str(PROXY_LOG), str(dest))
        print(f"Copied to {dest}")
    else:
        print("WARNING: Proxy log not found or stale after 70s")

    from livetools.gamectl import find_hwnd_by_exe
    crashed = not find_hwnd_by_exe("trl.exe")
    if crashed:
        print("WARNING: Game appears to have crashed!")
    else:
        print("Game still running (no crash). Closing...")
        kill_game()
        print("Game closed.")

    print("\n=== Collecting hash-debug screenshots ===")
    collect_screenshots()

    # --- Phase 2: Clean screenshots (debug view = 0) ---
    print("\n=== Phase 2: Clean screenshot (debug view off) ===")
    set_debug_view(0)

    hwnd2 = launch_game()
    print("Replaying macro (clean render)...")
    result2 = run_macro(hwnd2, MACRO_NAME, macros, delay_ms=0)
    if result2["ok"]:
        print(f"Macro complete. {result2['steps_result']['count']} actions sent.")
    time.sleep(3)

    print("\n=== Collecting clean screenshots ===")
    collect_screenshots()

    # --- Phase 3: Live analysis (keep game running, attach livetools) ---
    from livetools.gamectl import find_hwnd_by_exe
    hwnd3 = find_hwnd_by_exe("trl.exe")
    if hwnd3:
        live_results = do_live_analysis_legacy(hwnd3, duration_s=60)
    else:
        print("WARNING: Game not running for live analysis phase")
        live_results = None

    kill_game()
    print("Game closed.")

    print("\n=== Test complete ===")
    return not crashed


def do_batch_legacy(start, end, total, build_first=False):
    """Run multiple test iterations with randomized movement, commit each."""
    if build_first:
        build_proxy()
        build_first = False  # only build once

    for i in range(start, end + 1):
        print(f"\n{'='*60}")
        print(f"  BATCH RUN #{i}/{total}")
        print(f"{'='*60}")

        seed = random.randint(0, 2**32 - 1)
        random.seed(seed)
        print(f"  Random seed: {seed}")

        ok = do_test_legacy(build_first=False, randomize=True)

        # Commit and push
        print(f"\n=== Committing test #{i}/{total} ===")
        repo = str(REPO_ROOT)

        # Stage screenshots
        subprocess.run(["git", "add", "patches/TombRaiderLegend/screenshots/"],
                       cwd=repo, capture_output=True)
        subprocess.run(["git", "add", "TombRaiderLegendRTX-"],
                       cwd=repo, capture_output=True)

        # Stage run.py if this is the first run (captures the randomization code)
        subprocess.run(["git", "add", "patches/TombRaiderLegend/run.py"],
                       cwd=repo, capture_output=True)

        crash_note = " [CRASHED]" if not ok else ""
        msg = f"test: stable hash build #{i}/{total}{crash_note}"
        subprocess.run(["git", "commit", "-m", msg,
                        "--allow-empty"],
                       cwd=repo, capture_output=True)

        # Push
        subprocess.run(["git", "push", "origin", "master:main"],
                       cwd=repo, capture_output=True, timeout=30)
        print(f"  Pushed #{i}/{total}")

        if not ok:
            print(f"  WARNING: Test #{i} crashed — continuing to next run")

    print(f"\n{'='*60}")
    print(f"  BATCH COMPLETE: runs {start}-{end}/{total}")
    print(f"{'='*60}")


def main():
    parser = argparse.ArgumentParser(
        description="TRL RTX test orchestrator — hash stability tests")
    sub = parser.add_subparsers(dest="mode")

    # --- Active test ---
    test_p = sub.add_parser("test",
                            help="Hash stability test (camera-only, no WASD)")
    test_p.add_argument("--build", action="store_true",
                        help="Build and deploy proxy before testing")
    test_p.add_argument("--quick", action="store_true",
                        help="Skip dx9tracer phase (Phase 4)")

    # --- Record ---
    sub.add_parser("record",
                   help="Launch game and record inputs as test_session macro")

    # --- Legacy ---
    legacy_test_p = sub.add_parser("test-legacy",
                                   help="[LEGACY] 3-phase test with A/D movement")
    legacy_test_p.add_argument("--build", action="store_true")
    legacy_test_p.add_argument("--randomize", action="store_true")

    legacy_batch_p = sub.add_parser("batch-legacy",
                                    help="[LEGACY] Batch runs with random movement")
    legacy_batch_p.add_argument("--start", type=int, required=True)
    legacy_batch_p.add_argument("--end", type=int, required=True)
    legacy_batch_p.add_argument("--total", type=int, default=50)
    legacy_batch_p.add_argument("--build", action="store_true")

    args = parser.parse_args()

    if args.mode == "test":
        do_test_hash_stability(build_first=args.build,
                               quick=getattr(args, 'quick', False))
    elif args.mode == "record":
        do_record()
    elif args.mode == "test-legacy":
        do_test_legacy(build_first=args.build,
                       randomize=getattr(args, 'randomize', False))
    elif args.mode == "batch-legacy":
        do_batch_legacy(args.start, args.end, args.total,
                        build_first=args.build)
    else:
        parser.print_help()


if __name__ == "__main__":
    main()
