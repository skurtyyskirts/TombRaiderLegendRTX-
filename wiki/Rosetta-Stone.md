# Tomb Raider Legend — RTX Remix Rosetta Stone

**The complete reference of every value, address, setting, and decision that makes TRL render under RTX Remix path tracing.**

Working build: `patches/TombRaiderLegend/backups/2026-03-23_stable-skinned-npcs/`

---

## 1. The DLL Chain

```
trl.exe (32-bit D3D8)
  → dxwrapper.dll     D3D8 → D3D9 translation
    → d3d9.dll         THIS PROXY — transform decomposition
      → d3d9_remix.dll   RTX Remix runtime
        → system d3d9     real GPU driver
```

Every draw call passes through this chain top-to-bottom. The proxy sits in the middle and injects decomposed World/View/Projection transforms that Remix needs for path tracing.

---

## 2. Game Memory Addresses

These are hardcoded addresses in the TRL binary (`trl.exe` v1.2, May 18 2006). They point to live game data that the proxy reads each frame.

| Address | Type | Size | What Lives Here | How We Use It |
|---------|------|------|-----------------|---------------|
| `0x010FC780` | `float[16]` | 64 bytes | View matrix (row-major 4×4) | Read every draw to decompose World from WVP |
| `0x01002530` | `float[16]` | 64 bytes | Projection matrix (row-major 4×4) | Read every draw to decompose World from WVP |
| `0x00EFDD64` | `float` | 4 bytes | Frustum culling threshold | **Patched to `1e30`** at startup — disables frustum culling |
| `0x00407150` | code | 1 byte | Frustum cull function entry | **Patched to `0xC3` (ret)** at startup — disables all frustum culling |
| `0x0040EEA7` | code | — | Cull conditional instruction | NOT patched — culling handled via render state + frustum function ret |

### How Addresses Were Found

- **View/Proj matrices:** Live-traced `SetVertexShaderConstantF` calls with `livetools`, cross-referenced CTAB register names, then searched game memory for matching values
- **Frustum threshold:** String search for "frustum"/"cull" in binary, then traced xrefs to find the comparison float
- **Cull conditional:** Static disassembly of the frustum check code path

---

## 3. VS Constant Register Map

TRL uploads shader constants via `SetVertexShaderConstantF`. Each register is a `float4` (16 bytes). The proxy tracks all 256 registers.

| Registers | CTAB Name | Actual Content | Used By Proxy? |
|-----------|-----------|----------------|----------------|
| `c0-c3` | `WorldViewProject` | **Combined WVP matrix** (column-major, 4×4) | **YES** — decompose World from this |
| `c4-c6` | `World` | **Fog/lighting parameters** (NOT a World matrix despite CTAB name) | NO — misleading CTAB label |
| `c8` | — | Written per-frame, purpose varies | NO |
| `c12-c15` | `ViewProject` | VP matrix (some shaders) | Dirty tracking only |
| `c18-c19` | — | Ambient/material color, light direction | NO |
| `c24-c27` | — | Texture transform / UV animation | NO |
| `c28` | — | Per-object position offset (float3) | NO |
| `c48-c95` | `SkinMatrices` | Bone palette (16 bones × 3 regs = 48 regs) | Only if `ENABLE_SKINNING=1` |

### Why c4-c6 Is NOT World

The CTAB (Constant Table) embedded in the shader bytecode labels c4-c6 as "World". Live tracing showed these registers contain fog distance, lighting parameters, and texture scroll values — NOT a 4×4 or 3×4 world matrix. This was confirmed by comparing c4-c6 values across draws: they don't change per-object (a world matrix would).

---

## 4. Transform Decomposition

The proxy's core job. TRL combines World × View × Projection into a single WVP in c0-c3. Remix needs them separated.

### The Math

```
Given:
  WVP (c0-c3, column-major) — from SetVertexShaderConstantF
  View (0x010FC780, row-major) — from game memory
  Proj (0x01002530, row-major) — from game memory

Step 1: Transpose c0-c3 from column-major to row-major → WVP_row
Step 2: VP = View × Proj  (row-major multiply)
Step 3: inv(VP) = cofactor inversion of VP  (cached, see §4.1)
Step 4: World = WVP_row × inv(VP)
Step 5: SetTransform(D3DTS_WORLD, World)
        SetTransform(D3DTS_VIEW, View)
        SetTransform(D3DTS_PROJECTION, Proj)
```

