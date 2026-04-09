"""Shared project configuration — resolves paths from environment or defaults.

Environment variables (set in your shell or .env):
  TRL_GAME_DIR          — Tomb Raider Legend install directory
  TRL_NVIDIA_SCREENSHOTS — NVIDIA screenshot capture directory
  TRL_REMIX_DEBUG_DIR   — RTX Remix debug runtime directory
"""
from __future__ import annotations

import os
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent

# Game directory: env override or default (sibling of repo root)
GAME_DIR = Path(os.environ.get(
    "TRL_GAME_DIR",
    str(REPO_ROOT / "Tomb Raider Legend"),
))

# NVIDIA screenshot capture directory
# Default follows NVIDIA's convention: ~/Videos/NVIDIA/<game title>
_default_nvidia = Path(os.environ.get("USERPROFILE", os.path.expanduser("~"))) / "Videos" / "NVIDIA" / "Tomb Raider  Legend"
NVIDIA_SCREENSHOT_DIR = Path(os.environ.get(
    "TRL_NVIDIA_SCREENSHOTS",
    str(_default_nvidia),
))

# RTX Remix debug runtime (optional — only needed for runtime swapping)
_default_debug = Path(os.environ.get("USERPROFILE", os.path.expanduser("~"))) / "Downloads" / "remixdebug"
REMIX_DEBUG_RUNTIME = Path(os.environ.get(
    "TRL_REMIX_DEBUG_DIR",
    str(_default_debug),
))

# Derived paths
GAME_EXE = GAME_DIR / "trl.exe"
LAUNCHER = GAME_DIR / "NvRemixLauncher32.exe"
PROXY_LOG = GAME_DIR / "ffp_proxy.log"
