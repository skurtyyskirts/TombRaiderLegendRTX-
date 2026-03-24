"""Tomb Raider Legend — Autonomous test orchestrator.

Two modes:
  record   Launch game, wait for window, record your inputs, save macro.
  test     Launch game, wait for window, replay macro, collect diagnostics.

Usage:
  python patches/TombRaiderLegend/run.py record
  python patches/TombRaiderLegend/run.py test
  python patches/TombRaiderLegend/run.py test --build   (build proxy first)
"""

import argparse
import os
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


def collect_screenshots(max_age_seconds=120):
    """Copy screenshots from the last max_age_seconds from NVIDIA capture folder."""
    if not SCREENSHOTS_SRC.exists():
        print(f"WARNING: Screenshot folder not found: {SCREENSHOTS_SRC}")
        return []

    now = time.time()
    files = sorted(SCREENSHOTS_SRC.iterdir(),
                   key=lambda f: f.stat().st_mtime, reverse=True)
    files = [f for f in files
             if f.suffix.lower() in (".png", ".jpg", ".bmp")
             and (now - f.stat().st_mtime) < max_age_seconds]

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

    print(f"Collected {len(collected)} screenshots (last {max_age_seconds}s) "
          f"to {SCREENSHOTS_DIR}/")
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
            "EnableFSAA": 0,
            "EnableFullscreenEffects": 0,
            "EnableDepthOfField": 0,
            "EnableVSync": 0,
            "EnableShadows": 0,
            "EnableWaterFX": 0,
            "EnableReflection": 0,
            "UseShader20": 0,
            "UseShader30": 1,
            "BestTextureFilter": 2,
            "DisableHardwareVP": 0,
            "Disable32BitTextures": 0,
            "ExtendedDialog": 1,
            "AdapterID": 0,
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
                user32.SendMessageW(hwnd, BM_SETCHECK, BST_UNCHECKED, 0)
                # Also send BM_CLICK to trigger the dialog's change handler
                user32.SendMessageW(hwnd, BM_CLICK, 0, 0)
                print(f"    Unchecked: {label}")

    # Helper: check a checkbox if it's unchecked
    def ensure_checked(label):
        hwnd = children.get(label)
        if hwnd:
            state = user32.SendMessageW(hwnd, BM_GETCHECK, 0, 0)
            if state == BST_UNCHECKED:
                user32.SendMessageW(hwnd, BM_SETCHECK, BST_CHECKED, 0)
                user32.SendMessageW(hwnd, BM_CLICK, 0, 0)
                print(f"    Checked: {label}")

    # Helper: select combobox item containing target text
    def select_combo_item(label_text, target_text):
        # Find the combobox that comes after the label
        # We need to find it by enumerating and checking class
        combo_hwnds = []

        @WNDENUMPROC
        def find_combos(hwnd, _):
            cls = ctypes.create_unicode_buffer(256)
            user32.GetClassNameW(hwnd, cls, 256)
            if cls.value == "ComboBox":
                combo_hwnds.append(hwnd)
            return True

        user32.EnumChildWindows(dialog_hwnd[0], find_combos, 0)

        # Match combo to its preceding label by position
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
            # Combo should be roughly on the same row (within 30px vertical)
            if abs(cr.top - label_rect.top) < 30 and cr.left > label_rect.left:
                dist = cr.left - label_rect.right
                if dist < best_dist:
                    best_dist = dist
                    best_combo = ch

        if not best_combo:
            # Fallback: just try all combos
            for ch in combo_hwnds:
                count = user32.SendMessageW(ch, CB_GETCOUNT, 0, 0)
                for i in range(count):
                    length = user32.SendMessageW(ch, CB_GETLBTEXTLEN, i, 0)
                    if length > 0:
                        buf = ctypes.create_unicode_buffer(length + 1)
                        user32.SendMessageW(ch, CB_GETLBTEXT, i,
                                            ctypes.cast(buf, wt.LPARAM))
                        if target_text.lower() in buf.value.lower():
                            user32.SendMessageW(ch, CB_SETCURSEL, i, 0)
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
                                    ctypes.cast(buf, wt.LPARAM))
                if target_text.lower() in buf.value.lower():
                    user32.SendMessageW(best_combo, CB_SETCURSEL, i, 0)
                    print(f"    {label_text}: {buf.value}")
                    return

        # If exact target not found, select the last item (highest res/rate)
        if count > 0:
            user32.SendMessageW(best_combo, CB_SETCURSEL, count - 1, 0)
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

    # DevTech: keep all unchecked (they should be by default)
    ensure_unchecked("Disable Hardware Vertexshaders")
    ensure_unchecked("Disable Hardware DXTC")
    ensure_unchecked("Disable Non Pow2 Support")
    ensure_unchecked("Use D3D Reference Device")
    ensure_unchecked("No Dynamic Textures")
    ensure_unchecked("Disable Pure Device")
    ensure_unchecked("D3D FPU Preserve")
    ensure_unchecked("Disable 32bit Textures")
    ensure_unchecked("Disable Driver Management")
    ensure_unchecked("Disable Hardware Shadow Maps")
    ensure_unchecked("Disable Null Render Targets")
    ensure_unchecked("Dont Defer Shader Creation")

    time.sleep(0.5)

    # Click Ok to accept and launch
    ok_hwnd = children.get("Ok")
    if ok_hwnd:
        print("  Clicking Ok...")
        user32.SendMessageW(ok_hwnd, BM_CLICK, 0, 0)
        time.sleep(1)
        return True

    return False