### 4.1 VP Inverse Cache

| Parameter | Value | Why |
|-----------|-------|-----|
| `VP_CHANGE_THRESHOLD` | `1e-4` | If no VP element changes by more than this, reuse the cached inverse. Avoids expensive 4×4 cofactor inversion every draw when camera is stationary. |

### 4.2 World Matrix Quantization

| Parameter | Value | Why |
|-----------|-------|-----|
| `WORLD_QUANT_GRID` | `1e-3` | Snap World matrix elements to this grid. Stabilizes Remix geometry hashes across frames (tiny floating-point drift would create new hashes). |

### 4.3 Dirty Flag Tracking

The proxy only recomputes transforms when VS constants change:

| Trigger | Sets Dirty | Why |
|---------|------------|-----|
| Write to c0-c3 (WVP) | `viewProjDirty=1`, `worldDirty=1` | WVP changed → need new World |
| Write to c4-c7 | `worldDirty=1` | Conservative — in case skinned shader uses World here |
| Write to c12-c15 (VP) | `viewProjDirty=1`, `worldDirty=1` | VP changed → need new inverse |
| Vertex declaration change | `worldDirty=1` | Different draw type may need different decomposition |

---

## 5. Vertex Declarations

TRL uses exactly two vertex formats:

### Format 1: World Geometry (SHORT4)

| Element | Stream | Offset | Type | Usage |
|---------|--------|--------|------|-------|
| POSITION[0] | 0 | 0 | SHORT4 | Encoded position (VS decodes to float) |
| COLOR[0] | 0 | 8 | D3DCOLOR | Vertex color (ARGB 4×ubyte) |
| TEXCOORD[0] | 0 | 12 | SHORT2 | UV coordinates |
| TEXCOORD[1] | 0 | 16 | SHORT2 | Lightmap/secondary UV |

**Stride:** 20 bytes. **~90% of draws.** Requires the game's vertex shader to decode SHORT4 → float3 position.

### Format 2: Characters / UI / Overlays (FLOAT3)

| Element | Stream | Offset | Type | Usage |
|---------|--------|--------|------|-------|
| POSITION[0] | 0 | 0 | FLOAT3 | Direct float position |
| COLOR[0] | 0 | 12 | D3DCOLOR | Vertex color |
| TEXCOORD[0] | 0 | 16 | FLOAT2 | UV coordinates |

**Stride:** 24 bytes. Used for Lara, UI elements, particles, overlays.

### Declaration Flags Tracked

| Field | What It Means | How It's Used |
|-------|---------------|---------------|
| `curDeclPosType` | D3DDECLTYPE of POSITION (2=FLOAT3, 6=SHORT4) | Formerly used by quad filter |
| `curDeclIsSkinned` | Has BLENDWEIGHT + BLENDINDICES | Skinning path (disabled for TRL) |
| `curDeclHasMorph` | Has POSITION[1] | Morph target detection (Lara blend shapes) |
| `curDeclHasPosT` | Has POSITIONT | Pre-transformed screen coords — skip FFP transform |
| `curDeclHasNormal` | Has NORMAL | Rare in TRL — Remix generates normals from geometry |
| `curDeclHasColor` | Has COLOR | Vertex coloring |
| `curDeclHasTexcoord` | Has TEXCOORD[0] | UV mapping |

---

## 6. Draw Call Routing

The proxy intercepts all four D3D9 draw methods:

| Vtable Slot | Method | Status | Why Intercepted |
|-------------|--------|--------|------------------|
| 81 | `DrawPrimitive` | INTERCEPTED | Apply transform overrides |
| 82 | `DrawIndexedPrimitive` | INTERCEPTED | Apply transform overrides |
| 83 | `DrawPrimitiveUP` | INTERCEPTED | dxwrapper routes most D3D8 draws here |
| 84 | `DrawIndexedPrimitiveUP` | INTERCEPTED | dxwrapper routes most D3D8 draws here |

### Draw Routing Logic (per draw call)

```
if (primCount == 0 OR no vertex shader OR no declaration):
    SUPPRESS (return S_OK, don't forward to Remix)
if (viewProjValid AND not POSITIONT):
    TRL_PrepDraw()    → apply SetTransform overrides
    forward draw to Remix
else:
    SUPPRESS (return S_OK, don't forward to Remix)
```

