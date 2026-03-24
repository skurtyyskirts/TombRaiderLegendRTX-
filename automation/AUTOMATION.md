# TRL RTX Remix — Autonomous Test Automation

Automated build-test-analyze workflow for the Tomb Raider Legend FFP proxy with RTX Remix. Builds the proxy, launches the game, replays a recorded test session, captures screenshots and diagnostics, then closes the game.

## Prerequisites

- Visual Studio with C++ x86 build tools (auto-detected via `vswhere`)
- Python 3.10+ with packages from `requirements.txt`
- Game installed at `Tomb Raider Legend/` in the repo root
- `NvRemixLauncher32.exe` + `d3d9_remix.dll` in the game directory
- NVIDIA GeForce Experience or Shadowplay for `]` key screenshots

## Quick Start

```bash
# Record a new test session (launches game, records your inputs)
python patches/TombRaiderLegend/run.py record

# Run the full automation (build + deploy + launch + replay + collect)
python patches/TombRaiderLegend/run.py test --build

# Run without rebuilding (just launch + replay + collect)
python patches/TombRaiderLegend/run.py test
```

## How It Works

### Recording (`run.py record`)

1. Kills any existing `trl.exe`
2. Launches game via `NvRemixLauncher32.exe trl.exe`
3. Auto-dismisses the TRL setup dialog if it appears (clicks Ok)
4. Waits 20 seconds for the game to fully initialize (D3D9 + Remix + shaders)
5. Starts recording all keyboard, mouse clicks, and mouse movements
6. You play through your test routine manually
7. Press **F12** to stop recording
8. Saves as `test_session` macro in `patches/TombRaiderLegend/macros.json`

### Testing (`run.py test --build`)

1. **Build** — Compiles proxy DLL via `build.bat` (MSVC x86, no CRT)
2. **Deploy** — Copies `d3d9.dll` + `proxy.ini` to game directory
3. **Graphics config** — Sets TRL registry to lowest graphics (no shadows, no effects, no AA)
4. **Kill** — Terminates any existing `trl.exe`
5. **Launch** — Starts game via `NvRemixLauncher32.exe`
6. **Setup dialog** — Auto-detects and dismisses the TRL setup dialog (Win32 `BM_CLICK` on Ok button)
7. **Wait** — 20 seconds for game to fully load without touching focus
8. **Replay** — Sends the recorded `test_session` macro via `SendInput`
9. **Proxy log** — Waits up to 70 seconds for `ffp_proxy.log` to appear (50s delay in proxy)
10. **Close game** — `taskkill /f /im trl.exe`
11. **Screenshots** — Collects all NVIDIA screenshots from the last 2 minutes
12. **Done** — Proxy log + screenshots ready for analysis

## Key Files

| File | Purpose |
|------|---------|
| `patches/TombRaiderLegend/run.py` | Test orchestrator (record/test modes) |
| `patches/TombRaiderLegend/macros.json` | Recorded test macros |
| `patches/TombRaiderLegend/proxy/` | Proxy source (d3d9_device.c, build.bat, etc.) |
| `patches/TombRaiderLegend/proxy/proxy.ini` | Proxy runtime config |
| `patches/TombRaiderLegend/ffp_proxy.log` | Collected proxy diagnostics |
| `patches/TombRaiderLegend/screenshots/` | Test screenshots from NVIDIA capture |
| `Tomb Raider Legend/rtx.conf` | RTX Remix configuration |
| `livetools/gamectl.py` | Input automation engine (SendInput + Win32 hooks) |

## Input Recorder (`gamectl.py`)

The recorder uses Win32 low-level hooks (`WH_KEYBOARD_LL` + `WH_MOUSE_LL`) to capture input:

- **Keyboard**: All key presses with timing. Holds > 200ms become `HOLD:KEY:duration`
- **Mouse clicks**: Left/right clicks as `CLICK:X,Y` / `RCLICK:X,Y` (client coordinates)
- **Mouse movement**: Coalesced at 5px threshold as `MOVETO:X,Y`
- **Focus gate**: Only records when the game window is focused
- **Stop key**: F12 (consumed, not passed to game)

### Macro Token Syntax

```
KEY_NAME          — single key press (50ms hold)
WAIT:N            — pause N milliseconds
HOLD:KEY:N        — hold key for N ms
CLICK:X,Y         — left click at client coords
RCLICK:X,Y        — right click at client coords
MOVETO:X,Y        — move cursor to client coords
```

### Screenshot Triggers

Press `]` during gameplay to trigger an NVIDIA screenshot. Screenshots are saved to:
```
C:\Users\<user>\Videos\NVIDIA\Tomb Raider  Legend\
```

The automation collects all screenshots from the last 2 minutes after each test run.

## RTX Remix Configuration

Key `rtx.conf` settings for the automation:

```ini
rtx.remixMenuKeyBinds = X          # Open Remix dev menu with just X (no Alt)
rtx.geometryAssetHashRuleString = "indices,texcoords,geometrydescriptor"
rtx.useVertexCapture = True
rtx.fusedWorldViewMode = 0
rtx.zUp = True
```

## TRL Setup Dialog Handling

When the game detects a new `d3d9.dll`, it shows a Win32 setup dialog. The automation:

1. Detects the dialog by window class `#32770` and title containing "Setup"
2. Finds the "Ok" button child control
3. Sends `BM_CLICK` message to dismiss it
4. Graphics settings are pre-configured via registry:
   - `HKCU\Software\Crystal Dynamics\Tomb Raider: Legend\Graphics`
   - Fullscreen enabled, all effects disabled, lowest settings

## Proxy Log Analysis

The `ffp_proxy.log` contains scene summaries every 120 frames:

```
== SCENE 120
  total=1440        # total draw calls this batch
  processed=1440    # draws that went through transform pipeline
  skippedQuad=0     # screen-space quads skipped
  passthrough=0     # draws passed through without FFP conversion
  xformBlocked=0    # external SetTransform calls blocked
  vpValid=1         # View/Projection matrices valid (1=yes)
```

**Stability criteria:**
- `vpValid=1` in all frames
- `passthrough=0` (100% of draws processed)
- No crash during test session

## Iteration Workflow

For continuous hash stability testing:

1. Modify proxy source (`d3d9_device.c`)
2. Run `python patches/TombRaiderLegend/run.py test --build`
3. Check proxy log metrics
4. Review screenshots for hash stability (Geometry Hash debug view)
5. Commit and push results
6. Repeat until hashes are stable
