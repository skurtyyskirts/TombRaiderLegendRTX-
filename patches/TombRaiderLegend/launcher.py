"""Tomb Raider Legend -- Fast Peru launcher.

Launches TRL and loads directly into the Peru level with minimal menu
interaction. On first run, navigates New Game -> Peru and captures a
checkpoint save. Subsequent runs restore that checkpoint and use Continue
to skip the menu and cutscene entirely.

Usage:
  python patches/TombRaiderLegend/launcher.py             # auto-detect fastest path
  python patches/TombRaiderLegend/launcher.py --fresh      # force New Game route
  python patches/TombRaiderLegend/launcher.py --attach     # attach livetools after load
  python patches/TombRaiderLegend/launcher.py --build      # build+deploy proxy first
"""

import argparse
import shutil
import subprocess
import sys
import time
from pathlib import Path

SCRIPT_DIR = Path(__file__).resolve().parent
REPO_ROOT = SCRIPT_DIR.parent.parent
GAME_DIR = REPO_ROOT / "Tomb Raider Legend"
GAME_EXE = GAME_DIR / "trl.exe"
LAUNCHER = GAME_DIR / "NvRemixLauncher32.exe"

SAVE_DIR = Path.home() / "Documents" / "Tomb Raider - Legend"
AUTOSAVE = SAVE_DIR / "autosave.dat"
CHECKPOINT = SCRIPT_DIR / "peru_checkpoint.dat"

sys.path.insert(0, str(REPO_ROOT))


def ensure_save_dir():
    SAVE_DIR.mkdir(parents=True, exist_ok=True)
    (SAVE_DIR / "Saved Games").mkdir(exist_ok=True)


def has_checkpoint():
    return CHECKPOINT.exists()


def restore_checkpoint():
    """Copy the Peru checkpoint save as the autosave so Continue loads Peru."""
    ensure_save_dir()
    shutil.copy2(str(CHECKPOINT), str(AUTOSAVE))
    print(f"Restored Peru checkpoint as autosave")


def capture_checkpoint():
    """Capture the current autosave as the Peru checkpoint for future runs."""
    if AUTOSAVE.exists():
        shutil.copy2(str(AUTOSAVE), str(CHECKPOINT))
        print(f"Captured Peru checkpoint: {CHECKPOINT.name}")
    else:
        print("WARNING: No autosave to capture -- launch Peru manually first")


def set_graphics_config():
    """Set TRL registry to skip the setup dialog on launch."""
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
            "Fullscreen": 1, "Width": 3840, "Height": 2160, "Refresh": 240,
            "EnableFSAA": 0, "EnableFullscreenEffects": 0,
            "EnableDepthOfField": 0, "EnableVSync": 0, "EnableShadows": 0,
            "EnableWaterFX": 0, "EnableReflection": 0, "UseShader20": 0,
            "UseShader30": 0, "BestTextureFilter": 2,
            "DisableHardwareVP": 0, "Disable32BitTextures": 0,
            "ExtendedDialog": 1, "AdapterID": 0, "DisablePureDevice": 0,
            "DontDeferShaderCreation": 1, "AlwaysRenderZPassFirst": 0,
            "CreateGameFourCC": 0, "NoDynamicTextures": 0,
            "Shadows": 0, "AntiAlias": 0, "TextureFilter": 0,
            "NextGenContent": 0, "ScreenEffects": 0,
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
        print("Registry configured (setup dialog bypassed)")
    except Exception as e:
        print(f"WARNING: Could not set registry: {e}")


def launch_game():
    """Launch TRL and wait for the game window. Returns hwnd."""
    from livetools.gamectl import find_hwnd_by_exe, get_window_info

    # Import dismiss_setup_dialog from run.py if available
    try:
        sys.path.insert(0, str(SCRIPT_DIR))
        from run import dismiss_setup_dialog
    except ImportError:
        dismiss_setup_dialog = None

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
    for i in range(90):
        if not setup_dismissed and dismiss_setup_dialog:
            if dismiss_setup_dialog():
                setup_dismissed = True
                time.sleep(3)
                continue

        hwnd = find_hwnd_by_exe("trl.exe")
        if hwnd:
            info = get_window_info(hwnd)
            if "Setup" not in info["title"]:
                print(f"  Found: {info['title']} (hwnd={hex(hwnd)})")
                break
            elif dismiss_setup_dialog:
                dismiss_setup_dialog()
            hwnd = None
        time.sleep(1)
        if i % 10 == 9:
            print(f"  ...{i+1}s elapsed, still waiting")

    if not hwnd:
        print("ERROR: Game window not found after 90s")
        sys.exit(1)

    # Critical: don't touch focus during D3D init
    print("Window found. Waiting 20s for D3D/Remix init...")
    print("  (Do not click or alt-tab)")
    time.sleep(20)

    return hwnd


