"""Game window input automation via SendInput with proper focus management.

Re-Volt and most older DX9/DirectInput games read raw device state — they
ignore WM_KEYDOWN/PostMessage entirely. SendInput is required, but the game
window must be in the foreground first.

Focus strategy: attach our thread to the game's input queue via
AttachThreadInput, then SetForegroundWindow. This bypasses the Windows
foreground-lock that normally blocks background processes from stealing focus.

Window lookup supports two modes:
  --exe    <exe_name>     find window by process exe name (recommended)
  --window <title_hint>   find window by title substring fallback

Usage (CLI):
    python -m livetools gamectl --exe revolt_xbox.exe info
    python -m livetools gamectl --exe revolt_xbox.exe key RETURN
    python -m livetools gamectl --exe revolt_xbox.exe keys "DOWN DOWN RETURN"
    python -m livetools gamectl --exe revolt_xbox.exe keys "RETURN WAIT:1000 RETURN" --delay-ms 0
    python -m livetools gamectl --exe revolt_xbox.exe click 400 300
    python -m livetools gamectl --exe revolt_xbox.exe macro --macro-file patches/revolt/macros.json navigate_menu
    python -m livetools gamectl --exe revolt_xbox.exe macros --macro-file patches/revolt/macros.json
    python -m livetools gamectl --exe game.exe record --name test_session --output macros.json

Usage (library):
    from livetools.gamectl import find_hwnd_by_exe, focus_hwnd, send_key, send_keys, record
    hwnd = find_hwnd_by_exe("revolt_xbox.exe")
    focus_hwnd(hwnd)
    send_key("RETURN")
    send_keys("DOWN DOWN RETURN", delay_ms=200)

Macro file format (JSON) — store at patches/<GameName>/macros.json:
    {
      "navigate_menu": {
        "description": "Navigate from title screen into a race",
        "steps": "RETURN WAIT:1000 DOWN DOWN RETURN WAIT:500 RETURN"
      }
    }

Token syntax for steps (playback):
    KEY_NAME          — keydown + keyup (50ms hold)
    WAIT:N            — pause N milliseconds
    HOLD:KEY_NAME:N   — hold key N ms before keyup
    CLICK:X,Y         — left click at client coordinates (x,y)
    RCLICK:X,Y        — right click at client coordinates (x,y)
    MOVETO:X,Y        — move cursor to client coordinates (x,y)
"""

from __future__ import annotations

import ctypes
import ctypes.wintypes as wt
import json
import time
from pathlib import Path

# ── Win32 constants ────────────────────────────────────────────────────────

INPUT_KEYBOARD        = 1
INPUT_MOUSE           = 0
KEYEVENTF_KEYUP       = 0x0002
MOUSEEVENTF_LEFTDOWN  = 0x0002
MOUSEEVENTF_LEFTUP    = 0x0004
MOUSEEVENTF_RIGHTDOWN = 0x0008
MOUSEEVENTF_RIGHTUP   = 0x0010
SW_RESTORE            = 9
SW_SHOW               = 5
GW_OWNER              = 4
TH32CS_SNAPPROCESS    = 0x00000002

# Low-level hook constants (for recording)
WH_KEYBOARD_LL       = 13
WH_MOUSE_LL          = 14
WM_KEYDOWN            = 0x0100
WM_KEYUP              = 0x0101
WM_SYSKEYDOWN         = 0x0104
WM_SYSKEYUP           = 0x0105
WM_LBUTTONDOWN        = 0x0201
WM_LBUTTONUP          = 0x0202
WM_RBUTTONDOWN        = 0x0204
WM_RBUTTONUP          = 0x0205
WM_MOUSEMOVE          = 0x0200

user32   = ctypes.windll.user32
kernel32 = ctypes.windll.kernel32