**All draws without valid transform state are suppressed, not passed through.** This prevents Remix from receiving draws that would produce empty vertex position hashes (which triggers a debug assertion in `d3d9_rtx_geometry.cpp:212`).

### Screen-Space Quad Filter

**Status: DISABLED** (always returns 0).

The quad filter was designed to skip fullscreen post-processing passes that occlude path-traced lighting. It compared the WVP matrix against the Projection matrix to detect screen-aligned draws. It was disabled because:

1. The game updates the Proj memory address for each render pass → UI draws matched → all UI skipped
2. dxwrapper routes most post-processing through UP draw calls anyway
3. False positives caused more harm than the quads it was designed to catch

---

## 7. Intercepted Device Methods

### Full List (15 of 119 methods intercepted)

| Slot | Method | What the Proxy Does |
|------|--------|---------------------|
| 0 | QueryInterface | Forward (COM identity) |
| 1 | AddRef | Track refcount + forward |
| 2 | Release | Track refcount, cleanup on zero |
| 16 | Reset | Clear all cached state (shaders, transforms, VP cache) |
| 17 | Present | Frame logging (never called by dxwrapper — see §8) |
| 41 | **BeginScene** | Reset `ffpSetup` and `ffpActive` per scene |
| 42 | **EndScene** | Frame boundary — log summaries, reset draw counters |
| 44 | **SetTransform** | Block dxwrapper identity overrides; allow our own |
| 57 | **SetRenderState** | Force `D3DCULL_NONE` on all cull mode changes |
| 65 | SetTexture | Track current texture per stage |
| 81 | **DrawPrimitive** | Transform override + draw routing |
| 82 | **DrawIndexedPrimitive** | Transform override + draw routing |
| 83 | **DrawPrimitiveUP** | Transform override + draw routing |
| 84 | **DrawIndexedPrimitiveUP** | Transform override + draw routing |
| 87 | **SetVertexDeclaration** | Parse elements, detect skinning/morph/posT, strip non-FLOAT3 normals |
| 89 | **SetFVF** | Strip `D3DFVF_NORMAL` flag — prevents Remix normal format assertion |
| 92 | SetVertexShader | Track current VS, reset ffpActive |
| 94 | **SetVertexShaderConstantF** | Cache constants, dirty tracking, bone upload |
| 100 | SetStreamSource | Track stream VB/offset/stride |
| 107 | SetPixelShader | Track current PS, always forward (passthrough mode) |
| 109 | SetPixelShaderConstantF | Cache PS constants |

All other 104 methods use relay thunks (zero-overhead forwarding).

---

## 8. dxwrapper Behavior

dxwrapper translates D3D8 API calls to D3D9 with these quirks:

| Behavior | Impact | Proxy Workaround |
|----------|--------|------------------|
| Calls `SwapChain::Present` instead of `Device::Present` | `WD_Present` never fires | Frame resets in `BeginScene`/`EndScene` instead |
| Routes most draws through `DrawPrimitiveUP`/`DrawIndexedPrimitiveUP` | Relay thunks would bypass proxy | Intercepted all four draw methods |
| Sends ~1296 `SetTransform` calls/frame with identity View/Proj | Stomps decomposed W/V/P | Blocked all external V/P/W once `viewProjValid=1` |
| Multiple `BeginScene`/`EndScene` pairs per frame | Counter resets mid-frame | Counters accumulate, reset in `EndScene` |

---

## 9. Render State Overrides

| State | Forced Value | Why |
|-------|--------------|----- |
| `D3DRS_CULLMODE` | `1` (D3DCULL_NONE) | Remix path tracing needs all faces visible — rays come from any direction |

### Game Memory Patches (applied once at device creation)

| Address | Original | Patched To | Why |
|---------|----------|------------|-----|
| `0x00EFDD64` | Game's frustum threshold | `1e30f` | Disables distance-based culling |
| `0x00407150` | `55 8B EC 83` (function prologue) | `C3` (ret) | Disables ALL frustum culling — the function tests objects against frustum planes and marks invisible ones for skipping. Bare `ret` is safe (cdecl, caller cleanup). |

---

## 10. FFP State Setup

These states are configured when the proxy first sets up FFP mode (for Remix compatibility):

