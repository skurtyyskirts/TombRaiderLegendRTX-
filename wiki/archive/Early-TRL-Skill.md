---
name: trl-rtx-remix
description: Tomb Raider Legend RTX Remix compatibility. Use when working on making Tomb Raider Legend (2006) render correctly under NVIDIA RTX Remix path tracing. Covers the full stack — dxwrapper D3D8→D3D9 translation, FFP proxy conversion, camera/matrix recovery, draw call routing, 15-layer culling system, light visibility pipeline, geometry hash stability, automated test pipeline, and Remix configuration. Builds on @dx9-ffp-port and @dynamic-analysis skills.
---

# Tomb Raider Legend — RTX Remix Compatibility

Make Tomb Raider Legend (TRL, 2006) render correctly under NVIDIA RTX Remix path tracing. This skill covers TRL-specific architecture, known pitfalls, and validated solutions. For general FFP proxy mechanics see @dx9-ffp-port; for live analysis see @dynamic-analysis.

---

## TRL Runtime Stack

```
trl.exe (D3D8 API calls)
  → dxwrapper.dll (D3D8 → D3D9 translation)
    → d3d9.dll (FFP proxy — our code)
      → d3d9_remix.dll (RTX Remix bridge/runtime)
```

**Critical fact**: TRL is natively Direct3D 9.0c internally (SM 2.0/3.0, DX9.0c system requirement), but it presents a D3D8 interface externally. The game uses dxwrapper (elishacloud) for D3D8→D3D9 translation. No crosire d3d8to9 wrapper is needed. The dxwrapper passthrough is already in the deployment pipeline.

The FFP proxy (`d3d9.dll`) sits between dxwrapper and Remix. It intercepts the D3D9 device that dxwrapper creates, converts shader-based draws to FFP, and chain-loads `d3d9_remix.dll`.

### Deployment Layout (Game Directory)

```
A:\SteamLibrary\steamapps\common\Tomb Raider Legend\
  trl.exe
  dxwrapper.dll          ← D3D8→D3D9 translation
  dxwrapper.ini          ← dxwrapper config
  d3d9.dll               ← FFP proxy (built from patches/TombRaiderLegend/proxy/)
  d3d9_remix.dll         ← RTX Remix runtime (renamed from Remix's d3d9.dll)
  proxy.ini              ← FFP proxy config
  rtx.conf               ← Remix runtime config
  .trex/                 ← Remix mod folder (USD replacements, rtx.conf overrides)
```

---

## Current Project Status (Build 030)

### Solved

| Component | Status | Build |
|-----------|--------|-------|
| FFP proxy builds and chains to Remix | DONE | 001 |
| Transform pipeline (View/Proj/World from game memory) | DONE | 001 |
| Asset hash stability (static camera) | DONE | 002 |
| Automated two-phase test pipeline | DONE | 018 |
| Input delivery to DirectInput game (scancode fix) | DONE | 018 |
| All geometry culling disabled (15 layers mapped) | DONE | 029 |
| Draw counts ~94K-190K per window (full scene) | DONE | 028 |

### Remaining Blockers

| Problem | Root Cause | Fix |
|---------|-----------|-----|
| Lights disappear when Lara moves away from stage | `Light_VisibilityTest` at `0x60B050` — unpatched pre-frustum distance/sphere/cone gate | Patch with `mov al, 1; ret 4` (5 bytes: `B0 01 C2 04 00`) |
| Asset hash instability on movement | Hashes shift/flicker when Lara moves — root cause not yet confirmed | Investigate after light visibility fix |
| Remix light anchors don't hold on movement | Consequence of above — geometry hashes for light-bearing surfaces never reach Remix | Fix the light visibility gate first |

---

## Camera and Matrix Recovery (SOLVED)

### Architecture

The proxy reads View and Projection matrices directly from game memory, not from `SetVertexShaderConstantF` intercepts:

| Source | Address | What |
|--------|---------|------|
| View matrix | `0x010FC780` | 4×4 float, read every frame |
| Projection matrix | `0x01002530` | 4×4 float, read every frame |
| World matrix | Decomposed from WVP | `World = WVP × (VP)⁻¹` |

The proxy computes `VP = View × Proj`, inverts it, then for each draw: `World = WVP × VP⁻¹` where WVP comes from VS constant registers c0-c3.

### VS Constant Register Map

| Register | Purpose |
|----------|---------|
| c0-c3 | World matrix (transposed) / WorldViewProjection |
| c8-c15 | ViewProjection (two 4×4) |
| c16+ | Bone/skin matrices |
| c28 | Scalar parameter |
| c39 | Utility `{2.0, 0.5, 0.0, 1.0}` |

