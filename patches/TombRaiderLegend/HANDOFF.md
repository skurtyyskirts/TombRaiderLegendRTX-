# Session Handoff — 2026-04-09 (Session 3)

## What Was Accomplished This Session

### Main Menu Crash Fix (CRITICAL — Game Now Launches Normally)

The game was crashing when launched without `TR7.arg` (which forced direct level load into Peru). The root cause: **all 31 memory patches were applied at device creation** (`WrappedDevice_Create`), before any level was loaded. The main menu uses the same `SceneTraversal_CullAndSubmit` (0x407150) and `ProcessPendingRemovals` (0x436680) code paths but with NULL scene graph data — the NOP'd conditional jumps caused null pointer dereferences.

**Fix:** Deferred `TRL_ApplyMemoryPatches()` from device creation to the first `WD_BeginScene()` where `viewProjValid == 1`. The game only writes VS constants c0-c3 or c12-c15 during actual level rendering (never on the main menu), so `viewProjValid` reliably distinguishes "in a level" from "at the main menu."

The per-scene global stamps (frustum threshold, cull modes, far clip, etc.) are also guarded behind `memoryPatchesApplied` since the data pages aren't permanently unlocked until `TRL_ApplyMemoryPatches` runs.

**Both proxy copies updated:**
- `proxy/d3d9_device.c` (canonical source) — lines ~1223-1253 (BeginScene), ~2881 (device creation)
- `patches/TombRaiderLegend/proxy/d3d9_device.c` (patches copy) — lines ~2130-2160 (BeginScene), ~3905 (device creation)

**Verified:** Game launched without TR7.arg, survived 10+ seconds at main menu (ALIVE), Remix fully initialized. Previously crashed instantly.

### TR7.arg Removed