### Lighting
- `D3DRS_LIGHTING = 0` (disabled — Remix handles lighting via path tracing)
- Material: Diffuse=white, Ambient=white, Specular=0, Emissive=0, Power=0

### Texture Stages
- **Stage 0:** `COLOROP=MODULATE, ARG1=TEXTURE, ARG2=CURRENT, ALPHAOP=MODULATE, ARG1=TEXTURE, ARG2=DIFFUSE`
- **Stages 1-7:** `COLOROP=DISABLE, ALPHAOP=DISABLE` (prevent shadow maps, LUTs, normal maps from leaking into FFP)

### AlbedoStage
- Default: `0` (configured via `proxy.ini [FFP] AlbedoStage`)
- This is which texture stage holds the main diffuse/albedo texture that Remix should use for material replacement

---

## 11. RTX Remix Configuration

### rtx.conf

| Setting | Value | What It Does |
|---------|-------|--------------|
| `rtx.enableRaytracing` | `True` | Master switch for path tracing |
| `rtx.useVertexCapture` | `True` | Capture post-shader vertex positions. **Critical** — TRL's SHORT4 positions are decoded by the VS; Remix reads the decoded output |
| `rtx.fusedWorldViewMode` | `0` | Treat W/V/P as three separate matrices (not fused). Our proxy provides all three via SetTransform |
| `rtx.sceneScale` | `0.0001` | 1 game unit = 10,000 Remix units. TRL uses large world coordinates (~30,000 range) |
| `rtx.zUp` | `True` | Z-axis points up (Direct3D convention) |
| `rtx.fallbackLightMode` | `1` | Use directional fallback light (no game light data available) |
| `rtx.fallbackLightDirection` | `-70.5, 1.5, -326.9` | Direction of the fallback directional light |
| `rtx.fallbackLightRadiance` | `5, 5, 5` | Light intensity (R, G, B) |
| `rtx.skyBoxTextures` | `0x443B45FB9971FC90, 0x78AD1D0EDA0FFC21, 0x8405ADDE0AE29A5F` | Texture hashes Remix treats as sky (3 sky textures identified) |
| `rtx.terrain.terrainAsDecalsAllowOverModulate` | `False` | Disable terrain decal overmodulation |
| `rtx.terrain.terrainAsDecalsEnabledIfNoBaker` | `True` | Enable terrain decals without baking |
| `rtx.terrainBaker.enableBaking` | `False` | Disable terrain baking (not needed for TRL) |
| `rtx.geometryAssetHashRuleString` | `"indices,texcoords,geometrydescriptor"` | **Critical for hash stability.** Excludes positions from the asset hash. See §16 |
| `rtx.uiTextures` | `0x03016D2FBBF5C65D, 0x2164293A60D148AC` | Texture hashes Remix treats as UI |

### user.conf

| Setting | Value | What It Does |
|---------|-------|--------------|
| `rtx.defaultToAdvancedUI` | `True` | Show advanced Remix developer UI |
| `rtx.graphicsPreset` | `0` | Custom graphics preset (manual tuning) |

### dxwrapper.ini

| Setting | Value | What It Does |
|---------|-------|--------------|
| `D3d8to9` | `1` | **The only non-default value.** Enables D3D8→D3D9 translation for TRL |

### dxvk.conf

All values are at defaults (comments only, no active settings).

---

## 12. Proxy Build Configuration

### Compiler Settings (build.bat)

| Setting | Value | Why |
|---------|-------|-----|
| Compiler | MSVC x86 (cl.exe) | Must be 32-bit to match trl.exe |
| Optimization | `/O1` (size) | Smallest DLL |
| Intrinsics | `/Oi` (d3d9_device.c only) | Compiler-generated memcpy |
| Security | `/GS-` | No buffer security checks (no CRT) |
| CRT | `/Zl` + `/NODEFAULTLIB` | No C runtime — DLL is self-contained |
| Entry point | `_DllMainCRTStartup@12` | Standard DLL entry |
| Linker | `kernel32.lib` only | Only Windows API dependency |
| Output | `d3d9.dll` (20KB) | Drop-in D3D9 replacement |

### Proxy INI (proxy.ini)

```ini
[Remix]
Enabled=1
DLLName=d3d9_remix.dll

[Chain]
PreloadDLL=

[FFP]
AlbedoStage=0
```

---

## 13. Diagnostic System

### Logging

