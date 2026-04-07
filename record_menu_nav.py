"""Launch TRL and record menu navigation keypresses.

Press F12 when you've reached the Peru level to stop recording.
The recorded macro is saved as 'menu_nav' to both:
  - patches/TombRaiderLegend/macros.json
  - autopatch/macros.json
"""
import subprocess
import sys
import time
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent
GAME_DIR = REPO_ROOT / "Tomb Raider Legend"
LAUNCHER = GAME_DIR / "NvRemixLauncher32.exe"
GAME_EXE = GAME_DIR / "trl.exe"

MACRO_FILES = [
    REPO_ROOT / "patches" / "TombRaiderLegend" / "macros.json",
    REPO_ROOT / "autopatch" / "macros.json",
]

sys.path.insert(0, str(REPO_ROOT))
from livetools.gamectl import find_hwnd_by_exe, get_window_info
from tools.gamectl import record, events_to_macro, save_macro
from patches.TombRaiderLegend.run import set_graphics_config, dismiss_setup_dialog


def main():
    # Kill any running instance
    subprocess.run(["taskkill", "/f", "/im", "trl.exe"], capture_output=True)
    time.sleep(2)

    # Set registry to bypass/configure setup dialog
    set_graphics_config()

    print(f"Launching: {LAUNCHER.name} {GAME_EXE.name}")
    subprocess.Popen([str(LAUNCHER), str(GAME_EXE)], cwd=str(GAME_DIR))

    print("Waiting for game window...")
    hwnd = None
    setup_dismissed = False
    for i in range(90):
        if not setup_dismissed and dismiss_setup_dialog():
            setup_dismissed = True
            print("  Setup dialog dismissed.")
            time.sleep(3)
            continue
        hwnd = find_hwnd_by_exe("trl.exe")
        if hwnd:
            info = get_window_info(hwnd)
            if "Setup" not in info["title"]:
                print(f"  Found: {info['title']} (hwnd={hex(hwnd)})")
                break
            else:
                dismiss_setup_dialog()
                hwnd = None
        time.sleep(1)
        if i % 10 == 9:
            print(f"  ...{i+1}s elapsed")

    if not hwnd:
        print("ERROR: Game window not found after 90s")
        sys.exit(1)

    print()
    print("=" * 60)
    print("  RECORDING STARTED")
    print("  Navigate to Peru level, skip the cutscene, then press F12.")
    print("=" * 60)
    print()

    events = record(hwnd, stop_key="F12")

    if not events:
        print("No events recorded.")
        sys.exit(1)

    macro_str = events_to_macro(events)
    print(f"\nRecorded macro ({len(events)} events):")
    print(f"  {macro_str}")

    for mf in MACRO_FILES:
        save_macro(mf, "menu_nav",
                   "Navigate from title screen to Peru level (user-recorded)",
                   macro_str)
        print(f"Saved to {mf}")

    print("\nDone. The menu_nav macro has been updated in both macro files.")


if __name__ == "__main__":
    main()