# Declare proper types for hook APIs (64-bit Python truncates HHOOK without this)
HHOOK = ctypes.c_void_p
user32.SetWindowsHookExW.argtypes = [ctypes.c_int, ctypes.c_void_p,
                                     wt.HINSTANCE, wt.DWORD]
user32.SetWindowsHookExW.restype = HHOOK
user32.UnhookWindowsHookEx.argtypes = [HHOOK]
user32.UnhookWindowsHookEx.restype = wt.BOOL
user32.CallNextHookEx.argtypes = [HHOOK, ctypes.c_int, wt.WPARAM, wt.LPARAM]
user32.CallNextHookEx.restype = ctypes.c_long
kernel32.GetModuleHandleW.argtypes = [wt.LPCWSTR]
kernel32.GetModuleHandleW.restype = wt.HMODULE
user32.GetForegroundWindow.argtypes = []
user32.GetForegroundWindow.restype = wt.HWND
user32.PeekMessageW.argtypes = [ctypes.POINTER(wt.MSG), wt.HWND,
                                wt.UINT, wt.UINT, wt.UINT]
user32.PeekMessageW.restype = wt.BOOL

# ── Virtual key map ────────────────────────────────────────────────────────

VK_MAP: dict[str, int] = {
    "RETURN": 0x0D, "ENTER": 0x0D,
    "ESCAPE": 0x1B, "ESC": 0x1B,
    "SPACE": 0x20,
    "UP": 0x26, "DOWN": 0x28, "LEFT": 0x25, "RIGHT": 0x27,
    "TAB": 0x09, "BACKSPACE": 0x08, "DELETE": 0x2E,
    "HOME": 0x24, "END": 0x23, "PAGEUP": 0x21, "PAGEDOWN": 0x22,
    "F1": 0x70, "F2": 0x71, "F3": 0x72,  "F4": 0x73,
    "F5": 0x74, "F6": 0x75, "F7": 0x76,  "F8": 0x77,
    "F9": 0x78, "F10": 0x79, "F11": 0x7A, "F12": 0x7B,
    "SHIFT": 0x10, "CTRL": 0x11, "ALT": 0x12,
    **{c: 0x41 + i for i, c in enumerate("ABCDEFGHIJKLMNOPQRSTUVWXYZ")},
    **{str(d): 0x30 + d for d in range(10)},
    "NUMPAD0": 0x60, "NUMPAD1": 0x61, "NUMPAD2": 0x62, "NUMPAD3": 0x63,
    "NUMPAD4": 0x64, "NUMPAD5": 0x65, "NUMPAD6": 0x66, "NUMPAD7": 0x67,
    "NUMPAD8": 0x68, "NUMPAD9": 0x69,
    "[": 0xDB, "]": 0xDD,
    ";": 0xBA, "'": 0xDE, ",": 0xBC, ".": 0xBE, "/": 0xBF,
    "-": 0xBD, "=": 0xBB, "\\": 0xDC, "`": 0xC0,
    "PRINTSCREEN": 0x2C,
}

# ── SendInput structures ───────────────────────────────────────────────────

class KEYBDINPUT(ctypes.Structure):
    _fields_ = [
        ("wVk",         wt.WORD),
        ("wScan",       wt.WORD),
        ("dwFlags",     wt.DWORD),
        ("time",        wt.DWORD),
        ("dwExtraInfo", ctypes.POINTER(ctypes.c_ulong)),
    ]

class MOUSEINPUT(ctypes.Structure):
    _fields_ = [
        ("dx",          wt.LONG),
        ("dy",          wt.LONG),
        ("mouseData",   wt.DWORD),
        ("dwFlags",     wt.DWORD),
        ("time",        wt.DWORD),
        ("dwExtraInfo", ctypes.POINTER(ctypes.c_ulong)),
    ]

class _INPUT_UNION(ctypes.Union):
    _fields_ = [("ki", KEYBDINPUT), ("mi", MOUSEINPUT)]

class INPUT(ctypes.Structure):
    _fields_ = [("type", wt.DWORD), ("union", _INPUT_UNION)]

