"""Screen capture via Win32 GDI with NVIDIA Shadowplay fallback.

GDI capture is fast (~50ms) and works for menus/windowed mode.
NVIDIA capture (pressing ']') is slower (~1-2s) but guaranteed to
capture RTX Remix rendered frames in fullscreen exclusive mode.

Platform: Windows only. Importing on other platforms raises a clear error
at function call time, not at import time, so the rest of the package
stays importable for testing and development.
"""
from __future__ import annotations

import io
import sys
import time
from pathlib import Path

from PIL import Image

_IS_WINDOWS = sys.platform == "win32"

if _IS_WINDOWS:
    import ctypes
    import ctypes.wintypes as wt
    user32 = ctypes.windll.user32
    gdi32 = ctypes.windll.gdi32
else:
    ctypes = None  # type: ignore[assignment]
    wt = None  # type: ignore[assignment]
    user32 = None
    gdi32 = None

SRCCOPY = 0x00CC0020
DIB_RGB_COLORS = 0
BI_RGB = 0

# Defer config import so the module stays importable without config.py
NVIDIA_SCREENSHOT_DIR: Path | None = None

def _get_nvidia_dir() -> Path:
    global NVIDIA_SCREENSHOT_DIR
    if NVIDIA_SCREENSHOT_DIR is None:
        sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
        from config import NVIDIA_SCREENSHOT_DIR as _d
        NVIDIA_SCREENSHOT_DIR = _d
    return NVIDIA_SCREENSHOT_DIR


def _require_windows(fn_name: str) -> None:
    if not _IS_WINDOWS:
        raise RuntimeError(f"{fn_name}() requires Windows (current platform: {sys.platform})")


if _IS_WINDOWS:
    class BITMAPINFOHEADER(ctypes.Structure):
        _fields_ = [
            ("biSize", wt.DWORD),
            ("biWidth", wt.LONG),
            ("biHeight", wt.LONG),
            ("biPlanes", wt.WORD),
            ("biBitCount", wt.WORD),
            ("biCompression", wt.DWORD),
            ("biSizeImage", wt.DWORD),
            ("biXPelsPerMeter", wt.LONG),
            ("biYPelsPerMeter", wt.LONG),
            ("biClrUsed", wt.DWORD),
            ("biClrImportant", wt.DWORD),
        ]


    class BITMAPINFO(ctypes.Structure):
        _fields_ = [
            ("bmiHeader", BITMAPINFOHEADER),
            ("bmiColors", wt.DWORD * 3),
        ]


def capture_window_gdi(hwnd: int) -> Image.Image | None:
    """Capture a window's client area via GDI BitBlt.

    Returns a PIL Image, or None if the capture produced an all-black frame
    (common with fullscreen exclusive D3D9).
    """
    _require_windows("capture_window_gdi")
    rect = wt.RECT()
    user32.GetClientRect(hwnd, ctypes.byref(rect))
    w = rect.right - rect.left
    h = rect.bottom - rect.top
    if w <= 0 or h <= 0:
        return None

    wnd_dc = user32.GetDC(hwnd)
    mem_dc = gdi32.CreateCompatibleDC(wnd_dc)
    bitmap = gdi32.CreateCompatibleBitmap(wnd_dc, w, h)
    gdi32.SelectObject(mem_dc, bitmap)

    # PrintWindow with PW_CLIENTONLY|PW_RENDERFULLCONTENT for D3D windows
    PW_CLIENTONLY = 0x01
    PW_RENDERFULLCONTENT = 0x02
    user32.PrintWindow(hwnd, mem_dc, PW_CLIENTONLY | PW_RENDERFULLCONTENT)

    bmi = BITMAPINFO()
    bmi.bmiHeader.biSize = ctypes.sizeof(BITMAPINFOHEADER)
    bmi.bmiHeader.biWidth = w
    bmi.bmiHeader.biHeight = -h  # top-down
    bmi.bmiHeader.biPlanes = 1
    bmi.bmiHeader.biBitCount = 32
    bmi.bmiHeader.biCompression = BI_RGB

    buf = ctypes.create_string_buffer(w * h * 4)
    gdi32.GetDIBits(mem_dc, bitmap, 0, h, buf, ctypes.byref(bmi), DIB_RGB_COLORS)

    gdi32.DeleteObject(bitmap)
    gdi32.DeleteDC(mem_dc)
    user32.ReleaseDC(hwnd, wnd_dc)

    img = Image.frombuffer("RGBA", (w, h), buf, "raw", "BGRA", 0, 1)

    # Black-frame detection: if the mean brightness is < 2, it's probably
    # a failed capture (fullscreen exclusive returns black).
    import numpy as np
    arr = np.array(img)
    if arr[:, :, :3].mean() < 2.0:
        return None

    return img.convert("RGB")


def capture_nvidia(hwnd: int) -> Image.Image | None:
    """Capture a frame by pressing the NVIDIA Shadowplay hotkey.

    Presses ']', waits for the screenshot file to appear in the NVIDIA
    capture directory, reads it, and returns as a PIL Image.
    """
    _require_windows("capture_nvidia")
    from livetools.gamectl import send_key, focus_hwnd

    nvidia_dir = _get_nvidia_dir()
    focus_hwnd(hwnd)
    before_ts = time.time()
    send_key("]", hold_ms=50)

    # Wait for new screenshot file (up to 5s)
    for _ in range(50):
        time.sleep(0.1)
        if not nvidia_dir.exists():
            continue
        candidates = [
            f for f in nvidia_dir.iterdir()
            if f.suffix.lower() in (".png", ".jpg", ".bmp")
            and f.stat().st_mtime > before_ts
        ]
        if candidates:
            newest = max(candidates, key=lambda f: f.stat().st_mtime)
            time.sleep(0.3)  # let file finish writing
            return Image.open(newest).convert("RGB")

    return None


def capture(hwnd: int, prefer_nvidia: bool = False) -> Image.Image | None:
    """Capture the game screen, with automatic fallback.

    Args:
        hwnd: Game window handle.
        prefer_nvidia: If True, skip GDI and go straight to NVIDIA capture.
            Use this when you need guaranteed Remix-rendered frames.

    Returns:
        PIL Image or None if all capture methods failed.
    """
    if not prefer_nvidia:
        img = capture_window_gdi(hwnd)
        if img is not None:
            return img

    return capture_nvidia(hwnd)


def image_to_bytes(img: Image.Image, max_size: int = 1280) -> bytes:
    """Resize and compress an image for API submission.

    Downscales to max_size on the longest edge and compresses to JPEG
    to keep API costs reasonable (~100-200KB per image).
    """
    w, h = img.size
    if max(w, h) > max_size:
        scale = max_size / max(w, h)
        img = img.resize((int(w * scale), int(h * scale)), Image.LANCZOS)

    buf = io.BytesIO()
    img.save(buf, format="JPEG", quality=80)
    return buf.getvalue()