### Matrix Validation

The proxy validates `vpValid=1` every scene (View and Proj non-zero, non-identity). All 30 builds show 100% vpValid with zero passthrough, zero skipped, zero xformBlocked draws.

### Remix Camera Settings

```ini
rtx.fusedWorldViewMode = 0          # Separate W, V, P (proxy provides all three)
```

---

## Culling System — Complete 15-Layer Map

TRL has 15 discovered culling mechanisms. 11 are patched, 1 is identified but unpatched (the current blocker), 3 are unexplored.

### Patched Layers (Working)

| # | Layer | Address(es) | Patch | Build |
|---|-------|-------------|-------|-------|
| 1 | Frustum distance threshold | `0xEFDD64` | Stamp to `-1e30f` per BeginScene | 016 |
| 2 | Per-object frustum function | `0x407150` | `ret` at entry (0xC3 over 0x55) | 016 |
| 3 | Scene traversal cull jumps (7×) | `0x4072BD`, `0x4072D2`, `0x407AF1`, `0x407B30`, `0x407B49`, `0x407B62`, `0x407B7B` | 6-byte NOP each | 016 |
| 4 | D3D backface culling | `SetRenderState(D3DRS_CULLMODE)` | Force `D3DCULL_NONE` | 016 |
| 5 | Cull mode globals | `0xF2A0D4`, `0xF2A0D8`, `0xF2A0DC` | Stamp to `D3DCULL_NONE` per scene | 029 |
| 6 | Sector/portal visibility | `0x46C194`, `0x46C19D` | JE + JNE NOPed (6 bytes each) | 028 |
| 7 | Light frustum 6-plane test | `0x60CE20` | JNP NOPed (6 bytes) | 024 |
| 8 | Light broad-visibility test | `0x60CDE2` | NOPed | 024 |
| 9 | Pending-render flags | `0x603832`, `0x60E30D` | NOPed (no effect — proved bottleneck is elsewhere) | 025 |
| 10 | Light visibility state | 5 addresses in `LightVolume_UpdateVisibility` | Attempted but NOT confirmed in proxy log | 026 |

### Unpatched — Current Blocker

| # | Layer | Address | What It Does | Fix |
|---|-------|---------|-------------|-----|
| **11** | **Light_VisibilityTest** | **`0x60B050`** | Pre-frustum per-light distance/sphere/cone gate. Runs BEFORE the frustum test. For light types 0 and 1, rejects lights that are "too far." | `mov al, 1; ret 4` → bytes `B0 01 C2 04 00` |

### Unexplored

| # | Layer | Address | Notes |
|---|-------|---------|-------|
| 12 | Sector light list population | `[param+0x1B0]` count / `[param+0x1B8]` array | If patching layer 11 doesn't fix lights, the sector system may not populate all lights into the iteration list |
| 13 | LOD alpha fade | `0x446580` | 10 callers, may fade geometry invisible at distance |
| 14 | Scene graph sector early-outs | Unknown | May be covered by layer 6 |
| 15 | Light Draw virtual method | `vtable[0x18]` per light | Internal culling inside light's own Draw method |

### Light Pipeline (Call Chain)

```
Sector light list ([param+0x1B0] count, [param+0x1B8] array)   ← Layer 12 (unexplored)
  └→ Light_VisibilityTest (0x60B050)                            ← Layer 11 (UNPATCHED — BLOCKS LIGHTS)
       └→ Frustum 6-plane test (0x60CE20)                       ← Layer 7 (patched, NOP)
            └→ Light Draw vtable[0x18] (call at 0x60CE42)       ← Layer 15 (unexplored)
```

### Engine Debug Toggle

String at `0xEFF384`: `"Disable extra static light culling and fading"` — engine has a debug config flag. If per-light patches fail, find and activate this flag.

---

## Draw Call Routing

### Vertex Declaration Patterns

| Format | Stride | Elements | Route |
|--------|--------|----------|-------|
| Rigid world geometry | ~24 bytes | POSITION + NORMAL + TEXCOORD | FFP convert |
| Skinned characters | ~32+ bytes | POSITION + BLENDWEIGHT + BLENDINDICES + NORMAL + TEXCOORD | Shader passthrough (skinning off) |
| HUD / UI | varies | POSITION (no NORMAL), or POSITIONT | Shader passthrough |
| Fullscreen quads | varies | POSITIONT or large-triangle POSITION | Ignore / tag as UI |
| Particles | varies | POSITION + TEXCOORD (no NORMAL, non-indexed) | Evaluate: FFP or passthrough |

### Routing Logic

