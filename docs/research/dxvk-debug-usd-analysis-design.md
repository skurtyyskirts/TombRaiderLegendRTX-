# DXVK Debug Env Vars + USD Capture Analysis

## Overview

Two additions to the TRL RTX Remix testing pipeline:
1. DXVK debug environment variables for shader/runtime diagnostics
2. Lightweight USD capture analysis for hash stability verification

Both serve the core mission: stabilize geometry hashes so Remix-placed content persists.

## Feature 1: DXVK Debug Env Vars

### What

Set `DXVK_LOG_LEVEL=debug` and `DXVK_SHADER_DUMP_PATH` when launching TRL so Remix's DXVK layer produces diagnostic output.

### Where

`patches/TombRaiderLegend/run.py` — `launch_game()` function.

### Behavior

- New `--dxvk-debug` flag on `run.py test` (default off — debug logging is verbose)
- When enabled, sets environment variables on the `NvRemixLauncher32.exe` subprocess:
  - `DXVK_LOG_LEVEL=debug` — verbose DXVK runtime log
  - `DXVK_SHADER_DUMP_PATH=<game_dir>/dxvk_shaders` — dumps compiled shaders
- After each test phase, collects:
  - `<game_dir>/d3d9.log` (DXVK log) → `patches/TombRaiderLegend/dxvk_debug.log`
  - `<game_dir>/dxvk_shaders/` summary (file count, total size)
- Shader dump directory is created automatically if it doesn't exist
- When flag is off, no env vars are set (current behavior, zero overhead)

### Files Modified

- `patches/TombRaiderLegend/run.py` — add `--dxvk-debug` flag, env var injection, artifact collection

## Feature 2: USD Capture Analysis

### What

A standalone Python script that analyzes RTX Remix USD captures by comparing mesh hash sets across captures — no `pxr`/OpenUSD dependency required.

### Where

New file: `patches/TombRaiderLegend/usd_analyze.py`

### How It Works

Remix captures are binary USD (`PXR-USDC`) files. Each capture references meshes by hash-based paths like `meshes/mesh_006E39C27FD9359B.usd`. The shared `meshes/` directory accumulates across all captures.

To determine which meshes belong to which capture, the script scans the binary `.usd` file for embedded `mesh_` string references and extracts the 16-character hex hashes. No USD parsing library needed — just binary string extraction.

### Subcommands

**`list <capture.usd>`**
- Extract all mesh hashes referenced by this capture
- Output: sorted list of hashes with count

**`diff <capture_A.usd> <capture_B.usd>`**
- Compare mesh sets between two captures
- Output: added hashes, removed hashes, stable (shared) hashes
- Useful for: "did moving Lara cause geometry to disappear from Remix's view?"

**`stability [--captures-dir DIR]`**
- Analyze all `.usd` captures in the directory
- Report: hashes present in ALL captures (stable), hashes in SOME (transient), hashes in ONE (unique)
- This is the key diagnostic: transient hashes = culled geometry = Remix-placed content disappearing

**`summary <capture.usd>`**
- Mesh count, material count (from `materials/` references), texture count
- Capture file size
- Timestamp from filename

### Output Format

Plain text, designed to be copy-pasted into build SUMMARY.md files:

```
=== Capture Stability Report ===
Captures analyzed: 3
Total unique meshes: 2116
Stable (in all 3):  1847  (87.3%)
Transient:           209  (9.9%)
Unique to one:        60  (2.8%)

Top transient hashes (appeared/disappeared across captures):
  mesh_006E39C27FD9359B  in 2/3 captures
  mesh_009E4294B2386B0D  in 1/3 captures
  ...
```

### Dependencies

Python stdlib only (pathlib, re, struct, argparse). No pip install required.

### Files Created

- `patches/TombRaiderLegend/usd_analyze.py` — standalone CLI tool

## Integration with Existing Pipeline

- `usd_analyze.py` is a standalone tool, not integrated into `run.py` test automation (captures are manual via Alt+X in Remix toolkit)
- The `--dxvk-debug` flag is opt-in so normal test runs aren't slowed
- Both tools' output can be included in build SUMMARY.md files under a new "Remix Diagnostics" section

## What This Does NOT Do

- Does not parse USD vertex data (would need `pxr` library)
- Does not compute hashes independently (Remix owns the hash algorithm)
- Does not automate USD captures (those are manual Alt+X triggers)
- Does not replace the existing hash debug view test (that remains the primary stability check)