| Parameter | Value | What It Controls |
|-----------|-------|------------------|
| `DIAG_ENABLED` | `1` | Master switch for diagnostic logging |
| `DIAG_DELAY_MS` | `50000` (50 sec) | Wait before starting detailed frame logs (lets proxy stabilize) |
| `DIAG_LOG_FRAMES` | `3` | Number of detailed frames to log |
| Log file | `ffp_proxy.log` | Written to game directory |

### What Gets Logged

| Event | Detail Level | Condition |
|-------|-------------|----------|
| Device creation | Always | — |
| Matrix verification | Once | First `TRL_ApplyTransformOverrides` call |
| Vertex declarations | Once per unique decl | During DIAG_ACTIVE window |
| VS constant writes | All | During DIAG_ACTIVE window |
| Draw calls (DIP/DP) | First 200 per scene | During DIAG_ACTIVE window |
| Vertex raw bytes | First 10 draws | During DIAG_ACTIVE window |
| VS constant matrices | First 5 draws | During DIAG_ACTIVE window |
| Frame/scene summaries | Every 120 scenes | Always (up to 20 summaries) |
| BeginScene count | Always | During DIAG_ACTIVE window |

### Frame Summary Counters

| Counter | What It Counts |
|---------|----------------|
| `total` | All draw calls (DIP + DP + DPUP + DIPUP) |
| `processed` | Draws that went through `TRL_PrepDraw` (transform overrides applied) |
| `skippedQuad` | Draws caught by quad filter (should be 0 — filter disabled) |
| `passthrough` | Draws before `viewProjValid` or with POSITIONT |
| `xformBlocked` | External `SetTransform` calls blocked (from dxwrapper) |
| `vpValid` | Whether VP has been initialized (1 after first c0-c3 write) |

---

## 14. Skinning System (Disabled)

Included but disabled for TRL (`ENABLE_SKINNING=0`). Documented here for reference.

| Parameter | Value | Purpose |
|-----------|-------|---------|
| `ENABLE_SKINNING` | `0` | Master switch |
| `EXPAND_SKIN_VERTICES` | `0` | Vertex expansion to fixed 48-byte layout |
| `VS_REG_BONE_THRESHOLD` | `48` | Bone matrices start at c48 |
| `VS_REGS_PER_BONE` | `3` | 3 registers per bone (4×3 packed) |
| `MAX_FFP_BONES` | `48` | Maximum FFP bone count |
| `SKIN_VTX_SIZE` | `48` | Expanded vertex size (bytes) |
| `SKIN_CACHE_SIZE` | `64` | Vertex buffer cache slots |

TRL uses morph targets (POSITION[1] blend shapes) for character animation instead of bone-weighted skinning, so the skinning system is unnecessary.

---

## 15. Normal Stripping

Remix's game capturer asserts that vertex normals are `VK_FORMAT_R32G32B32_SFLOAT` (D3D9 `FLOAT3`). TRL uses `SHORT4N` and `DEC3N` normals in some vertex declarations. The proxy strips these at two levels:

### Vertex Declaration Stripping

`SetVertexDeclaration` (slot 87) detects non-FLOAT3 NORMAL elements and creates a **modified declaration** with NORMAL removed. The modified declarations are cached (up to 64) and released on Reset/Release. The vertex buffer data is unchanged — only the declaration seen by Remix omits the normal element. Remix computes smooth normals via path tracing, so input normals aren't needed.

### FVF Normal Stripping

`SetFVF` (slot 89) strips the `D3DFVF_NORMAL` flag. dxwrapper's D3D8→D3D9 conversion may use SetFVF instead of SetVertexDeclaration for some draws.

### Vertex Color Neutralization (UP Draws)

TRL bakes per-vertex lighting as `D3DCOLOR` in every vertex. These change with camera/player position. For UP draws (where vertex data is inline), changing colors change the hash because Remix hashes the raw bytes. The proxy copies UP vertex data to a scratch buffer and sets all `COLOR[0]` to white (`0xFFFFFFFF`), stabilizing the hash while Remix handles lighting via path tracing.

---

## 16. Hash Stability Architecture

**This is the most critical section for understanding how material/decal assignments persist.**

### The Problem

With `rtx.useVertexCapture=True`, Remix captures post-vertex-shader positions. These are **clip-space** coordinates (after World × View × Projection transform). When the camera moves, View changes, clip-space positions change, and any hash that includes positions becomes unstable.