WNDENUMPROC = ctypes.WINFUNCTYPE(wt.BOOL, wt.HWND, wt.LPARAM)

class PROCESSENTRY32(ctypes.Structure):
    _fields_ = [
        ("dwSize",              wt.DWORD),
        ("cntUsage",            wt.DWORD),
        ("th32ProcessID",       wt.DWORD),
        ("th32DefaultHeapID",   ctypes.POINTER(ctypes.c_ulong)),
        ("th32ModuleID",        wt.DWORD),
        ("cntThreads",          wt.DWORD),
        ("th32ParentProcessID", wt.DWORD),
        ("pcPriClassBase",      ctypes.c_long),
        ("dwFlags",             wt.DWORD),
        ("szExeFile",           ctypes.c_char * 260),
    ]


def _find_pid(exe_name: str) -> int | None:
    """Return the PID of the first process whose exe matches exe_name."""
    snap = kernel32.CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0)
    if snap == ctypes.c_void_p(-1).value:
        return None
    entry = PROCESSENTRY32()
    entry.dwSize = ctypes.sizeof(PROCESSENTRY32)
    try:
        if not kernel32.Process32First(snap, ctypes.byref(entry)):
            return None
        while True:
            name = entry.szExeFile.decode("utf-8", errors="replace")
            if name.lower() == exe_name.lower():
                return entry.th32ProcessID
            if not kernel32.Process32Next(snap, ctypes.byref(entry)):
                break
    finally:
        kernel32.CloseHandle(snap)
    return None

# ── Window lookup ──────────────────────────────────────────────────────────

WNDENUMPROC = ctypes.WINFUNCTYPE(wt.BOOL, wt.HWND, wt.LPARAM)


def find_hwnd_by_exe(exe_name: str) -> int | None:
    """Find the main visible window of a process by its exe filename.

    Args:
        exe_name: Process exe name, e.g. "revolt_xbox.exe"

    Returns:
        Window handle (int) or None if not found.
    """
    pid = _find_pid(exe_name)
    if pid is None:
        return None
    result: list[int] = []

    @WNDENUMPROC
    def _cb(hwnd: int, _: int) -> bool:
        if not user32.IsWindowVisible(hwnd):
            return True
        if user32.GetWindow(hwnd, GW_OWNER) != 0:
            return True
        proc_id = wt.DWORD(0)
        user32.GetWindowThreadProcessId(hwnd, ctypes.byref(proc_id))
        if proc_id.value == pid:
            result.append(hwnd)
        return True

    user32.EnumWindows(_cb, 0)
    return result[0] if result else None


def find_hwnd_by_title(title_hint: str) -> int | None:
    """Find the first visible top-level window whose title contains title_hint."""
    result: list[int] = []

    @WNDENUMPROC
    def _cb(hwnd: int, _: int) -> bool:
        if not user32.IsWindowVisible(hwnd):
            return True
        if user32.GetWindow(hwnd, GW_OWNER) != 0:
            return True
        buf = ctypes.create_unicode_buffer(256)
        user32.GetWindowTextW(hwnd, buf, 256)
        if buf.value and title_hint.lower() in buf.value.lower():
            result.append(hwnd)
        return True

    user32.EnumWindows(_cb, 0)
    return result[0] if result else None


def resolve_hwnd(exe: str | None, window: str | None) -> tuple[int | None, str]:
    """Resolve hwnd from --exe or --window, return (hwnd, error_msg)."""
    if exe:
        hwnd = find_hwnd_by_exe(exe)
        if not hwnd:
            return None, f"No window found for process '{exe}'"
        return hwnd, ""
    if window:
        hwnd = find_hwnd_by_title(window)
        if not hwnd:
            return None, f"No window found matching title '{window}'"
        return hwnd, ""
    return None, "Provide --exe <game.exe> or --window <title>"


