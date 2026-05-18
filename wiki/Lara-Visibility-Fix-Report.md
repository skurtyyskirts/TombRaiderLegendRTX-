# Tomb Raider Legend — Lara Visibility Fix Report

**Date:** 2026-03-22
**Working Build:** `patches/TombRaiderLegend/backups/2026-03-22_working-lara-visible/`
**Symptom:** Lara invisible when moving in-game; UI missing in-game; menu worked correctly.
**Status:** FIXED — Lara visible, UI visible, world geometry correct, menu functional.

---

## Table of Contents

1. [Architecture Overview](#1-architecture-overview)
2. [The Bug Hunt — Step by Step](#2-the-bug-hunt--step-by-step)
3. [Root Causes Found](#3-root-causes-found)
4. [Fixes Applied](#4-fixes-applied)
5. [Technical Definitions](#5-technical-definitions)
6. [File Manifest](#6-file-manifest)
7. [How to Reproduce the Build](#7-how-to-reproduce-the-build)
8. [Lessons Learned](#8-lessons-learned)

---

## 1. Architecture Overview

### DLL Chain Loading Stack

```
trl.exe (32-bit, D3D8 API)
  → dxwrapper.dll (D3D8-to-D3D9 translation, D3d8to9=1)
    → d3d9.dll (THIS PROXY — our FFP conversion layer)
      → d3d9_remix.dll (NVIDIA RTX Remix runtime)
        → system d3d9.dll (real GPU driver)
```

### What the Proxy Does

The proxy sits between dxwrapper and RTX Remix. Its job:

1. **Intercept draw calls** (DrawPrimitive, DrawIndexedPrimitive, DrawPrimitiveUP, DrawIndexedPrimitiveUP)
2. **Track VS/PS constant registers** to know the current WorldViewProjection matrix (c0-c3)
3. **Decompose transforms**: Extract separate World, View, and Projection matrices from the combined WVP, then call `SetTransform()` so Remix sees decomposed transforms for path tracing
4. **Keep shaders active** (passthrough mode) — TRL uses SHORT4 vertex positions that require the game's vertex shaders to decode. Remix captures post-shader positions via `rtx.useVertexCapture=True`

### Key Files

| File | Purpose |
|------|---------|
| `d3d9_device.c` | Core proxy — wraps IDirect3DDevice9 with ~15 intercepted methods |
| `d3d9_main.c` | DLL entry, Remix chain-loading, logging functions |
| `d3d9_wrapper.c` | Wraps IDirect3D9 to intercept CreateDevice |
| `d3d9.def` | DLL export definition (Direct3DCreate9) |
| `proxy.ini` | Runtime config (Remix DLL name, AlbedoStage) |
| `build.bat` | MSVC x86 build script (no CRT) |
| `d3d9_skinning.h` | Skinning extension (disabled for TRL — ENABLE_SKINNING=0) |

---

## 2. The Bug Hunt — Step by Step

### Step 1: Initial Analysis — Reading the Docs and Source

Read all documents in `docss/` and the full proxy source code (`d3d9_device.c`, 1780 lines). Key findings from the codebase:

- **TRL's VS constant layout** (from CTAB analysis):
  - `c0-c3`: WorldViewProjection (the ONLY transform in all shaders)
  - `c4-c6`: Fog/lighting parameters (CTAB labels this "World" but it's NOT a world matrix)
  - `c12-c15`: ViewProject (written by some shaders)
  - `c48-c95`: SkinMatrices (bone palette, 16 bones × 3 regs each)

- **Two vertex declarations** in the game:
  1. `SHORT4 POSITION + D3DCOLOR + SHORT2 TEXCOORD×2` (stride 20) — world geometry
  2. `FLOAT3 POSITION + D3DCOLOR + FLOAT2 TEXCOORD` (stride 24) — characters, overlays, UI

- **Transform decomposition**: The proxy reads View and Projection from hardcoded game memory addresses (`0x010FC780` and `0x01002530`), computes `VP = View × Proj`, and derives `World = WVP × inv(VP)`.

### Step 2: First Hypothesis — Skinned World Matrix Bug

**Theory:** The `TRL_ApplyTransformOverrides` function had a branch for `curDeclIsSkinned == 1` that read c4-c6 as a packed 3×4 World matrix. But c4-c6 contains fog/lighting parameters in TRL, not a World matrix.

**Fix applied:** Removed the skinned branch. All draws now use rigid decomposition `World = WVP × inv(VP)`.

**Result:** No visible change. Lara still invisible.

**Why it didn't help:** TRL doesn't have draws with BLENDWEIGHT+BLENDINDICES in this level, so `curDeclIsSkinned` was never 1. The fix was correct but didn't address the actual problem.

### Step 3: Second Hypothesis — Morph Target Quad Filter

**Theory:** Lara uses POSITION[1] (morph targets) with FLOAT3 position type. The screen-space quad filter checks `posType == FLOAT3` and might catch Lara's draws.

**Fix applied:** Added `curDeclHasMorph` field to track POSITION[1] and exclude from quad check.

**Result:** No visible change. TRL doesn't use POSITION[1] in this level either.

### Step 4: Third Hypothesis — Dirty Flag Stale Transforms

**Theory:** When vertex declaration changes without VS constant writes, the dirty flags stay 0 and `TRL_ApplyTransformOverrides` skips the transform update.

**Fix applied:** Force `worldDirty = 1` when vertex declaration changes.

**Result:** Minor correctness improvement, but didn't fix visibility.

### Step 5: Breakthrough — Present Never Called

**Evidence from proxy log (50MB, 2M lines):**
- 1697 `BeginScene` entries, **ZERO** `Present` entries
- drawCallCount grew past 200, preventing DIP diagnostic logging
- diagLoggedFrames never incremented, causing logging to never stop

**Root cause:** dxwrapper (D3D8→D3D9) calls `IDirect3DSwapChain9::Present` instead of `IDirect3DDevice9::Present`. Our `WD_Present` was never invoked.

**Impact:** All per-frame resets in `WD_Present` never executed:
- `ffpActive` was permanently stuck at 1 after the first draw
- `drawCallCount` never reset (grew past 200 → no DIP logging)
- `diagLoggedFrames` never incremented (log grew to 50MB)
- Frame summaries never appeared

### Step 6: The Pixel Shader Swallowing Bug

With `ffpActive` stuck at 1, `WD_SetPixelShader` had this code:

```c
if (!self->ffpActive)
    return forward_to_real_device(pShader);
return 0; /* swallowed while in FFP mode */
```

After the first draw set `ffpActive = 1`, **every subsequent SetPixelShader call was silently swallowed**. The real device kept the first pixel shader forever. Lara rendered with the wrong PS — likely one that produced transparent output for her textures.

**Fix:** Removed PS swallowing entirely. In passthrough mode (shaders stay active), PS must always pass through.

**Fix:** Moved frame resets from `WD_Present` to `WD_BeginScene`.

### Step 7: Quad Filter Skipping Everything

After fixing the Present/PS issues, the proxy log showed:

```
== FRAME 60
  total=12
  processed=0
  skippedQuad=12
```

Only 12 draws per frame, ALL caught by the quad filter, ZERO processed.

**Root cause:** The quad filter compared WVP against the **live** Projection at game memory address `0x01002530`. The game overwrites this address for each render pass (3D, UI, post-processing). When the game switches to UI rendering, BOTH c0-c3 AND the memory address hold the UI projection → they match → quad filter fires → UI draws skipped.

**Fix:** Cached the first valid 3D projection for comparison instead of reading live memory.

**Result:** UI appeared in the menu but was still missing in-game.

### Step 8: DrawPrimitiveUP Not Intercepted

The proxy only intercepted 4 draw methods:
- `DrawPrimitive` (slot 81)
- `DrawIndexedPrimitive` (slot 82)
- ~~`DrawPrimitiveUP` (slot 83) — RELAY THUNK~~
- ~~`DrawIndexedPrimitiveUP` (slot 84) — RELAY THUNK~~

dxwrapper routes most D3D8 draws through the UP variants. These went directly to Remix via relay thunks, **bypassing all transform overrides**.

**Fix:** Added `WD_DrawPrimitiveUP` and `WD_DrawIndexedPrimitiveUP` interceptors with the same draw routing logic (transform overrides, quad filter, diagnostics).

**Result:** UI appeared in menu. Lara appeared in menu. But in-game UI still missing.

### Step 9: Multi-Scene Counter Reset

The DIAG log showed `drawCalls: 1584` but the frame summary showed `total=12`. TRL uses multiple `BeginScene`/`EndScene` pairs per frame:

- **Scene 1:** ~1572 draws (world geometry, characters)
- **Scene 2:** ~12 draws (UI overlays)

The BeginScene frame-boundary code reset counters at EVERY BeginScene, so the frame summary only captured the last scene's 12 draws. And the quad filter caught all 12 as quads.

**Fix:** Moved counter resets to EndScene, accumulating across scenes.

### Step 10: Disable Quad Filter Entirely

The quad filter had been the source of multiple false-positive bugs. Analysis showed:
- The post-processing quads it was designed to block come through DrawPrimitiveUP (now intercepted) with the same VS constants as other draws
- The matrix-comparison approach can't reliably distinguish post-processing from UI/character draws
- All 12 DIP/DP draws in the second scene pass are UI elements that MUST render

**Fix:** Disabled `TRL_IsScreenSpaceQuad` (always returns 0). All draws pass through.

**Result: Everything works.** Lara visible (standing and moving), UI visible, world geometry correct, menu functional.

---

## 3. Root Causes Found

| # | Bug | Impact | Location |
|---|-----|--------|----------|
| 1 | `WD_Present` never called (dxwrapper uses SwapChain::Present) | All per-frame resets dead; ffpActive stuck at 1 | `WD_Present` vs `WD_BeginScene` |
| 2 | Pixel shader swallowed when `ffpActive=1` | Every PS change after first draw was dropped; Lara got wrong PS → invisible | `WD_SetPixelShader` |
| 3 | Quad filter compared WVP to **live** Proj address | Game updates Proj for each render pass; UI pass matched → all UI skipped | `TRL_IsScreenSpaceQuad` |
| 4 | DrawPrimitiveUP/DrawIndexedPrimitiveUP not intercepted | Most draws bypassed transform overrides entirely | Relay thunks for slots 83/84 |
| 5 | BeginScene counter reset in multi-scene frames | Counters only showed last scene pass (12 draws); misleading diagnostics | `WD_BeginScene` |
| 6 | Skinned World from c4-c6 (fog data) | Would produce garbage World for BLENDWEIGHT draws (not triggered in this level) | `TRL_ApplyTransformOverrides` |

---

## 4. Fixes Applied

### Fix 1: Remove PS Swallowing (d3d9_device.c)

**Before:**
```c
static int __stdcall WD_SetPixelShader(WrappedDevice *self, void *pShader) {
    ...
    if (!self->ffpActive)
        return forward(pShader);
    return 0; /* swallowed */
}
```

**After:**
```c
static int __stdcall WD_SetPixelShader(WrappedDevice *self, void *pShader) {
    ...
    return forward(pShader); /* always pass through in passthrough mode */
}
```

### Fix 2: Move Frame Resets to BeginScene/EndScene

Moved all per-frame resets (`ffpActive`, `drawCallCount`, counter resets) from `WD_Present` (never called) to `WD_BeginScene` (behavioral resets) and `WD_EndScene` (counter logging and resets).

### Fix 3: Disable Quad Filter

```c
static int TRL_IsScreenSpaceQuad(WrappedDevice *self) {
    (void)self;
    return 0; /* disabled — causes more harm than good */
}
```

### Fix 4: Intercept UP Draw Calls

Added `WD_DrawPrimitiveUP` (slot 83) and `WD_DrawIndexedPrimitiveUP` (slot 84) with transform override logic. Replaced relay thunks in the vtable.

### Fix 5: Always Use Rigid World Decomposition

Removed the skinned branch that read c4-c6 as World. All draws use `World = WVP × inv(VP)` from c0-c3.

### Fix 6: Track Morph Targets

Added `curDeclHasMorph` field set when POSITION[1] is detected. Defensive measure for future levels.

### Fix 7: Force Dirty on Declaration Change

`worldDirty = 1` when vertex declaration changes, ensuring transform recomputation.

---

## 5. Technical Definitions

### Transform Decomposition

TRL fuses World, View, and Projection into a single WVP matrix in VS constant registers c0-c3 (column-major). RTX Remix needs separate W/V/P to place geometry correctly for path tracing.

**Method:**
1. Read View from game memory at `0x010FC780` (row-major, 4×4)
2. Read Projection from game memory at `0x01002530` (row-major, 4×4)
3. Compute `VP = View × Projection`
4. Cache `inv(VP)` (recompute only when VP changes by more than `1e-4` in any element)
5. Transpose c0-c3 from column-major to row-major → `WVP_row`
6. Compute `World = WVP_row × inv(VP)`
7. Call `SetTransform(D3DTS_WORLD, World)`, `SetTransform(D3DTS_VIEW, View)`, `SetTransform(D3DTS_PROJECTION, Proj)`

### Shader Passthrough Mode

Unlike full FFP conversion (where shaders are replaced with NULL), passthrough mode keeps all vertex and pixel shaders active. This is necessary because TRL's vertex shaders decode SHORT4-encoded positions that the FFP pipeline cannot interpret. Remix captures the decoded post-shader positions via `rtx.useVertexCapture=True`.

### dxwrapper Behavior

dxwrapper's `D3d8to9=1` mode translates D3D8 API calls to D3D9:
- `IDirect3DDevice8::Present` → `IDirect3DSwapChain9::Present` (NOT `IDirect3DDevice9::Present`)
- Most draw calls → `DrawPrimitiveUP` / `DrawIndexedPrimitiveUP` (UP = user pointer)
- `SetTransform` → Sends identity View/Proj + combined WVP as World (~1296 calls/frame, all blocked by our proxy)

### Relay Thunks

MSVC x86 naked thunks that replace `this` (WrappedDevice*) with `pReal` (real device*) and jump to the real vtable. Used for non-intercepted methods (100+ of 119 total IDirect3DDevice9 methods). Zero overhead.

```asm
mov eax, [esp+4]       ; eax = WrappedDevice*
mov ecx, [eax+4]       ; ecx = pReal
mov [esp+4], ecx       ; replace this
mov eax, [ecx]         ; eax = real vtable
jmp dword ptr [eax + slot*4]
```

### Vertex Declaration Parsing

`WD_SetVertexDeclaration` parses D3D9 vertex elements to detect:
- `curDeclPosType`: FLOAT3 vs SHORT4 (affects quad detection)
- `curDeclIsSkinned`: BLENDWEIGHT + BLENDINDICES present
- `curDeclHasMorph`: POSITION[1] present (morph targets)
- `curDeclHasPosT`: POSITIONT (pre-transformed, skips FFP)
- `curDeclHasNormal`, `curDeclHasColor`, `curDeclHasTexcoord`

### VP Inverse Cache

Computing `inv(VP)` is expensive (cofactor expansion, 4×4 matrix). The proxy caches `inv(VP)` and the VP it was computed from. Only recomputes when any element of VP changes by more than `VP_CHANGE_THRESHOLD` (1e-4). This avoids redundant matrix inversions when the camera hasn't moved.

### Game Memory Patches

Applied once at device creation:
- `0x00EFDD64` (frustum threshold): Set to `1e30` so nothing is frustum-culled. Without this, geometry behind the camera is culled and RTX Remix can't ray-trace it.
- Backface culling: Forced to `D3DCULL_NONE` via `WD_SetRenderState` for all draws. Remix needs all faces visible for ray tracing from any direction.

---

## 6. File Manifest

### Working Build (2026-03-22)

```
patches/TombRaiderLegend/backups/2026-03-22_working-lara-visible/
├── d3d9_device.c          # Core proxy (main interceptor, ~1850 lines)
├── d3d9_main.c            # DLL entry, Remix chain-loading, logging
├── d3d9_wrapper.c         # IDirect3D9 wrapper (intercepts CreateDevice)
├── d3d9.def               # Export definition
├── d3d9_skinning.h        # Skinning extension (ENABLE_SKINNING=0)
├── proxy.ini              # Runtime config
├── build.bat              # MSVC x86 build script
├── d3d9.dll               # Compiled proxy DLL (20,480 bytes)
└── ffp_proxy_working.log  # Proxy log from successful test run
```

### Game Directory Deployment

```
Tomb Raider Legend/
├── trl.exe                # Game binary (never modified)
├── dxwrapper.dll          # D3D8→D3D9 translation layer
├── dxwrapper.ini          # dxwrapper config (D3d8to9=1)
├── d3d9.dll               # ← OUR PROXY (copy from build)
├── d3d9_remix.dll         # NVIDIA RTX Remix runtime
├── proxy.ini              # ← OUR CONFIG (copy from build)
└── ffp_proxy.log          # Diagnostic output (generated at runtime)
```

---

## 7. How to Reproduce the Build

### Prerequisites

- Windows 10/11
- Visual Studio with C++ desktop workload (Build Tools)
- The game installed with dxwrapper and RTX Remix already set up

### Build Steps

```batch
cd patches\TombRaiderLegend\proxy
build.bat
```

This produces `d3d9.dll` (20KB, no CRT dependency, x86).

### Deploy

Copy `d3d9.dll` and `proxy.ini` to the game directory (where `trl.exe` is).

### Verify

1. Launch the game
2. Check `ffp_proxy.log` appears in the game directory
3. Log should show:
   - `WrappedDevice created with shader passthrough + transform override`
   - `Patched frustum threshold to 1e30`
   - `== SCENE` entries with `processed > 0`
4. Lara should be visible in menus and in-game (including while moving)

---

## 8. Lessons Learned

### 1. Verify the frame boundary mechanism

When wrapping a D3D9 device behind a D3D8→D3D9 translation layer, `Present` may not be called on the wrapped device. dxwrapper calls `SwapChain::Present` directly. Always check the proxy log for `Present` calls and use `BeginScene`/`EndScene` as alternative frame boundaries.

### 2. Don't swallow shader changes in passthrough mode

The PS swallowing logic (`return 0` when `ffpActive=1`) was inherited from full FFP conversion mode where shaders are replaced with NULL. In passthrough mode where shaders stay active, this silently drops every pixel shader change after the first draw — causing objects to render with the wrong shader.

### 3. Intercept ALL draw methods

D3D9 has four draw methods: `DrawPrimitive`, `DrawIndexedPrimitive`, `DrawPrimitiveUP`, `DrawIndexedPrimitiveUP`. dxwrapper routes most D3D8 draws through the UP variants. If the UP methods are relay thunks, the majority of draws bypass all proxy logic. Always intercept all four.

### 4. Don't compare against live game memory for filtering

The quad filter compared WVP against the Projection at a hardcoded game memory address. But the game updates this address for each render pass (3D, UI, post-processing). Using the live value meant UI draws matched and were incorrectly skipped. Either cache the reference value at first use, or use a different heuristic entirely.

### 5. Multi-scene frames break per-BeginScene counters

Games may call `BeginScene`/`EndScene` multiple times per frame (shadow passes, reflection passes, UI passes). Resetting counters at every `BeginScene` only captures the last pass. Accumulate across scenes and reset at a true frame boundary.

### 6. The proxy log is your best diagnostic tool

Every fix in this report was informed by proxy log data:
- Zero `Present` calls → discovered the dxwrapper bypass
- `total=12, skippedQuad=12` → discovered the quad filter false-positives
- `drawCalls: 1584` vs `total=12` → discovered the multi-scene counter bug

Always ensure the log captures accurate per-frame data before debugging rendering issues.

### 7. Check relay thunks when draw counts are too low

If the proxy sees far fewer draws than expected, check if the rendering API is using draw methods that go through relay thunks (unintercepted). The relay thunks pass calls directly to the real device, bypassing all proxy logic.