All draws go through FFP conversion. Zero passthrough in production builds. The proxy's routing:

1. `viewProjValid` must be true (always is)
2. No NORMAL filter — TRL world geometry always has NORMAL
3. Fullscreen quads detected and skipped via POSITIONT / vertex count
4. Post-process passes: add texture hashes to `rtx.ignoreTextures`

---

## Geometry Hash Stability

Static camera hashes are stable. Movement-induced hash instability is still an open problem.

### Hash Rule

```ini
# In rtx.conf or .trex/rtx.conf
rtx.geometryHashGenerationHashIndexer = True
rtx.geometryHashGenerationHashTexCoord = True
rtx.geometryHashGenerationHashGeometryDescriptor = True
```

Rule: `indices,texcoords,geometrydescriptor` — stable across frames, sessions, camera movement.

### Why Static Hashes Are Stable

- TRL skinning is GPU-side (VS constants), vertex buffers are static
- No LOD switching observed in test area

### Why Movement Hashes Are Unstable (Open Investigation)

- World matrix may still contain camera-dependent data at certain positions
- Culling patches change which geometry subsets are submitted per frame — VB content may vary
- Hash color shifts observed in builds 017, 022 — initially attributed to proxy bugs but not conclusively resolved

### Remix Hashing Settings

```ini
rtx.calculateMeshBoundingBox = True
rtx.antiCulling.object.hashInstanceWithBoundingBoxHash = False
```

---

## Stage Lights — The Remaining Problem

The red and green stage lights in the Bolivia test level are **Remix-placed lights anchored to geometry hashes**, not native game lights. They disappear because:

1. TRL's engine culls the geometry those hashes are attached to
2. When geometry is culled, Remix loses the hash anchor
3. The Remix-placed light disappears

The fix chain: disable `Light_VisibilityTest` → geometry stays submitted → hash anchor survives → Remix light stays visible.

**If `Light_VisibilityTest` patch doesn't work**, the fallback investigation order:
1. Check proxy log — was patch actually applied? (VirtualProtect can silently fail)
2. Do lights appear briefly then vanish? → Light Draw method internal culling (vtable[0x18])
3. Do lights never appear at far positions? → Sector light list not populated upstream
4. Try the engine debug toggle at `0xEFF384`

---

## Automated Test Pipeline

### Commands

```bash
python patches/TombRaiderLegend/run.py test --build --randomize   # Full pipeline: build + deploy + test
python patches/TombRaiderLegend/run.py build                      # Build proxy only
python patches/TombRaiderLegend/run.py deploy                     # Deploy to game dir
python patches/TombRaiderLegend/run.py test                       # Test only (no build)
```

### Two-Phase Test

1. **Phase 1 — Hash Debug**: RTX Remix debug view 277 (Geometry/Asset Hash). Each geometry piece gets a color. Same geometry must keep same color across 3 screenshots at different positions.
2. **Phase 2 — Clean Render**: Normal path-traced render. Both red AND green stage lights must be visible in ALL 3 screenshots. Lights must shift position as Lara moves.

### Success Criteria

- Hash debug: same geometry keeps same color across all 3 positions
- Clean render: both red AND green lights visible in ALL 3 screenshots
- Lights must shift left/right relative to Lara (proves actual movement)
- Same position across screenshots = false positive (macro failed)

### Known False Positive Patterns

| Issue | Builds Affected | Fix |
|-------|----------------|-----|
| Input not reaching game (no `KEYEVENTF_SCANCODE`) | 001-016 | Scancode flag added (build 018) |
| Wrong screenshots evaluated | 019-020 | Fixed screenshot selection |
| Patches in wrong source file | 023 | Always edit `patches/TombRaiderLegend/proxy/d3d9_device.c` |
| Lara walks past stage area | 022, 027, 029 | Randomized movement with shorter bounds |
| VirtualProtect silent failures | 026 | Always check proxy log for patch confirmation |

### Game Launch Rules

- Never touch/focus the game window programmatically
- Wait 20 seconds after launch before any input
- Accept the setup dialog (resolution, etc.) before gameplay

---

## Remix Configuration

### rtx.conf — Key Settings for TRL

```ini
# Camera / transform
rtx.fusedWorldViewMode = 0          # 0=separate W,V,P (proxy provides all three)

# Anti-culling (supplements game-side patches, does not replace them)
rtx.antiCulling.object.enable = True
rtx.antiCulling.object.fovScale = 2.0
rtx.antiCulling.object.farPlaneScale = 10.0
rtx.antiCulling.object.numObjectsToKeep = 10000
rtx.antiCulling.object.enableHighPrecisionAntiCulling = True
rtx.antiCulling.object.enableInfinityFarPlane = True

# Hashing
rtx.calculateMeshBoundingBox = True
rtx.antiCulling.object.hashInstanceWithBoundingBoxHash = False

# Texture categorization (populate with actual hashes)
rtx.ignoreTextures =                 # post-process / fullscreen quad textures
rtx.uiTextures =                     # HUD element textures
```