def get_window_info(hwnd: int) -> dict:
    """Return title and process info for a hwnd."""
    buf = ctypes.create_unicode_buffer(256)
    user32.GetWindowTextW(hwnd, buf, 256)
    pid = wt.DWORD(0)
    tid = user32.GetWindowThreadProcessId(hwnd, ctypes.byref(pid))
    return {"hwnd": hwnd, "title": buf.value, "pid": pid.value, "tid": tid}

# ── Focus management ───────────────────────────────────────────────────────

def focus_hwnd(hwnd: int) -> bool:
    """Force hwnd to the foreground using AttachThreadInput.

    DirectInput games only process keys when their window is the foreground
    window. This attaches our thread to the game's input queue so
    SetForegroundWindow is not blocked by the Windows foreground lock.

    Returns True if the window is in the foreground after the call.
    """
    if user32.IsIconic(hwnd):
        user32.ShowWindow(hwnd, SW_RESTORE)
        time.sleep(0.3)

    my_tid  = kernel32.GetCurrentThreadId()
    fg_hwnd = user32.GetForegroundWindow()
    fg_tid  = user32.GetWindowThreadProcessId(fg_hwnd, None)

    if fg_tid and fg_tid != my_tid:
        user32.AttachThreadInput(my_tid, fg_tid, True)
        user32.BringWindowToTop(hwnd)
        user32.SetForegroundWindow(hwnd)
        user32.AttachThreadInput(my_tid, fg_tid, False)
    else:
        user32.SetForegroundWindow(hwnd)

    time.sleep(0.15)
    return user32.GetForegroundWindow() == hwnd

# ── SendInput keyboard ─────────────────────────────────────────────────────

def _make_key_input(vk: int, up: bool = False) -> INPUT:
    inp = INPUT()
    inp.type = INPUT_KEYBOARD
    inp.union.ki.wVk   = vk
    inp.union.ki.wScan = user32.MapVirtualKeyW(vk, 0)
    inp.union.ki.dwFlags = KEYEVENTF_KEYUP if up else 0
    inp.union.ki.time = 0
    inp.union.ki.dwExtraInfo = None
    return inp


def send_key(key_name: str, hold_ms: int = 50) -> dict:
    """Send a single key press via SendInput (game must be foreground).

    Args:
        key_name: Key name from VK_MAP (e.g. "RETURN", "UP", "A", "F5").
        hold_ms:  Delay between keydown and keyup in milliseconds.

    Returns:
        dict with ok, key, vk
    """
    vk = VK_MAP.get(key_name.upper())
    if vk is None:
        return {"ok": False, "error": f"Unknown key: '{key_name}'. "
                f"Valid: {', '.join(sorted(VK_MAP))}"}
    dn = (INPUT * 1)(_make_key_input(vk, up=False))
    user32.SendInput(1, dn, ctypes.sizeof(INPUT))
    time.sleep(hold_ms / 1000.0)
    up = (INPUT * 1)(_make_key_input(vk, up=True))
    user32.SendInput(1, up, ctypes.sizeof(INPUT))
    return {"ok": True, "key": key_name, "vk": hex(vk)}


