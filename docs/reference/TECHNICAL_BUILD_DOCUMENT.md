# Tomb Raider Legend RTX â Complete Technical Build Document

**Build:** Candidate1 (Build1)
**Date:** 2026-04-02
**Target:** Tomb Raider Legend (2006, Crystal Dynamics / Eidos, Steam version)
**Platform:** Windows 11, NVIDIA RTX GPU, DirectX 9

---

## Table of Contents

1. [Architecture Overview](#1-architecture-overview)
2. [DLL Chain Loading Order](#2-dll-chain-loading-order)
3. [The Core Problem: Why TRL Needs a Proxy](#3-the-core-problem-why-trl-needs-a-proxy)
4. [Proxy DLL â Full Technical Specification](#4-proxy-dll--full-technical-specification)
5. [Game Memory Patches](#5-game-memory-patches)
6. [RTX Remix Configuration](#6-rtx-remix-configuration)
7. [DXWrapper Configuration](#7-dxwrapper-configuration)
8. [Build Process â Exact Reproduction Steps](#8-build-process--exact-reproduction-steps)
9. [File Manifest](#9-file-manifest)
10. [Reverse Engineering Discoveries](#10-reverse-engineering-discoveries)
11. [Known Issues and Limitations](#11-known-issues-and-limitations)
12. [Glossary](#12-glossary)

---

## 1. Architecture Overview

RTX Remix is NVIDIA's tool for adding ray tracing to classic DirectX 8/9 games. It works by intercepting Fixed-Function Pipeline (FFP) draw calls and replacing the rasterized output with path-traced rendering. However, Tomb Raider Legend uses **programmable vertex and pixel shaders** (not FFP), so Remix cannot directly understand the game's rendering.

The solution is a **d3d9 proxy DLL** that sits between the game and Remix. This proxy:

1. **Intercepts** the game's shader-based draw calls
2. **Reads** the game's View, Projection, and World matrices from VS constant registers and game memory
3. **Decomposes** the fused WorldViewProjection (WVP) matrix into separate W, V, P components
4. **Calls SetTransform** with the decomposed matrices so Remix can build its scene graph
5. **Keeps shaders active** â TRL uses SHORT4 compressed vertex positions that require the original vertex shader to decode. Remix captures post-shader vertex positions via `rtx.useVertexCapture=True`
6. **Patches game memory** at runtime to disable frustum culling, light culling, and backface culling â Remix needs all geometry and lights visible from any angle for ray tracing

### Component Stack

```
trl.exe (Tomb Raider Legend, 32-bit x86, MSVC-compiled)
  â
  âââ dxwrapper.dll (D3D8âD3D9 translation layer)
  â     âââ Creates D3D9 device
  â
  âââ d3d9.dll (OUR PROXY â 23 KB, no-CRT pure C)
  â     âââ Wraps IDirect3D9::CreateDevice
  â     âââ Wraps IDirect3DDevice9 (119 methods, ~15 intercepted)
  â     âââ Reads game memory for View/Proj matrices
  â     âââ Decomposes WVP â W, V, P via matrix math
  â     âââ Calls SetTransform(VIEW/PROJECTION/WORLD)
  â     âââ Patches game memory (culling, lights, frustum)
  â     âââ Chain-loads d3d9_remix.dll
  â
  âââ d3d9_remix.dll (RTX Remix bridge client, 2 MB)
  â     âââ Communicates with NvRemixBridge.exe via IPC
  â
  âââ .trex/NvRemixBridge.exe (64-bit Remix server)
        âââ Receives D3D9 calls over shared memory
        âââ Builds scene graph from FFP state
        âââ Captures vertex positions via vertex capture
        âââ Renders with Vulkan ray tracing (DLSS, NRC, ReSTIR)
```

---

## 2. DLL Chain Loading Order

When `NvRemixLauncher32.exe trl.exe` starts:

1. **trl.exe** loads, calls `LoadLibrary("d3d9.dll")` â picks up our proxy from game directory
2. **Our d3d9.dll** exports `Direct3DCreate9`. In `DllMain`, it reads `proxy.ini`:
   - `[Remix] Enabled=1, DLLName=d3d9_remix.dll`
   - Loads `d3d9_remix.dll` and resolves its `Direct3DCreate9`
3. When game calls `Direct3DCreate9`:
   - Proxy calls Remix's `Direct3DCreate9` to get the real `IDirect3D9`
   - Wraps it in `WrappedDirect3D9` (intercepts `CreateDevice`)
4. When game calls `CreateDevice`:
   - Proxy calls Remix's `CreateDevice` to get the real `IDirect3DDevice9`
   - Wraps it in `WrappedDevice` (119-method vtable, ~15 methods intercepted, rest relay via naked ASM thunks)
   - Applies one-shot game memory patches
5. **DXWrapper** (`dxwrapper.dll`) is also present â it translates TRL's D3D8 calls to D3D9. It has its own d3d9 path but our proxy.ini's chain loading ensures correct order.

### INI Chain Configuration

**proxy.ini:**
```ini
[Remix]
Enabled=1
DLLName=d3d9_remix.dll

[Chain]
PreloadDLL=              ; empty â no additional side-effect DLLs

[FFP]
AlbedoStage=0            ; texture stage 0 holds albedo/diffuse
```

---

## 3. The Core Problem: Why TRL Needs a Proxy

### 3.1 Fused WorldViewProjection Matrix

TRL's vertex shaders receive a **single combined WVP matrix** in constant registers c0-c3 (column-major). RTX Remix requires **separate** World, View, and Projection matrices via `SetTransform` to:

- Determine camera position and orientation (View)
- Compute perspective/FOV (Projection)
- Position each object in world space (World)
- Build the BVH (Bounding Volume Hierarchy) for ray tracing

**Our solution:** Read the authoritative View and Projection matrices directly from game memory at hardcoded addresses, compute `VP = View * Projection`, cache `inverse(VP)`, and derive `World = WVP * inverse(VP)`.

### 3.2 SHORT4 Compressed Vertex Positions

TRL stores vertex positions as `SHORT4` (4x 16-bit signed integers) instead of `FLOAT3`. The vertex shader multiplies these by the WVP matrix to produce clip-space positions. Remix's FFP path expects `FLOAT3` positions.

**Our solution:** Keep the game's vertex shaders active (shader passthrough mode). Remix's `rtx.useVertexCapture=True` setting captures the **post-shader** vertex positions from the GPU's vertex output stage, bypassing the need to interpret SHORT4 data.

### 3.3 Aggressive Frustum and Light Culling

TRL aggressively culls geometry and lights that are outside the camera frustum. This is correct for rasterization but catastrophic for ray tracing â rays need to bounce off objects behind and beside the camera.

**Our solution:** Runtime memory patches that disable:
- Frustum threshold (set to -1e30)
- Cull mode globals (force D3DCULL_NONE)
- Light frustum rejection (NOP the jump)
- Light visibility pre-test (NOP to always pass)
- Sector light count gate (NOP to always load lights)
- Light count per-frame clearing (NOP to persist across frames)
- Render lights gate (NOP to force light rendering)
- Render flags bit 20 (clear to keep object rendering loop active)
- Far clip distance (set to 1e30)

### 3.4 Per-Vertex Baked Lighting

TRL bakes per-vertex lighting as D3DCOLOR values that change with camera position. For UserPointer (UP) draws, Remix hashes the raw vertex bytes â changing colors mean changing hashes, which breaks material/light anchoring.

**Our solution:** For DrawPrimitiveUP/DrawIndexedPrimitiveUP calls, copy vertex data to a scratch buffer and neutralize all COLOR[0] values to white (0xFFFFFFFF). Remix handles lighting via path tracing.

### 3.5 Non-FLOAT3 Normals

TRL vertex declarations use SHORT4N and DEC3N normals. Remix's game capturer asserts normals are FLOAT3. 

**Our solution:** Strip NORMAL elements from vertex declarations when they aren't FLOAT3. Remix computes smooth normals via path tracing, so input normals aren't needed. A cache maps original declarations to stripped versions.

### 3.6 DXWrapper SetTransform Interference

DXWrapper (D3D8âD3D9 bridge) sends ~1296 SetTransform calls per frame with View=Identity, Proj=Identity, World=WVP-combined. These overwrite our decomposed transforms.

**Our solution:** Once `viewProjValid` is set (first meaningful VS constant write), ALL external SetTransform calls for View, Projection, and World are blocked. Only our own calls (marked by `transformOverrideActive` flag) pass through.

### 3.7 Z-Prepass Duplicate Geometry

TRL renders a depth-only Z-prepass (colorWriteEnable=0) before the main pass. Remix would see duplicate geometry that corrupts instance tracking.

**Our solution:** Track `D3DRS_COLORWRITEENABLE` via SetRenderState interception. When colorWriteEnable==0, suppress the draw call entirely.

---

## 4. Proxy DLL â Full Technical Specification

### 4.1 Source Files

| File | Lines | Purpose |
|------|-------|---------|
| `d3d9_device.c` | 2481 | Core proxy â WrappedDevice with 119-method IDirect3DDevice9 vtable. ~15 methods intercepted, rest use naked ASM relay thunks. Contains all game-specific logic. |
| `d3d9_main.c` | ~200 | DLL entry point, proxy.ini parsing, logging infrastructure, chain-load Remix DLL. Exports `Direct3DCreate9`. |
| `d3d9_wrapper.c` | ~200 | WrappedDirect3D9 â intercepts `CreateDevice` to wrap the returned device. |
| `d3d9_skinning.h` | ~400 | Skinning extension (ENABLE_SKINNING=0, not active in this build). |
| `d3d9.def` | 2 | DLL export definition: `Direct3DCreate9 @1`. |
| `build.bat` | 68 | MSVC x86 no-CRT build script. |
| `proxy.ini` | 28 | Runtime configuration. |

### 4.2 Game-Specific Constants (d3d9_device.c, lines 46-138)

These are the values discovered through reverse engineering of trl.exe:

#### VS Constant Register Layout

TRL uploads shader constants in 16-register batches (c0-c15):

| Registers | Content | Format | Source |
|-----------|---------|--------|--------|
| c0-c3 | WorldViewProjection (WVP) | 4x4 column-major | Computed by engine: World * View * Proj |
| c4-c7 | Fog/lighting params; for skinned draws: packed 4x3 World matrix | column-major | Engine render state |
| c8-c11 | View matrix | 4x4 column-major | From renderer struct +0x480 â +0x4C0 |
| c12-c15 | Projection matrix | 4x4 column-major | From renderer struct +0x500 |
| c48-c95 | SkinMatrices (48 regs = 16 bones Ã 3 regs each) | 4x3 packed column-major | Skeletal animation system |

```c
#define VS_REG_WVP_START        0
#define VS_REG_WVP_END          4
#define VS_REG_VIEW_SRC_START   8
#define VS_REG_VIEW_SRC_END    12
#define VS_REG_PROJ_SRC_START  12
#define VS_REG_PROJ_SRC_END    16
```

#### Game Memory Addresses (trl.exe, no ASLR, preferred base 0x00400000)

| Address | Type | Content | How Discovered |
|---------|------|---------|----------------|
| `0x010FC780` | float[16] | View matrix (row-major) | Decompiled renderer struct; traced via livetools |
| `0x01002530` | float[16] | Projection matrix (row-major) | Decompiled renderer struct; traced via livetools |
| `0x00EFDD64` | float | Frustum rejection threshold | Decompiled SceneTraversal; static analysis |
| `0x0040EEA7` | code | Conditional cull instruction | Disassembled cull decision tree |
| `0x00F2A0D4` | uint32 | g_cullMode_pass1 (cached cull state) | Decompiled renderer; datarefs analysis |
| `0x00F2A0D8` | uint32 | g_cullMode_pass2 | Adjacent to pass1 in .data section |
| `0x00F2A0DC` | uint32 | g_cullMode_pass2_inverse | Adjacent to pass2 |
| `0x0060CE20` | code (6 bytes) | Light frustum reject JNP in RenderLights_FrustumCull | Decompiled at 0x0060C7D0; Ghidra analysis |
| `0x0060B050` | code | Light_VisibilityTest entry | Decompiled; performs distance/sphere/cone pre-cull |
| `0x01075BE0` | uint32 | Engine light culling disable flag | Found via string/constant search |
| `0x00EC6337` | code | Sector light count gate JZ | Decompiled sector loading; controls static light submission |
| `0x00603AE6` | code | Light count clear MOV in cleanup function at 0x603AD0 | Decompiled frame cleanup; zeros sector +0x1B0 |
| `0x0060E3B1` | code | RenderLights gate JE | Decompiled; checks stack flag from sector light count |
| `0x010E5384` | uint32 | Render flags global (bit 20 = skip object loop) | Decompiled scene traversal at 0x40E2C0 |
| `0x010FC910` | float | Far clip distance | Decompiled camera setup; game sets per-level |

### 4.3 Intercepted IDirect3DDevice9 Methods

Of 119 vtable methods, these are intercepted with custom logic:

| Slot | Method | What We Do |
|------|--------|-----------|
| 0 | QueryInterface | Relay (pReal swap) |
| 1 | AddRef | Track refcount + relay |
| 2 | Release | Track refcount, cleanup on zero, relay |
| 16 | Reset | Release cached state, relay |
| 17 | Present | Frame counter, diagnostic logging, reset per-frame state |
| 41 | BeginScene | Re-stamp memory patches (frustum, cull globals, far clip, light flag, render flags) every scene |
| 42 | EndScene | Frame boundary detection, logging |
| 44 | SetTransform | Block external V/P/W once viewProjValid; allow own calls via transformOverrideActive flag |
| 57 | SetRenderState | Force D3DCULL_NONE on CULLMODE; track COLORWRITEENABLE for Z-prepass detection |
| 65 | SetTexture | Track per-stage textures |
| 81 | DrawPrimitive | Draw routing: Z-prepass suppression, degenerate guard, transform override, quad detection |
| 82 | DrawIndexedPrimitive | Same as DP plus skinning detection and diagnostic vertex dump |
| 83 | DrawPrimitiveUP | Transform override + vertex color neutralization for hash stability |
| 84 | DrawIndexedPrimitiveUP | Same as DPUP |
| 87 | SetVertexDeclaration | Parse vertex elements: detect POSITION type, BLENDWEIGHT/INDICES (skinning), NORMAL, COLOR offset, POSITIONT, MORPH targets. Strip non-FLOAT3 normals. |
| 89 | SetFVF | Parse FVF flags for NORMAL presence; strip NORMAL bit from FVF |
| 92 | SetVertexShader | Track current VS, manage refcount |
| 94 | SetVertexShaderConstantF | Cache all constants, dirty tracking for WVP/View/Proj ranges, bone upload for skinning |
| 100 | SetStreamSource | Track VB, offset, stride per stream |
| 107 | SetPixelShader | Track current PS, manage refcount |
| 109 | SetPixelShaderConstantF | Cache PS constants |

All other 104 methods use **naked ASM relay thunks** â a 5-instruction x86 sequence that swaps the `this` pointer from WrappedDevice to pReal and jumps to the real vtable:

```asm
mov eax, [esp+4]       ; eax = WrappedDevice*
mov ecx, [eax+4]       ; ecx = pReal (real IDirect3DDevice9*)
mov [esp+4], ecx       ; replace this pointer on stack
mov eax, [ecx]         ; eax = real vtable
jmp dword ptr [eax + SLOT*4]  ; jump to real method
```

### 4.4 Transform Decomposition Pipeline

Per-draw call flow in `TRL_ApplyTransformOverrides`:

```
1. Read View[16] from *(float*)0x010FC780 (row-major)
2. Read Proj[16] from *(float*)0x01002530 (row-major)
3. Cache first valid 3D Projection for quad detection
4. Compute VP = View Ã Proj (row-major multiply)
5. If VP changed from last frame (any element differs by > 1e-4):
     Recompute VP_inverse via 4x4 cofactor expansion
     Cache VP_inverse and lastVP
6. Read WVP from vsConst[c0..c3] (column-major in shader constants)
7. Transpose WVP to row-major: wvp_row
8. Classify draw type:
   a. FLOAT3 position + projection-like c0-c3 (row3 â [0,0,Â±1,0]):
      â View-space draw: World=Identity, View=Identity, Proj=game projection
   b. Skinned (BLENDWEIGHT+BLENDINDICES in decl):
      â Read packed 4x3 World from c4-c6, transpose to 4x4
   c. Standard SHORT4 world geometry:
      â World = wvp_row Ã VP_inverse
9. Call SetTransform(D3DTS_WORLD, world)
10. Call SetTransform(D3DTS_VIEW, view)
11. Call SetTransform(D3DTS_PROJECTION, proj)
12. Clear dirty flags
```

### 4.5 Draw Routing Decision Tree

For DrawIndexedPrimitive (the main draw path):

```
colorWriteEnable == 0?
âââ YES â suppress (Z-prepass)
âââ primCount==0 OR numVerts==0 OR no VS OR no decl?
â   âââ YES â suppress (degenerate)
âââ viewProjValid?
    âââ NO â suppress (no transform data yet)
    âââ YES
        âââ TRL_IsScreenSpaceQuad? â suppress (disabled, always returns 0)
        âââ TRL_PrepDraw â apply transform overrides + inject light â draw
```

### 4.6 WrappedDevice Structure

Key fields of the 119-method wrapped device (allocated on heap, ~12 KB):

```c
typedef struct WrappedDevice {
    void **vtbl;                    // Our 119-entry vtable
    void *pReal;                    // Real IDirect3DDevice9*
    int refCount;
    unsigned int frameCount;
    int ffpSetup;                   // Light injected this frame?

    float vsConst[256 * 4];        // All 256 VS constant registers (vec4 each)
    float psConst[32 * 4];         // All 32 PS constant registers
    int worldDirty;                 // c0-c3 or c4-c7 changed
    int viewProjDirty;              // c8-c15 changed

    void *lastVS, *lastPS;         // Current shader pointers
    int viewProjValid;              // Set once WVP or Proj written
    int ffpActive;                  // Transform overrides applied this draw

    void *lastDecl;                 // Current vertex declaration
    int curDeclIsSkinned;           // BLENDWEIGHT + BLENDINDICES present
    int curDeclHasNormal;           // NORMAL element present
    int curDeclHasColor;            // COLOR[0] present
    int curDeclColorOff;            // Byte offset of COLOR[0] in vertex
    int curDeclHasPosT;             // POSITIONT (screen-space, skip transform)
    int curDeclPosType;             // D3DDECLTYPE of POSITION (2=FLOAT3, etc.)

    float cachedVPInverse[16];      // Cached inverse(View * Projection)
    float lastVP[16];               // Last VP for cache invalidation
    int vpInverseValid;             // Cache valid flag
    int transformOverrideActive;    // 1 during our SetTransform calls
    int memoryPatchesApplied;       // One-shot code patches done

    unsigned int colorWriteEnable;  // Shadow of D3DRS_COLORWRITEENABLE

    void *strippedDeclOrig[64];     // Normal-stripped decl cache (original â fixed)
    void *strippedDeclFixed[64];
    int strippedDeclCount;
} WrappedDevice;
```

---

## 5. Game Memory Patches

All patches are applied at runtime by the proxy DLL. No game files are modified on disk. Patches are re-applied every BeginScene because the game's engine overwrites some values per-frame.

### 5.1 One-Shot Code Patches (applied once in CreateDevice)

These modify executable code in trl.exe's .text section:

| Address | Original | Patched To | Purpose |
|---------|----------|-----------|---------|
| `0x0060CE20` | `0F 85 xx xx xx xx` (JNZ, 6 bytes) | `90 90 90 90 90 90` (6Ã NOP) | Disable light frustum rejection in RenderLights_FrustumCull |
| `0x0060B050` | Function prologue | `B8 01 00 00 00 C3` (MOV EAX,1; RET) | Force Light_VisibilityTest to always return TRUE |
| `0x00EC6337` | `74 xx` (JZ, 2 bytes) | `90 90` (2Ã NOP) | NOP sector light count gate â force all sectors to load lights |
| `0x00603AE6` | `89 xx xx xx xx xx` (MOV, 6 bytes) | `90 90 90 90 90 90` (6Ã NOP) | Prevent per-frame clearing of sector light count (+0x1B0) |
| `0x0060E3B1` | `74 xx` (JE, 2 bytes) | `90 90` (2Ã NOP) | NOP RenderLights gate â force light rendering regardless of sector count |

### 5.2 Per-Scene Data Patches (re-stamped every BeginScene)

| Address | Value | Purpose |
|---------|-------|---------|
| `0x00EFDD64` | `-1e30f` (float) | Frustum rejection threshold â ensures nothing is culled by distance |
| `0x010FC910` | `1e30f` (float) | Far clip distance â prevents far-plane clipping |
| `0x00F2A0D4` | `1` (uint32) | g_cullMode_pass1 = D3DCULL_NONE |
| `0x00F2A0D8` | `1` (uint32) | g_cullMode_pass2 = D3DCULL_NONE |
| `0x00F2A0DC` | `1` (uint32) | g_cullMode_pass2_inverse = D3DCULL_NONE |
| `0x01075BE0` | `1` (uint32) | Engine light culling disable flag |
| `0x010E5384` | `AND ~0x00100000` | Clear render flags bit 20 â keep object rendering loop active |

All memory patches use `VirtualProtect` to set PAGE_EXECUTE_READWRITE before writing, then restore original protection.

---

## 6. RTX Remix Configuration

### 6.1 rtx.conf (Game-Specific Remix Settings)

This file controls how RTX Remix interprets the game's D3D9 state:

```ini
# Matrix handling â proxy separates W/V/P, don't fuse them
rtx.fusedWorldViewMode = 0

# Coordinate system â TRL uses Z-up (not Y-up)
rtx.zUp = True

# Vertex capture â shaders stay active, capture post-VS positions
rtx.useVertexCapture = True

# Enable ray tracing
rtx.enableRaytracing = True

# Scene scale â TRL uses large unit scale (~10000 units per meter)
rtx.sceneScale = 0.0001

# Geometry hashing â use indices + texcoords + descriptor for stable hashes
# Excludes positions (which change per-view due to vertex shader transforms)
rtx.geometryAssetHashRuleString = indices,texcoords,geometrydescriptor

# Anti-culling â keep geometry alive in the BVH even when the game stops submitting it
rtx.antiCulling.object.enable = False  # (disabled â proxy handles culling itself)
rtx.antiCulling.object.fovScale = 2
rtx.antiCulling.object.enableHighPrecisionAntiCulling = True

# Light anti-culling
rtx.antiCulling.light.enable = False
rtx.antiCulling.light.fovScale = 3
rtx.antiCulling.light.numFramesToExtendLightLifetime = 120

# Fallback lighting â neutral white to prevent dark scenes
rtx.fallbackLightMode = 2
rtx.fallbackLightRadiance = 1, 1, 1
rtx.fallbackLightDirection = -70.5, -70.5, -70.5

# UI and sky detection
rtx.orthographicIsUI = True
rtx.skyAutoDetect = 0
rtx.skyBrightness = 1

# Near plane override â prevent z-fighting at close range
rtx.enableNearPlaneOverride = True
rtx.nearPlaneOverride = 0.1

# Capture instances (required for ray tracing scene building)
rtx.captureInstances = True

# Terrain settings
rtx.terrain.terrainAsDecalsEnabledIfNoBaker = True
rtx.terrain.terrainAsDecalsAllowOverModulate = False
rtx.terrainBaker.enableBaking = False

# Remix UI keybind
rtx.remixMenuKeyBinds = X
```

#### Texture Hash Classifications

These hashes were identified by inspecting the game's rendered output in Remix's debug view (Geometry/Asset Hash mode, index 277):

| Category | Hashes | Purpose |
|----------|--------|---------|
| `rtx.skyBoxTextures` | `0x443B45FB...` (7 hashes) | Sky dome textures â excluded from ray tracing |
| `rtx.uiTextures` | `0x03016D2F...` (4 hashes) | HUD/menu textures â excluded from scene |
| `rtx.particleTextures` | `0x0E197C80...` (8 hashes) | Particle effects â special Remix handling |
| `rtx.ignoreTextures` | `0x33D473C4...` (2 hashes) | Textures to completely ignore |
| `rtx.decalTextures` | `0x3082A54F...` (1 hash) | Decal textures â Remix projects them |
| `rtx.smoothNormalsTextures` | 12 hashes | Textures that should use Remix smooth normals |

### 6.2 user.conf (RTX Quality Settings)

Contains per-user quality tuning (DLSS preset, ReSTIR GI, NRC settings, ray bounce counts). These are adjustable without affecting game compatibility. Key settings:

- `rtx.graphicsPreset = 4` (Ultra)
- `rtx.qualityDLSS = 3` (Quality)
- `rtx.pathMaxBounces = 4`
- `rtx.neuralRadianceCache.qualityPreset = 2`

---

## 7. DXWrapper Configuration

TRL was originally a DirectX 8 game. DXWrapper (`dxwrapper.dll`) translates D3D8 calls to D3D9, which our proxy and Remix then process.

Key dxwrapper.ini settings:
```ini
[Compatibility]
D3d8to9 = 1              ; Enable D3D8âD3D9 translation
EnableD3d9Wrapper = 0     ; Don't wrap D3D9 again (our proxy does this)
```

---

## 8. Build Process â Exact Reproduction Steps

### 8.1 Prerequisites

- **Visual Studio 2022** (or later) with "Desktop development with C++" workload
  - Specifically needs the MSVC x86 compiler (not just x64)
  - The build script auto-detects VS via `vswhere.exe`
- **Windows 11** (or Windows 10)
- No other dependencies â the proxy is compiled with `/NODEFAULTLIB` (no C runtime)

### 8.2 Compile the Proxy DLL

```cmd
cd proxy
build.bat
```

**What build.bat does:**

1. Finds Visual Studio installation via `vswhere.exe`
2. Calls `vcvarsall.bat x86` to set up 32-bit compiler environment
3. Compiles three source files with these exact flags:
   ```
   cl.exe /nologo /O1 /GS- /W3 /Zl /c /D "WIN32" /D "NDEBUG" d3d9_main.c
   cl.exe /nologo /O1 /GS- /W3 /Zl /c /D "WIN32" /D "NDEBUG" d3d9_wrapper.c
   cl.exe /nologo /O1 /Oi /GS- /W3 /Zl /c /D "WIN32" /D "NDEBUG" d3d9_device.c
   ```
   - `/O1` â minimize size
   - `/GS-` â disable buffer security checks (no CRT)
   - `/W3` â warning level 3
   - `/Zl` â omit default library name in .obj
   - `/Oi` â enable intrinsics (d3d9_device.c only)
4. Links with no default libraries:
   ```
   link.exe /nologo /DLL /NODEFAULTLIB /ENTRY:_DllMainCRTStartup@12 
            /DEF:d3d9.def /OUT:d3d9.dll 
            d3d9_main.obj d3d9_wrapper.obj d3d9_device.obj kernel32.lib
   ```
   - `/NODEFAULTLIB` â no C runtime, only kernel32.lib
   - `/ENTRY:_DllMainCRTStartup@12` â custom entry point
   - `/DEF:d3d9.def` â exports `Direct3DCreate9`
5. Cleans up .obj, .lib, .exp intermediate files
6. Output: `d3d9.dll` (approximately 23 KB)

### 8.3 Deploy to Game Directory

Copy these files to the Tomb Raider Legend game directory:

```
d3d9.dll          â from proxy/d3d9.dll (freshly compiled)
proxy.ini         â from proxy/proxy.ini
rtx.conf          â from patches/TombRaiderLegend/rtx.conf (or Tomb Raider Legend/rtx.conf)
```

The following must already be in the game directory (from RTX Remix installation):
```
d3d9_remix.dll    â RTX Remix bridge client
NvRemixLauncher32.exe â RTX Remix launcher
.trex/            â RTX Remix runtime (50+ DLLs, shaders, plugins)
dxwrapper.dll     â D3D8âD3D9 compatibility (from dxwrapper project)
dxwrapper.ini     â DXWrapper configuration
```

### 8.4 Launch

```cmd
NvRemixLauncher32.exe trl.exe
```

Or directly via the test orchestrator:
```cmd
python patches/TombRaiderLegend/run.py test --build
```

The `--build` flag compiles the proxy first, deploys it, then launches and runs the automated test macro.

### 8.5 Verify the Build

After launching, check `ffp_proxy.log` in the game directory (written 50 seconds after device creation):

**Healthy log indicators:**
- `vpValid=1` â transform decomposition is working
- `processed=` > 0 â draw calls are being routed through the proxy
- `zPrepass=` > 0 â Z-prepass suppression is active
- `xformBlocked=` > 0 â dxwrapper SetTransform interference is being blocked
- View/Proj matrices show non-zero, non-identity values
- `decomp error` < 0.1 â WVP decomposition is accurate

---

## 9. File Manifest

### Build1 (Drag-and-Drop Installation)

| File | Size | Required | Description |
|------|------|----------|-------------|
| `d3d9.dll` | 23 KB | YES | Our FFP proxy DLL |
| `d3d9_remix.dll` | 2 MB | YES | RTX Remix bridge client |
| `proxy.ini` | 957 B | YES | Proxy configuration (chain loading, albedo stage) |
| `rtx.conf` | ~1.9 KB | YES | RTX Remix game-specific settings |
| `user.conf` | ~2 KB | Optional | RTX quality tuning (DLSS, bounces, NRC) |
| `dxvk.conf` | ~14 KB | Optional | DXVK configuration |
| `dxwrapper.dll` | 8 MB | YES | D3D8âD3D9 translation |
| `dxwrapper.ini` | ~4 KB | YES | DXWrapper settings |
| `remix-comp-proxy.ini` | ~2.4 KB | Optional | Compiled proxy variant config |
| `NvRemixLauncher32.exe` | 138 KB | YES | RTX Remix launcher |
| `.trex/` | ~200 MB | YES | RTX Remix runtime (DLLs, shaders, plugins) |
| `rtx-remix/` | varies | Optional | Captures and user mods folder |

### Source Files (for rebuilding)

| File | Description |
|------|-------------|
| `proxy/d3d9_device.c` | Core proxy â 2481 lines, all game-specific logic |
| `proxy/d3d9_main.c` | DLL entry, logging, chain loading â ~200 lines |
| `proxy/d3d9_wrapper.c` | IDirect3D9 wrapper â ~200 lines |
| `proxy/d3d9_skinning.h` | Skinning extension (not active) â ~400 lines |
| `proxy/d3d9.def` | Export definition |
| `proxy/build.bat` | Build script |
| `proxy/proxy.ini` | Configuration template |

---

## 10. Reverse Engineering Discoveries

### 10.1 TRL Renderer Architecture

TRL's renderer is a large C++ object (Crystal Dynamics engine, "CDC" framework). Key fields discovered:

- **Renderer struct base**: accessed via global pointers
- **+0x480 â +0x4C0**: View matrix (row-major float[16])
- **+0x500**: Projection matrix (row-major float[16])
- **View matrix global**: `0x010FC780`
- **Projection matrix global**: `0x01002530`
- **Far clip global**: `0x010FC910`

### 10.2 Vertex Format Analysis

TRL uses multiple vertex formats identified through vertex declaration parsing:

| Position Type | Usage | Vertex Types |
|--------------|-------|-------------|
| SHORT4 (type 6) | World geometry, environment | Walls, floors, props, architecture |
| FLOAT3 (type 2) | View-space pre-transformed | Hair, eyelashes, foliage, some character parts |
| Skinned (SHORT4 + BLENDWEIGHT + BLENDINDICES) | Animated characters | Lara, NPCs, enemies |

### 10.3 Culling System

TRL implements multiple culling layers:

1. **Frustum culling** (SceneTraversal): rejects entire scene sectors via a distance threshold at `0x00EFDD64`
2. **Backface culling** (SetRenderState): per-triangle rejection cached in globals `0x00F2A0D4-DC`
3. **Light frustum culling** (RenderLights_FrustumCull at `0x0060C7D0`): 6-plane frustum test per light, JNP at `0x0060CE20` rejects failures
4. **Light visibility pre-test** (Light_VisibilityTest at `0x0060B050`): distance/sphere/cone check per light type
5. **Sector light gating** (`0x00EC6337`): JZ skips loading sector light count when visibility flag is zero
6. **Per-frame light count clear** (`0x00603AE6`): cleanup function at `0x603AD0` zeros sector +0x1B0 each frame
7. **RenderLights gate** (`0x0060E3B1`): JE skips entire RenderLights call when sector light count is 0
8. **Render flags bit 20** (`0x010E5384`): skips the post-sector object rendering loop

All 8 layers are neutralized by the proxy.

### 10.4 Light System

Stage lights in the Bolivia level are **not native TRL lights** â they are placed by RTX Remix using geometry hash anchoring. When the geometry they're anchored to is culled by TRL's frustum culler, the lights disappear. This is why disabling ALL culling layers is critical: even one active cull path can remove the geometry that Remix lights are attached to.

### 10.5 DXWrapper Interaction

TRL was originally a D3D8 game. DXWrapper translates D3D8âD3D9. Key discovery: DXWrapper sends ~1296 SetTransform calls per frame with identity View/Proj and combined WVP as World. These must be blocked or they destroy our decomposed transforms. The proxy blocks all external View/Proj/World SetTransform calls once viewProjValid is set.

---

## 11. Known Issues and Limitations

1. **Skinning is disabled** (`ENABLE_SKINNING=0`): Skeletal animation (Lara's body, NPCs) renders in T-pose or with incorrect bone transforms. Enabling requires thorough testing of the bone palette upload path.

2. **View-space geometry detection is heuristic**: FLOAT3 draws are classified as view-space based on the c0-c3 matrix pattern. If a FLOAT3 draw has a non-projection matrix in c0-c3, it will be misclassified.

3. **Memory addresses are hardcoded**: All game memory addresses are for the specific Steam release of trl.exe (13.7 MB, no ASLR). Other versions (GOG, retail disc) may have different addresses.

4. **No morph target support**: Lara's face blend shapes (POSITION[1]) are detected but not specially handled.

5. **UP draw scratch buffer is 1 MB**: DrawPrimitiveUP calls with vertex data exceeding 1 MB will not have their vertex colors neutralized.

---

## 12. Glossary

| Term | Definition |
|------|-----------|
| **FFP** | Fixed-Function Pipeline â the pre-shader D3D rendering mode that Remix natively understands |
| **WVP** | WorldViewProjection â a single 4Ã4 matrix combining all three transforms |
| **VS Constants** | Vertex Shader Constant registers (c0-c255) â GPU registers set by the CPU for shader use |
| **CTAB** | Constant Table â metadata embedded in compiled D3D9 shaders mapping variable names to registers |
| **SHORT4** | 4Ã 16-bit signed integers â a compressed vertex position format requiring shader decode |
| **FLOAT3** | 3Ã 32-bit floats â standard vertex position format that FFP can directly process |
| **D3DCULL_NONE** | DirectX cull mode value 1 â render both front and back faces |
| **BVH** | Bounding Volume Hierarchy â acceleration structure for ray tracing |
| **NOP** | No Operation (0x90 on x86) â instruction that does nothing, used to disable code |
| **Naked thunk** | Assembly function with no compiler-generated prologue/epilogue â used for efficient vtable relay |
| **DXWrapper** | Third-party DLL that translates D3D8 API calls to D3D9 |
| **RTX Remix** | NVIDIA's tool for adding ray tracing to classic D3D8/9 games |
| **Vertex Capture** | Remix feature that captures post-shader vertex positions from the GPU |
| **ReSTIR GI** | Reservoir-based Spatiotemporal Importance Resampling for Global Illumination |
| **NRC** | Neural Radiance Cache â machine learning-based radiance caching |
| **DLSS** | Deep Learning Super Sampling â AI upscaling for real-time rendering |

---

*This document contains all information needed to reproduce the exact build from source. Every game memory address, VS constant register mapping, build flag, and configuration value is specified.*