Remix uses two separate hashes:

| Hash Type | Purpose | Default Components |
|-----------|---------|-------------------|
| **Generation hash** | Internal frame-to-frame geometry tracking, deduplication | positions, indices, texcoords, vertexlayout, vertexshader, geometrydescriptor |
| **Asset hash** | Material/decal/light persistence, USD replacements | positions, indices, texcoords, geometrydescriptor |

Both include positions by default → both are unstable with vertex capture.

### The Solution

```
rtx.geometryAssetHashRuleString = "indices,texcoords,geometrydescriptor"
```

**Exclude positions from the asset hash only.** The generation hash keeps its default (includes positions).

| Hash | Includes Positions? | Stable? | Effect |
|------|-------------------|---------|--------|
| Generation | Yes (default) | No — changes with camera | Debug view colors flash when camera moves. This is cosmetic. |
| Asset | **No** (custom rule) | **Yes** — camera-invariant | Material assignments, decal hashes, placed lights persist across camera movement. |

### Why Not Exclude Positions From Both?

Excluding positions from the `geometryGenerationHashRuleString` triggers a debug assertion in RTX Remix:

```
d3d9_rtx_geometry.cpp:212
hashes[HashComponents::VertexPosition] != kEmptyHash
```

When positions are excluded from the generation hash rule, Remix's debug build skips computing the position hash component, leaving it as `kEmptyHash`. An internal assertion then checks that all components are non-empty regardless of the rule. This is a Remix debug build behavior — release builds would not assert.

### What's Stable Now

| Geometry Type | Asset Hash Stable? | Why |
|---------------|-------------------|-----|
| **Lara (skinned NPC)** | **Yes** | Indices + texcoords + geometry descriptor don't change with camera |
| **Other NPCs** | **Yes** | Same reason |
| **World geometry** | **Partially** | Static meshes are mostly stable. Some dynamic geometry (water, particles) may still shift due to vertex color changes. |
| **Decals (e.g. dirt on Lara)** | **Yes** | Asset hash `B0E9715056328D5E` persists across camera movement |

### What the Debug View Shows

The debug geometry hash visualization (colored overlays) shows the **generation hash**, which includes positions and WILL flash/change when the camera moves. This is expected and does not affect material persistence. To verify material stability, assign a material or decal and confirm it persists after moving the camera.

### Supporting Proxy Features

The proxy includes several features that work together with the hash rule for stability:

1. **World matrix quantization** (`WORLD_QUANT_GRID = 1e-3`): Snaps decomposed World matrix elements to a grid, preventing floating-point drift from creating new hashes frame-to-frame.

2. **Draw suppression guards**: All draw calls are checked for valid state (vertex shader, declaration, transforms) before forwarding to Remix. Draws without proper state are suppressed (return `S_OK`), preventing empty position hashes.

3. **Vertex color neutralization**: UP draw vertex colors are set to white to prevent per-vertex lighting changes from affecting hashes.

4. **VP inverse caching**: The View × Projection inverse is cached and only recomputed when the camera moves more than `VP_CHANGE_THRESHOLD` (1e-4), preventing unnecessary World matrix recalculation.

---

## 17. Known Limitations

| Issue | Status | Workaround |
|-------|--------|------------|
| **World geometry generation hashes shift with camera** | Open | Asset hash is stable for material persistence. Generation hash instability is cosmetic in debug view. Next step: investigate vertex expansion (SHORT4→FLOAT3 in proxy) to stabilize generation hash. |
| Lara hair/eyelash textures can't be selected in Remix | Open | Need to identify texture hashes via Remix dev tools |
| VP inverse may be slightly stale if game updates matrices mid-draw-sequence | Mitigated | Threshold of 1e-4 catches most changes; vertex capture provides correct final positions |
| Post-processing bloom/HDR quads may leak through | Acceptable | Quad filter disabled; Remix handles most post-processing automatically |
| Lara outline ghost in freecam | Open | Game silhouette shader effect — needs texture hash filtering in Remix |
| `smoothNormalsTextures` disabled | Blocked by Remix debug assert | Re-enable after switching to a release build of Remix |
| Generation hash flashes in debug view | Expected | Uses clip-space positions from vertex capture. Does not affect material persistence (uses asset hash). |