def send_keys(hwnd: int, sequence: str, delay_ms: int = 200) -> dict:
    """Focus hwnd then send a space-separated key sequence via SendInput.

    Token syntax:
        KEY_NAME          — keydown + keyup
        WAIT:N            — pause N milliseconds
        HOLD:KEY_NAME:N   — hold key N ms before keyup
        CLICK:X,Y         — left click at client coordinates
        RCLICK:X,Y        — right click at client coordinates

    Args:
        hwnd:      Target window handle (will be focused before sending).
        sequence:  Space-separated token string.
        delay_ms:  Default inter-key delay in milliseconds.

    Returns:
        dict with ok, count, actions
    """
    focused = focus_hwnd(hwnd)
    if not focused:
        # Still try — some games accept input even if focus check is unreliable
        pass

    actions: list[dict] = []
    for token in sequence.strip().split():
        upper = token.upper()
        if upper.startswith("WAIT:"):
            ms = int(token.split(":")[1])
            time.sleep(ms / 1000.0)
            actions.append({"action": "wait", "ms": ms})
        elif upper.startswith("HOLD:"):
            parts = token.split(":")
            key = parts[1]
            ms  = int(parts[2]) if len(parts) > 2 else 500
            r   = send_key(key, hold_ms=ms)
            actions.append({**r, "action": "hold", "hold_ms": ms})
            time.sleep(delay_ms / 1000.0)
        elif upper.startswith("CLICK:"):
            coords = token.split(":")[1].split(",")
            x, y = int(coords[0]), int(coords[1])
            r = click_at(hwnd, x, y)
            actions.append({**r, "action": "click"})
            time.sleep(delay_ms / 1000.0)
        elif upper.startswith("RCLICK:"):
            coords = token.split(":")[1].split(",")
            x, y = int(coords[0]), int(coords[1])
            r = rclick_at(hwnd, x, y)
            actions.append({**r, "action": "rclick"})
            time.sleep(delay_ms / 1000.0)
        elif upper.startswith("MOVETO:"):
            coords = token.split(":")[1].split(",")
            x, y = int(coords[0]), int(coords[1])
            r = move_to(hwnd, x, y)
            actions.append({**r, "action": "move"})
        else:
            r = send_key(token)
            actions.append(r)
            time.sleep(delay_ms / 1000.0)
    return {"ok": True, "focused": focused, "count": len(actions), "actions": actions}

# ── Mouse input ────────────────────────────────────────────────────────────

def click_at(hwnd: int, x: int, y: int) -> dict:
    """Focus hwnd then left-click at client-area coordinates via SendInput.

    Args:
        hwnd: Target window handle.
        x, y: Client-area coordinates.

    Returns:
        dict with ok, screen_x, screen_y
    """
    focus_hwnd(hwnd)
    pt = wt.POINT(x, y)
    user32.ClientToScreen(hwnd, ctypes.byref(pt))
    user32.SetCursorPos(pt.x, pt.y)
    time.sleep(0.05)

    dn = INPUT(); dn.type = INPUT_MOUSE; dn.union.mi.dwFlags = MOUSEEVENTF_LEFTDOWN
    up = INPUT(); up.type = INPUT_MOUSE; up.union.mi.dwFlags = MOUSEEVENTF_LEFTUP
    arr = (INPUT * 2)(dn, up)
    user32.SendInput(2, arr, ctypes.sizeof(INPUT))
    return {"ok": True, "screen_x": pt.x, "screen_y": pt.y, "client_x": x, "client_y": y}


def move_to(hwnd: int, x: int, y: int) -> dict:
    """Move cursor to client-area coordinates without clicking."""
    pt = wt.POINT(x, y)
    user32.ClientToScreen(hwnd, ctypes.byref(pt))
    user32.SetCursorPos(pt.x, pt.y)
    return {"ok": True, "screen_x": pt.x, "screen_y": pt.y, "client_x": x, "client_y": y}


# ── Macro support ──────────────────────────────────────────────────────────

def load_macros(path: str | Path) -> dict[str, dict]:
    """Load a macro JSON file.

    Args:
        path: Path to JSON file mapping name -> {description, steps}.

    Returns:
        dict of macro definitions.
    """
    p = Path(path)
    if not p.exists():
        raise FileNotFoundError(f"Macro file not found: {p}")
    data = json.loads(p.read_text(encoding="utf-8"))
    if not isinstance(data, dict):
        raise ValueError("Macro file must be a JSON object")
    return data