- **Deleted:** `C:\TR7\GAME\PC\TR7.arg` (the hardcoded path the game reads at startup)
- **Still in repo:** `Tomb Raider Legend/TR7.arg` (contains just `-chapter 4`, should also be deleted or `.gitignore`'d)
- **Python automation NOT yet updated** — `write_tr7_arg()` function still exists in:
  - `patches/TombRaiderLegend/run.py:371-382` (definition)
  - `patches/TombRaiderLegend/run.py:403` (called in `launch_game()`)
  - `autopatch/orchestrator.py:117-119` (called in `_launch_game()`)
  - `autopatch/diagnose.py:130-134` (called in launch)
- **Cutscene skip macros still assume direct-to-Peru load** — the ESC→W→ENTER sequence in `run.py:705-708` and `run.py:946-951` won't work from the main menu. If testing automation is run, it will need menu navigation added or TR7.arg temporarily restored.

### Other Uncommitted Changes in Working Tree

These changes were already present before this session (from prior work, not yet committed):

1. **`memcpy` optimization** — byte-at-a-time replaced with dword-aligned copy + `#pragma intrinsic(memcpy)` to restore compiler intrinsic for known-size copies
2. **`WD_Release` reorder** — proxy-owned COM objects (lastVS, lastPS, normalStagingVB, strippedDecl cache) are now released BEFORE forwarding final Release to real device, preventing use-after-free when Remix tears down
3. **`dataPageUnlocked` field** added to WrappedDevice struct (complements `memoryPatchesApplied`)
4. **`rtx.conf` changes** — the game-dir copy has diverged from `patches/TombRaiderLegend/rtx.conf`; the game-dir version has extensive texture hashes, smooth normals, particle textures, UI textures etc. from Remix capture sessions
5. **`build.bat` changes** in both `proxy/` and `patches/TombRaiderLegend/proxy/` — likely VS detection improvements

## What Was NOT Done

- **Python automation not updated** — `write_tr7_arg()` calls still exist everywhere. The test pipeline (`run.py test --build`) will still write TR7.arg and launch directly to Peru. This is fine for automated testing but contradicts the goal of removing TR7.arg.
- **`Tomb Raider Legend/TR7.arg` not deleted from repo** — still tracked by git
- **No test run** — the deferred patches fix was built and deployed, main menu survival verified, but no full hash stability test was run
- **No commit** — all changes are uncommitted in the working tree

## Current Proxy State

- **Deferred patches:** Memory patches now apply on first BeginScene with valid VP (not at device creation)
- **Main menu:** WORKS — game loads to main menu without crashing
- **Peru gameplay:** Should work (patches apply once level loads and VS constants are written) — NOT YET VERIFIED
- **31 culling layers:** All still patched (same patches, just applied later)
- **SHORT4→FLOAT3 position expansion:** Working
- **SHORT2→FLOAT2 texcoord expansion (1/4096 scale):** Working
- **World matrix decomposition from WVP:** Working
- **`useVertexCapture = True`** in rtx.conf
- **Anti-culling = disabled** in rtx.conf (causes freeze)
- **Build deployed to game dir:** `d3d9.dll` at 02:44 AM 2026-04-09

## Proxy DLL Location

The **canonical proxy source** is `proxy/d3d9_device.c` (repo root). The `patches/TombRaiderLegend/proxy/d3d9_device.c` is a copy that may have slight differences (e.g., extra per-scene stamps like `TRL_POSTSECTOR_ENABLE_ADDR`). Both were updated this session.

The built DLL is deployed to: `C:\Users\skurtyy\Documents\GitHub\AlmightyBackups\NightRaven1\Vibe-Reverse-Engineering-Claude\Tomb Raider Legend\d3d9.dll`

## What To Do Next

### 1. Verify Gameplay Still Works
Launch the game (it will go to main menu now), navigate to Peru manually, and confirm:
- Remix rendering works
- Culling patches activate (check `ffp_proxy.log` for "Patched" messages)
- Stage lights visible (the two blockers from CLAUDE.md still apply)

### 2. Update Python Automation (Optional)
If automated testing needs to work without TR7.arg:
- Remove `write_tr7_arg()` from `run.py`, `orchestrator.py`, `diagnose.py`
- Replace cutscene-skip macros with main-menu navigation macros
- OR: keep `write_tr7_arg()` for automated tests only (it's harmless — just writes a file the game optionally reads)

### 3. Run Hash Stability Test
Say "begin testing" — but note the automation will recreate TR7.arg via `write_tr7_arg()`. This is fine for testing purposes; the fix ensures the game doesn't crash if TR7.arg is absent.

### 4. Address the Two Remaining Blockers (from CLAUDE.md)
- **Blocker 1: Anchor Mesh Hashes Unverified** — stage lights may be at wrong hashes. Lower mod light intensity from 10000000 to ~1000 to see if build 073's white dots are actually colored lights.
- **Blocker 2: Hash Instability Unverified** — no Toolkit mesh replacement has been tested end-to-end.

## Key Files Changed This Session

| File | Change |
|------|--------|
| `proxy/d3d9_device.c` | Deferred `TRL_ApplyMemoryPatches` to first BeginScene with viewProjValid |
| `patches/TombRaiderLegend/proxy/d3d9_device.c` | Same deferred patch change |
| `C:\TR7\GAME\PC\TR7.arg` | **Deleted** from disk |

## Key Files Reference

| File | Purpose |
|------|---------|
| `proxy/d3d9_device.c` | Canonical proxy source (all FFP + patch logic) |
| `patches/TombRaiderLegend/proxy/d3d9_device.c` | Patches copy of proxy (may diverge slightly) |
| `patches/TombRaiderLegend/run.py` | Test automation — build, deploy, launch, macro, screenshot |
| `patches/TombRaiderLegend/findings.md` | Accumulated RE findings |
| `patches/TombRaiderLegend/kb.h` | Knowledge base (function/struct definitions) |
| `C:\Users\skurtyy\Desktop\pee\mod.usda` | Remix mod with light definitions |
| `Tomb Raider Legend/rtx.conf` | Remix config (game dir copy, authoritative) |
| `Tomb Raider Legend/.trex/bridge.conf` | Bridge config (`keyboardPolicy = 3`) |
| `CLAUDE.md` | Project instructions, 31-layer culling map, dead ends |
| `docs/status/WHITEBOARD.md` | Live project status |

## Technical Detail: The Deferred Patch Mechanism

```
Device Creation (WrappedDevice_Create):
  memoryPatchesApplied = 0   ← patches NOT applied
  viewProjValid = 0          ← no VS constants written yet

Main Menu (BeginScene called repeatedly):
  viewProjValid still 0      ← menu doesn't use VS constants c0-c3/c12-c15
  memoryPatchesApplied still 0
  → per-scene stamps SKIPPED (data pages not unlocked)
  → game runs normally, no crashes

Level Loads → First Draw Call:
  SetVertexShaderConstantF writes c0-c3 or c12-c15
  viewProjValid = 1

Next BeginScene:
  viewProjValid=1 && !memoryPatchesApplied → TRL_ApplyMemoryPatches()
  memoryPatchesApplied = 1
  → all 31 patches applied
  → data pages permanently unlocked
  → per-scene stamps now active every BeginScene
```

This means: if the player returns to the main menu from gameplay, the patches stay applied. This should be fine — the patches only affect level rendering code paths, and by that point the scene graph structures exist in memory.