def navigate_to_peru(hwnd):
    """From main menu, navigate to Peru and skip the cutscene.

    Uses the user-recorded menu_nav sequence.
    """
    from livetools.gamectl import send_keys

    print("Navigating to Peru (recorded macro)...")
    send_keys(hwnd, "WAIT:5850 ESCAPE WAIT:6250 RETURN WAIT:2650 DOWN WAIT:1900 RETURN WAIT:1550 RETURN WAIT:6400 ESCAPE WAIT:1550 UP WAIT:1550 RETURN", delay_ms=0)

    time.sleep(3)
    print("Peru loaded -- ready to play")

    # Capture autosave as checkpoint for future fast launches
    capture_checkpoint()


def attach_livetools():
    """Attach livetools to the running game."""
    print("\nAttaching livetools...")
    r = subprocess.run(
        ["python", "-m", "livetools", "attach", "trl.exe"],
        capture_output=True, text=True, cwd=str(REPO_ROOT)
    )
    if "Attached" in r.stdout or "Attached" in r.stderr:
        print("Livetools attached successfully")
    else:
        print(f"WARNING: livetools may have failed: {r.stdout} {r.stderr}")
        # Retry once
        time.sleep(5)
        r = subprocess.run(
            ["python", "-m", "livetools", "attach", "trl.exe"],
            capture_output=True, text=True, cwd=str(REPO_ROOT)
        )
        if "Attached" in r.stdout or "Attached" in r.stderr:
            print("Livetools attached on retry")
        else:
            print("WARNING: livetools attachment failed")


def build_and_deploy():
    """Build the proxy DLL and deploy to game directory."""
    proxy_dir = SCRIPT_DIR / "proxy"
    build_bat = proxy_dir / "build.bat"
    if not build_bat.exists():
        print(f"ERROR: build.bat not found: {build_bat}")
        sys.exit(1)

    print("=== Building proxy ===")
    r = subprocess.run(str(build_bat), capture_output=True, text=True,
                       shell=True, cwd=str(proxy_dir))
    print(r.stdout)
    if r.returncode != 0:
        print(f"BUILD FAILED:\n{r.stderr}")
        sys.exit(1)

    shutil.copy2(str(proxy_dir / "d3d9.dll"), str(GAME_DIR / "d3d9.dll"))
    shutil.copy2(str(proxy_dir / "proxy.ini"), str(GAME_DIR / "proxy.ini"))
    rtx_conf = SCRIPT_DIR / "rtx.conf"
    if rtx_conf.exists():
        shutil.copy2(str(rtx_conf), str(GAME_DIR / "rtx.conf"))
    print(f"Deployed proxy to {GAME_DIR.name}/")


def main():
    parser = argparse.ArgumentParser(
        description="Fast Peru launcher for Tomb Raider Legend")
    parser.add_argument("--attach", action="store_true",
                        help="Attach livetools after reaching Peru")
    parser.add_argument("--build", action="store_true",
                        help="Build and deploy proxy DLL before launch")
    args = parser.parse_args()

    if args.build:
        build_and_deploy()

    # Kill any existing instance
    subprocess.run(["taskkill", "/f", "/im", "trl.exe"],
                   capture_output=True)
    time.sleep(2)

    set_graphics_config()

    print("\n=== Launching Peru ===")
    hwnd = launch_game()
    navigate_to_peru(hwnd)

    if args.attach:
        attach_livetools()

    print("\n=== Peru is ready ===")
    if args.attach:
        print("Livetools attached -- use 'python -m livetools' to interact")
    print("Close the game when done, or Ctrl+C to detach this launcher")

    # Keep alive so the user can see the output
    try:
        while True:
            time.sleep(60)
    except KeyboardInterrupt:
        print("\nLauncher detached (game still running)")


if __name__ == "__main__":
    main()