def run_macro(hwnd: int, name: str, macros: dict[str, dict],
              delay_ms: int = 200) -> dict:
    """Focus hwnd and execute a named macro.

    Args:
        hwnd:      Target window handle.
        name:      Macro name key.
        macros:    Loaded macro definitions.
        delay_ms:  Inter-key delay in milliseconds.

    Returns:
        dict with ok, macro, steps_result
    """
    if name not in macros:
        return {"ok": False,
                "error": f"Macro '{name}' not found. "
                         f"Available: {', '.join(sorted(macros))}"}
    steps = macros[name].get("steps", "")
    if not steps:
        return {"ok": False, "error": f"Macro '{name}' has no steps"}
    result = send_keys(hwnd, steps, delay_ms=delay_ms)
    return {"ok": result["ok"], "macro": name, "steps_result": result}


def rclick_at(hwnd: int, x: int, y: int) -> dict:
    """Focus hwnd then right-click at client-area coordinates via SendInput."""
    focus_hwnd(hwnd)
    pt = wt.POINT(x, y)
    user32.ClientToScreen(hwnd, ctypes.byref(pt))
    user32.SetCursorPos(pt.x, pt.y)
    time.sleep(0.05)

    dn = INPUT(); dn.type = INPUT_MOUSE; dn.union.mi.dwFlags = MOUSEEVENTF_RIGHTDOWN
    up = INPUT(); up.type = INPUT_MOUSE; up.union.mi.dwFlags = MOUSEEVENTF_RIGHTUP
    arr = (INPUT * 2)(dn, up)
    user32.SendInput(2, arr, ctypes.sizeof(INPUT))
    return {"ok": True, "screen_x": pt.x, "screen_y": pt.y, "client_x": x, "client_y": y}


# ── Low-level hook structures (for recording) ────────────────────────────

class KBDLLHOOKSTRUCT(ctypes.Structure):
    _fields_ = [
        ("vkCode",      wt.DWORD),
        ("scanCode",    wt.DWORD),
        ("flags",       wt.DWORD),
        ("time",        wt.DWORD),
        ("dwExtraInfo", ctypes.POINTER(ctypes.c_ulong)),
    ]

class MSLLHOOKSTRUCT(ctypes.Structure):
    _fields_ = [
        ("pt",          wt.POINT),
        ("mouseData",   wt.DWORD),
        ("flags",       wt.DWORD),
        ("time",        wt.DWORD),
        ("dwExtraInfo", ctypes.POINTER(ctypes.c_ulong)),
    ]

HOOKPROC = ctypes.WINFUNCTYPE(ctypes.c_long, ctypes.c_int, wt.WPARAM,
                               wt.LPARAM)

# Reverse VK map: vk code -> canonical key name
_VK_TO_NAME: dict[int, str] = {}
for _name, _vk in VK_MAP.items():
    if _vk not in _VK_TO_NAME:
        _VK_TO_NAME[_vk] = _name


# ── Input recorder ───────────────────────────────────────────────────────