def kill_game():
    """Kill trl.exe if running."""
    subprocess.run(["taskkill", "/f", "/im", "trl.exe"],
                   capture_output=True)
    time.sleep(2)


def launch_game():
    """Launch TRL via NvRemixLauncher32 and return once the window appears."""
    from livetools.gamectl import find_hwnd_by_exe, get_window_info

    if not LAUNCHER.exists():
        print(f"ERROR: Launcher not found: {LAUNCHER}")
        sys.exit(1)
    if not GAME_EXE.exists():
        print(f"ERROR: Game exe not found: {GAME_EXE}")
        sys.exit(1)

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

    # Let the game fully load (D3D init, Remix init, shader compilation).
    # Do NOT touch the window or call focus functions — fullscreen D3D9
    # games crash if you trigger focus changes during initialization.
    print("Window found. Waiting 20s for game to fully load...")
    print("  (Do not click or alt-tab — let the game initialize)")
    time.sleep(20)

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
    shutil.copy2(str(dll_src), str(GAME_DIR / "d3d9.dll"))
    shutil.copy2(str(ini_src), str(GAME_DIR / "proxy.ini"))
    print(f"Deployed d3d9.dll + proxy.ini to {GAME_DIR.name}/")


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


def do_test(build_first=False):
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
    print(f"\nMacro: {MACRO_NAME}")
    print(f"  {macro_info.get('description', '')}")

    # Estimate duration from WAIT tokens
    steps = macro_info["steps"]
    wait_total = sum(int(t.split(":")[1]) for t in steps.split()
                     if t.startswith("WAIT:"))
    print(f"  Estimated duration: {wait_total // 1000}s")

    kill_game()
    hwnd = launch_game()

    print("\n=== Replaying test_session macro ===")
    result = run_macro(hwnd, MACRO_NAME, macros, delay_ms=0)

    if result["ok"]:
        print(f"Macro complete. {result['steps_result']['count']} actions sent.")
    else:
        print(f"Macro failed: {result.get('error', result)}")
        return False

    # Wait for proxy log (DIAG_DELAY_MS = 50000 in proxy)
    print("\n=== Waiting for proxy diagnostics ===")
    print("Proxy log appears ~50s after device creation.")

    # Check if log already exists with fresh timestamp
    log_wait_start = time.time()
    log_ready = False
    for i in range(70):  # up to 70 seconds
        if PROXY_LOG.exists():
            age = time.time() - PROXY_LOG.stat().st_mtime
            if age < 120:  # log modified within last 2 minutes
                log_ready = True
                break
        time.sleep(1)
        if i % 10 == 9:
            elapsed = int(time.time() - log_wait_start)
            print(f"  ...{elapsed}s waiting for proxy log")

    if log_ready:
        print(f"Proxy log ready: {PROXY_LOG}")
        # Copy to project dir
        dest = SCRIPT_DIR / "ffp_proxy.log"
        shutil.copy2(str(PROXY_LOG), str(dest))
        print(f"Copied to {dest}")
    else:
        print("WARNING: Proxy log not found or stale after 70s")

    # Check game is still alive
    from livetools.gamectl import find_hwnd_by_exe
    crashed = not find_hwnd_by_exe("trl.exe")
    if crashed:
        print("WARNING: Game appears to have crashed!")
    else:
        print("Game still running (no crash). Closing...")
        kill_game()
        print("Game closed.")

    # Collect NVIDIA screenshots
    print("\n=== Collecting screenshots ===")
    collect_screenshots()

    print("\n=== Test complete ===")
    return not crashed


def main():
    parser = argparse.ArgumentParser(
        description="TRL RTX test orchestrator — record or replay test macros")
    sub = parser.add_subparsers(dest="mode")

    sub.add_parser("record",
                   help="Launch game and record inputs as test_session macro")

    test_p = sub.add_parser("test",
                            help="Launch game, replay macro, collect diagnostics")
    test_p.add_argument("--build", action="store_true",
                        help="Build and deploy proxy before testing")

    args = parser.parse_args()

    if args.mode == "record":
        do_record()
    elif args.mode == "test":
        do_test(build_first=args.build)
    else:
        parser.print_help()


if __name__ == "__main__":
    main()