### proxy.ini

```ini
[Remix]
Enabled=1
DLL=d3d9_remix.dll

[FFP]
AlbedoStage=0
```

---

## Engine Internals — Key Addresses

### Globals

| Address | Name | Notes |
|---------|------|-------|
| `0x01392E18` | `g_pEngineRoot` | Root engine object |
| `0x010FC780` | View matrix source | 4×4 float, read by proxy |
| `0x01002530` | Proj matrix source | 4×4 float, read by proxy |
| `0xEFDD64` | Frustum threshold | Original `16.0f`, stamped to `-1e30f` |
| `0xF2A0D4/D8/DC` | Cull mode globals | Stamped to `D3DCULL_NONE` |
| `0xEFD404/0xEFD40C` | Screen boundary min/max | Used by boundary cull checks |

### Renderer Chain

```
g_pEngineRoot (+0x214) → TRLRenderer* (+0x0C) → IDirect3DDevice9*
```

### Key Functions

| Address | Name | Role |
|---------|------|------|
| `0x00ECBA40` | SetVSConstF helper | Wrapper for `SetVertexShaderConstantF` |
| `0x00ECBB00` | SetVSConstF helper | Alternate upload path |
| `0x0060C7D0` | `RenderLights_FrustumCull` | Light frustum culling + draw dispatch |
| `0x0060B050` | `Light_VisibilityTest` | Per-light visibility gate (THE BLOCKER) |
| `0x0060EBF0` | Render caller B | VS constant upload path |
| `0x00610850` | Render caller C | VS constant upload path |
| `0x00407150` | `SceneTraversal_CullAndSubmit` | Frustum cull function (patched to RET) |
| `0x0046C194` | Sector visibility check | Portal/sector gate (patched to NOP) |

---

## Files and Working State

| Path | Purpose |
|------|---------|
| `patches/TombRaiderLegend/proxy/d3d9_device.c` | Active FFP proxy source (**always edit this one**) |
| `patches/TombRaiderLegend/proxy/d3d9_main.c` | DLL entry, logging, Remix chain-loading |
| `patches/TombRaiderLegend/proxy/d3d9_wrapper.c` | IDirect3D9 wrapper |
| `patches/TombRaiderLegend/proxy/proxy.ini` | Proxy runtime config |
| `patches/TombRaiderLegend/kb.h` | Knowledge base: functions, structs, register layout |
| `patches/TombRaiderLegend/run.py` | Automated build + test pipeline |
| `patches/TombRaiderLegend/findings.md` | Static analysis findings (appended by subagents) |
| `TRL tests/WHITEBOARD.md` | Results whiteboard — all builds, hypotheses, next steps |

---

## Decision Tree: What To Do Next

```
Patch Light_VisibilityTest (0x60B050) with B0 01 C2 04 00
├── Lights now stable at all positions → MIRACLE BUILD — ship it
└── Lights still disappear
    ├── Check proxy log: was patch applied?
    │   └── No → VirtualProtect failed, fix address/permissions
    ├── Do lights appear briefly then vanish?
    │   └── Yes → Light Draw method internal culling (vtable[0x18])
    ├── Do lights never appear at far positions?
    │   └── Yes → Sector light list not populated upstream
    │       ├── Try engine debug toggle: "Disable extra static light culling and fading" (0xEFF384)
    │       └── RE the sector light list builder to force all lights
    └── Lights appear but wrong color/position
        └── Remix anchor hash mismatch — check hash stability at new position
```

---

## Grid Artifacts at Mesh Seams

Visible wireframe-like lines at mesh boundaries under path tracing. Caused by NRD denoiser + normal discontinuities.

### Fixes

- **USD mesh replacements**: replace meshes with properly welded geometry sharing vertex normals at seams
- **Denoiser tuning**: loosen normal thresholds in Remix Rendering → Denoising
- **Backface culling for secondary rays**: `rtx.enableBackfaceCulling = True`

---

## SHORT4 Vertex Decompression

If TRL uses `D3DDECLTYPE_SHORT4` for compressed vertex positions, the proxy must decompress to `D3DDECLTYPE_FLOATn` before FFP submission. Check vertex element types in `ffp_proxy.log` or via `dx9tracer analyze --vtx-formats`. Not currently observed in TRL but documented for completeness.