def record(hwnd: int, stop_key: str = "F12",
           print_fn=None) -> list[dict]:
    """Record keyboard and mouse input while the game window is focused.

    Installs WH_KEYBOARD_LL and WH_MOUSE_LL hooks. Runs a Win32 message
    pump until stop_key is pressed. Only records events when hwnd is the
    foreground window.

    Args:
        hwnd:      Game window handle (focus gate + coordinate conversion).
        stop_key:  Key name that stops recording (consumed, not recorded).
        print_fn:  Optional callback for status messages (default: print).

    Returns:
        List of event dicts with keys:
          {time_ms, type="keydown"|"keyup"|"lclick"|"rclick",
           key, vk, x, y}
    """
    out = print_fn or print
    stop_vk = VK_MAP.get(stop_key.upper())
    if stop_vk is None:
        raise ValueError(f"Unknown stop key: {stop_key}")

    events: list[dict] = []
    t0 = time.perf_counter_ns()
    running = [True]  # mutable flag for closure access

    def _ms() -> int:
        return int((time.perf_counter_ns() - t0) / 1_000_000)

    # Cast hwnd to int once for reliable comparison with GetForegroundWindow
    target_hwnd = int(hwnd)

    def _game_focused() -> bool:
        fg = user32.GetForegroundWindow()
        # HWND can come back as ctypes c_void_p or int — normalize both
        return int(fg or 0) == target_hwnd

    # ── keyboard hook callback ──
    @HOOKPROC
    def kb_hook(nCode: int, wParam: int, lParam: int) -> int:
        if nCode >= 0:
            info = ctypes.cast(lParam,
                               ctypes.POINTER(KBDLLHOOKSTRUCT)).contents
            vk = info.vkCode
            is_down = wParam in (WM_KEYDOWN, WM_SYSKEYDOWN)
            is_up   = wParam in (WM_KEYUP, WM_SYSKEYUP)

            # Stop key: always respond regardless of focus
            if vk == stop_vk and is_down:
                running[0] = False
                return 1  # block the stop key from reaching the game

            if _game_focused() and (is_down or is_up):
                name = _VK_TO_NAME.get(vk)
                if name:
                    events.append({
                        "time_ms": _ms(),
                        "type": "keydown" if is_down else "keyup",
                        "key": name,
                        "vk": vk,
                    })

        return user32.CallNextHookEx(None, nCode, wParam, lParam)

    # ── mouse hook callback ──
    # Movement coalescing: only record when cursor moves >= 5px from last point
    last_mouse = [0, 0]  # last recorded client x, y
    MOVE_THRESHOLD = 5   # pixels

    @HOOKPROC
    def mouse_hook(nCode: int, wParam: int, lParam: int) -> int:
        if nCode >= 0 and _game_focused():
            info = ctypes.cast(lParam,
                               ctypes.POINTER(MSLLHOOKSTRUCT)).contents

            # Convert screen coords to client coords
            pt = wt.POINT(info.pt.x, info.pt.y)
            user32.ScreenToClient(hwnd, ctypes.byref(pt))

            if wParam == WM_LBUTTONDOWN:
                etype = "lclick"
            elif wParam == WM_RBUTTONDOWN:
                etype = "rclick"
            elif wParam == WM_MOUSEMOVE:
                # Coalesce: skip if cursor barely moved
                dx = abs(pt.x - last_mouse[0])
                dy = abs(pt.y - last_mouse[1])
                if dx < MOVE_THRESHOLD and dy < MOVE_THRESHOLD:
                    return user32.CallNextHookEx(None, nCode, wParam, lParam)
                last_mouse[0] = pt.x
                last_mouse[1] = pt.y
                events.append({
                    "time_ms": _ms(),
                    "type": "move",
                    "x": pt.x,
                    "y": pt.y,
                })
                return user32.CallNextHookEx(None, nCode, wParam, lParam)
            else:
                return user32.CallNextHookEx(None, nCode, wParam, lParam)

            last_mouse[0] = pt.x
            last_mouse[1] = pt.y
            events.append({
                "time_ms": _ms(),
                "type": etype,
                "x": pt.x,
                "y": pt.y,
            })

        return user32.CallNextHookEx(None, nCode, wParam, lParam)

    # Install hooks
    hmod = kernel32.GetModuleHandleW(None)
    kb_hk = user32.SetWindowsHookExW(WH_KEYBOARD_LL, kb_hook, hmod, 0)
    mouse_hk = user32.SetWindowsHookExW(WH_MOUSE_LL, mouse_hook, hmod, 0)

    if not kb_hk or not mouse_hk:
        raise RuntimeError("Failed to install input hooks. "
                           "Run from a terminal with appropriate permissions.")

    out(f"Recording... press {stop_key} to stop.")

    # Message pump — required for low-level hooks to fire
    msg = wt.MSG()
    try:
        while running[0]:
            # PeekMessage with PM_REMOVE so we don't block forever
            while user32.PeekMessageW(ctypes.byref(msg), None, 0, 0, 1):
                user32.TranslateMessage(ctypes.byref(msg))
                user32.DispatchMessageW(ctypes.byref(msg))
            time.sleep(0.005)  # yield CPU between pump cycles
    finally:
        user32.UnhookWindowsHookEx(kb_hk)
        user32.UnhookWindowsHookEx(mouse_hk)

    duration_s = _ms() / 1000.0
    out(f"Recording stopped. {len(events)} raw events in {duration_s:.1f}s")
    return events


def events_to_macro(events: list[dict], min_wait_ms: int = 100,
                    hold_threshold_ms: int = 200) -> str:
    """Convert recorded events to a macro token string.

    Pairs keydown/keyup into single key presses or HOLD tokens. Inserts
    WAIT tokens for gaps >= min_wait_ms. Mouse clicks become CLICK/RCLICK.

    Args:
        events:             Raw event list from record().
        min_wait_ms:        Minimum gap to emit a WAIT token.
        hold_threshold_ms:  Keydown-to-keyup duration that triggers HOLD.

    Returns:
        Space-separated token string for send_keys().
    """
    # Pair keydown→keyup per key
    pending_keys: dict[str, int] = {}  # key_name -> keydown time_ms
    # (start_ms, end_ms, token) — end_ms is when the action finishes during replay
    merged: list[tuple[int, int, str]] = []

    for ev in events:
        t = ev["time_ms"]

        if ev["type"] == "keydown":
            key = ev["key"]
            if key not in pending_keys:
                pending_keys[key] = t

        elif ev["type"] == "keyup":
            key = ev["key"]
            if key in pending_keys:
                down_t = pending_keys.pop(key)
                hold_ms = t - down_t
                if hold_ms >= hold_threshold_ms:
                    hold_ms = max(50, round(hold_ms / 50) * 50)
                    merged.append((down_t, down_t + hold_ms,
                                   f"HOLD:{key}:{hold_ms}"))
                else:
                    # Short press: ~50ms replay time
                    merged.append((down_t, down_t + 50, key))

        elif ev["type"] == "lclick":
            merged.append((t, t + 50, f"CLICK:{ev['x']},{ev['y']}"))

        elif ev["type"] == "rclick":
            merged.append((t, t + 50, f"RCLICK:{ev['x']},{ev['y']}"))

        elif ev["type"] == "move":
            merged.append((t, t, f"MOVETO:{ev['x']},{ev['y']}"))

    # Flush any keys that were held down when recording stopped
    for key, down_t in pending_keys.items():
        merged.append((down_t, down_t + 50, key))

    merged.sort(key=lambda x: x[0])

    # Build token string with WAIT tokens for gaps
    tokens: list[str] = []
    cursor = merged[0][0] if merged else 0  # tracks replay timeline position

    for start_t, end_t, token in merged:
        gap = start_t - cursor
        if gap >= min_wait_ms:
            gap = max(50, round(gap / 50) * 50)
            tokens.append(f"WAIT:{gap}")
        tokens.append(token)
        cursor = end_t

    return " ".join(tokens)


def save_macro(path: Path, name: str, description: str,
               steps: str) -> dict[str, dict]:
    """Merge a new macro into a macros JSON file.

    Creates the file if it doesn't exist. Overwrites any existing macro
    with the same name.

    Args:
        path:        Path to macros JSON file.
        name:        Macro name key.
        description: Human-readable description.
        steps:       Token string (output of events_to_macro).

    Returns:
        The full macros dict after saving.
    """
    p = Path(path)
    if p.exists():
        macros = json.loads(p.read_text(encoding="utf-8"))
    else:
        macros = {}

    macros[name] = {"description": description, "steps": steps}
    p.parent.mkdir(parents=True, exist_ok=True)
    p.write_text(json.dumps(macros, indent=2, ensure_ascii=False) + "\n",
                 encoding="utf-8")
    return macros
