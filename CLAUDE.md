# CLAUDE.md — TombRaiderLegendRTX

## Identity
- **Game:** Tomb Raider: Legend (2006, Crystal Dynamics, cdcEngine, Steam PC, 32-bit x86)
- **Project:** Vibe Reverse Engineering toolkit — D3D9 FFP proxy DLL + RE tools for RTX Remix compatibility
- **Repo:** github.com/skurtyyskirts/TombRaiderLegendRTX-
- **Owner:** Jeffrey (skurtyyskirts), Temple TX
- **Builds completed:** 001–044 (116 commits)

## DLL Chain
```
NvRemixLauncher32.exe → trl.exe → dxwrapper.dll → d3d9.dll (FFP proxy) → d3d9_remix.dll
```

## Architecture Summary
TRL renders exclusively through programmable vertex shaders. RTX Remix requires Fixed-Function Pipeline (FFP). The proxy intercepts D3D9 calls, reconstructs W/V/P matrices from VS constants, and feeds them to Remix through FFP calls — so Remix sees TRL as a native FFP game. The proxy also patches 22 identified culling layers at runtime via `VirtualProtect` + memory write.

### VS Constant Register Layout (TRL-specific)
```
c0–c3:   World matrix (transposed)
c8–c11:  View matrix
c12–c15: Projection matrix
c48+:    Skinning bone matrices (3 regs/bone)
```
Note: View and Projection are SEPARATE registers, not a fused ViewProj.

### Proxy Method Hooks
| Method | What it does ||--------|-------------|
| `SetVertexShaderConstantF` | Captures VS constants into per-draw register bank |
| `DrawIndexedPrimitive` | Reconstructs W/V/P matrices, calls `SetTransform`, chains to Remix |
| `SetRenderState` | Intercepts `D3DRS_CULLMODE` — forces `D3DCULL_NONE` |
| `BeginScene` | Stamps anti-culling globals (frustum threshold, cull mode, far clip) |
| `Present` | Logs diagnostics every 120 frames |

### Matrix Recovery Addresses
- View matrix: `0x010FC780` (global, read-only)
- Projection matrix: `0x01002530` (global, read-only)
- Engine root: `0x01392E18` (`g_pEngineRoot`)
- Renderer chain: `g_pEngineRoot (+0x214) → TRLRenderer* (+0x0C) → IDirect3DDevice9*`

## rtx.conf (Actual)
```ini
rtx.zUp = True
rtx.useVertexCapture = True
rtx.fusedWorldViewMode = 0
rtx.sceneScale = 0.0001
rtx.enableRaytracing = True
rtx.fallbackLightMode = 1
rtx.fallbackLightRadiance = 5, 5, 5
rtx.fallbackLightDirection = -70.5, 1.5, -326.9
rtx.geometryAssetHashRuleString = "indices,texcoords,geometrydescriptor"
rtx.terrainBaker.enableBaking = False
rtx.terrain.terrainAsDecalsEnabledIfNoBaker = True
rtx.terrain.terrainAsDecalsAllowOverModulate = False
rtx.remixMenuKeyBinds = X
rtx.skyBoxTextures = 0x443B45FB9971FC90, 0x78AD1D0EDA0FFC21, 0x8405ADDE0AE29A5F
rtx.uiTextures = 0x03016D2FBBF5C65D, 0x2164293A60D148AC
```
### Hash Rule Rationale
- Asset hash: `indices,texcoords,geometrydescriptor` — excludes clip-space positions for stable material/replacement assignments
- Generation hash: must include positions (Remix hard-crashes with "Position hash should never be empty" otherwise)
- Generation hash includes positions and flickers with camera movement
- **Asset hash stability has NOT been verified** — no Toolkit mesh replacements have been tested to confirm hashes are truly stable

## Current Status

### DONE
- FFP proxy DLL builds and chains to Remix
- Transform pipeline (View/Proj/World from VS constants)
- Asset hash stability (static + moving camera) — **UNRESOLVED** (incorrectly marked as resolved; debug geometry view always shows changing hash colors; never verified with actual Toolkit mesh replacements)
- Automated test pipeline (two-phase: hash debug + clean render, randomized movement)
- All 22 identified culling layers investigated; 20 patched

### TWO REMAINING BLOCKERS

#### Blocker 1: Anchor Geometry Not Submitted at Distance

**Symptom:** Both stage lights (red + green) vanish when Lara walks away from the stage. Lights are anchored to geometry hashes — when the engine stops submitting anchor geometry as draw calls, Remix loses the anchors and lights disappear.

**Root cause reframed (build 038):** The "red light at distance" in builds 019–037 was actually the fallback light (`rtx.fallbackLightRadiance`). With neutral white fallback, BOTH stage lights gone at distance. Problem is geometry submission, NOT light culling.

**What's been exhausted:**
- All 11 conditional exits in `SceneTraversal_CullAndSubmit` (0x407150) NOPed — draw counts rose to ~190K, still fails
- Sector/portal visibility NOPed (65× draw count increase)
- Light_VisibilityTest force-true, light frustum NOPs, sector light count gate
- Far clip stamped to 1e30f, frustum threshold to -1e30f
- Camera-sector proximity filter NOPed
- All 3 identified render paths patched: (1) RenderVisibleSectors→RenderSector, (2) SceneTraversal wrapper→0x407150, (3) moveable object loop at 0x40E2C0
**PRIME SUSPECT:** `TerrainDrawable (0x40ACF0)` / `TERRAIN_DrawUnits` — a SEPARATE terrain rendering path with its own culling. Never decompiled. Never patched.

**PASS criteria:** Both red and green stage lights visible in all 3 clean render screenshots, lights shift as Lara strafes, hashes stable, no crash.

#### Blocker 2: Hash Instability (UNRESOLVED)

**Symptom:** The geometry debug view always shows changing hash colors. This was incorrectly marked as resolved based on a theory that generation hash flickering is cosmetic and asset hashes are stable — but this was never verified with actual RTX Toolkit mesh replacements.

**What's unverified:**
- No Toolkit mesh replacement has ever been tested to confirm asset hashes are truly stable
- Generation hash includes positions and flickers with camera — assumed cosmetic, never proven
- The claim that `indices,texcoords,geometrydescriptor` produces stable asset hashes has not been validated end-to-end

## 22-Layer Culling Map

| # | Layer | Address(es) | Patched? | Build |
|---|-------|------------|----------|-------|
| 1 | Frustum distance threshold | 0xEFDD64 | Yes — -1e30f per BeginScene | 016 |
| 2 | Per-object frustum function | 0x407150 | Yes — RET at entry | 016 |
| 3 | Scene traversal cull jumps (7×) | 0x4072BD, 0x4072D2, 0x407AF1, 0x407B30, 0x407B49, 0x407B62, 0x407B7B | Yes — all NOPed | 016 |
| 4 | D3D backface culling | SetRenderState | Yes — D3DCULL_NONE | 016 |
| 5 | Cull mode globals | 0xF2A0D4/D8/DC | Yes — stamped per scene | 029 |
| 6 | Sector/portal visibility | 0x46C194, 0x46C19D | Yes — both NOPed | 028 |
| 7 | Light frustum 6-plane test | 0x60CE20 | Yes — NOPed | 024 |
| 8 | Light broad-visibility test | 0x60CDE2 | Yes — NOPed | 024 |
| 9 | Pending-render flags | 0x603832, 0x60E30D | Yes — NOPed (no effect) | 025 |
| 10 | Light visibility state NOPs | 5 addrs in LightVolume_UpdateVisibility | Attempted — NOT confirmed | 026 |
| 11 | Light_VisibilityTest | 0x60B050 | Yes — `mov al,1; ret 4` | 031 |
| 12 | Sector light count gate | 0xEC6337 | Yes — NOPed | 033 |
| 13 | Sector light list population | FUN_006033d0 / FUN_00602aa0 | IRRELEVANT (build 038 reframe) | — |
| 14 | LOD alpha fade | 0x446580 | **UNEXPLORED** | — |
| 15 | Scene graph sector early-outs | Unknown | **UNEXPLORED** | — |
| 16 | Light Draw virtual method | vtable[0x18] | IRRELEVANT — Remix anchors to geometry | — |
| 17 | RenderLights gate | 0x60E3B1 | Yes — NOPed | 037 |
| 18 | Sector light count clear | 0x603AE6 | Yes — NOPed | 037 |
| 19 | Additional SceneTraversal exits (4×) | 0x4071CE, 0x407976, 0x407B06, 0x407ABC | Yes — all NOPed | 040 |
| 20 | Far clip distance global | 0x10FC910 | Yes — 1e30f per BeginScene | 041 |
| 21 | Camera-sector proximity filter | 0x46B85A | Yes — NOPed | 044 |
| 22 | **Terrain rendering path** | TerrainDrawable (0x40ACF0) / TERRAIN_DrawUnits | **UNEXPLORED — PRIME SUSPECT** | — |
## Known Dead Ends — DO NOT RETRY

| # | Approach | Why It Failed | Build |
|---|----------|--------------|-------|
| 1 | Re-parenting lights to largest mesh (7DFF31ACB21B3988) | Worse — large mesh not always drawn | 042 |
| 2 | Aggressive 7-NOP set in SceneTraversal | Crashed, build not preserved | 043 |
| 3 | Assuming "red at distance" was real stage light | Was fallback light — reframed at build 038 | 019–037 |
| 4 | All 11 conditional exits in 0x407150 | Draw counts 190K but anchors still vanish | 040 |
| 5 | Config flag at 0x01075BE0 ("disable extra static light culling") | No code xrefs, not connected to light collection | 032 |
| 6 | Pending-render flag NOPs (0x603832, 0x60E30D) | No effect on bottleneck | 025 |

## What Has NOT Been Tried

| Idea | Why It Matters | Difficulty |
|------|---------------|------------|
| **Investigate TerrainDrawable (0x40ACF0)** | Prime suspect: separate terrain render path with own culling | Medium — static analysis |
| dx9tracer frame capture at near vs far | Definitively shows which draw calls disappear | Medium — setup tracer |
| Find Lara's character mesh hash | Always drawn — anchor lights to her hash guarantees visibility | Easy |
| Patch terrain culling path | TerrainDrawable likely has distance/sector culling | Hard |
| LOD alpha fade at 0x446580 | 10 callers, may fade geometry at distance | Medium |
| Investigate 0x41F96A visibility check | Same threshold, different code path | Low priority |

## Repository Layout

| Path | Description |
|------|-------------|
| `proxy/` | D3D9 FFP proxy DLL source |
| `retools/` | Offline static analysis — decompile, xrefs, CFG, RTTI, signatures |
| `livetools/` | Live dynamic analysis — Frida-based tracing, breakpoints, memory r/w |
| `graphics/directx/dx9/tracer/` | Full-frame D3D9 API capture and offline analysis || `autopatch/` | Autonomous hypothesis-test-patch loop |
| `automation/` | Screenshot automation and test replay infrastructure |
| `patches/TombRaiderLegend/` | Runtime patches applied by proxy |
| `docs/` | Full documentation — research, reference, guides |
| `docs/status/WHITEBOARD.md` | **Live status** — 22-layer culling map, build history, decision tree |
| `docs/status/TEST_STATUS.md` | Build-by-build pass/fail results |
| `TRL tests/` | Test build archive — every build with SUMMARY.md, screenshots, proxy log, source |
| `TRL traces/` | Full-frame D3D9 API captures |
| `rtx_remix_tools/` | RTX Remix integration utilities |
| `tools/` | Build scripts, test utilities |
| `.claude/` | Claude Code settings |
| `.cursor/` | Cursor settings |
| `.kiro/` | Kiro settings |

## Engine Globals Reference

| Address | Name | Notes |
|---------|------|-------|
| 0x01392E18 | `g_pEngineRoot` | Root engine object |
| 0x010FC780 | View matrix source | Read by proxy |
| 0x01002530 | Proj matrix source | Read by proxy |
| 0xEFDD64 | Frustum threshold (was 16.0f) | Stamped to -1e30f |
| 0xF2A0D4/D8/DC | Cull mode globals | Stamped to D3DCULL_NONE |
| 0x10FC910 | Far clip distance | Stamped to 1e30f |
| 0xEFD404/0xEFD40C | Screen boundary min/max | Used by boundary cull checks |

### Sector Data Layout
| Field | Notes |
|-------|-------|
| `*(renderCtx+0x220)` | Sector data base pointer |
| `sector_data + N*0x684 + 0x664` | Native static light count for sector N |
| `sector+0x1B0` | Per-sector light list count || `sector+0x1B8` | Per-sector light list array pointer |
| `sector+0x84`, `sector+0x94` | Fields gating light pass in RenderScene_Main |

## Tool Catalog

### In-Repo Tools
- **retools/** — Offline static analysis: decompile, xrefs, CFG, RTTI, signatures
- **livetools/** — Frida-based live tracing, breakpoints, memory r/w
- **dx9 tracer** (`graphics/directx/dx9/tracer/`) — full-frame D3D9 API capture
- **autopatch/** — autonomous hypothesis-test-patch loop
- **automation/** — screenshot capture and test replay

### External Tools
- **GhidraMCP** (localhost:8080) — 33 MCP tools, target: `trl_dump_SCY.exe`
- **Ghidra Scripts:** `TR7_CreateGDT.py`, `TR7_Analyze.py`, `RecoverFailedPDBSymbols.py`
- Launch Ghidra via `support\pyghidraRun.bat` (NOT ghidraRun.bat)

## Build & Test
```bash
# Install deps and verify
pip install -r requirements.txt
python verify_install.py

# Full build + test pipeline
python patches/TombRaiderLegend/run.py test --build --randomize

# Autonomous patch-and-test loop
python -m autopatch
```

Say **"begin testing"** to run the full automated pipeline.
Say **"begin testing manually"** to launch and test yourself.
## Engineering Standards
1. Every session: read CLAUDE.md, then CHANGELOG.md, then WHITEBOARD.md
2. Log ALL findings to CHANGELOG.md with timestamps
3. Failed approaches go in Dead Ends table with WHY and which build
4. **Never retry a documented dead end without new evidence**
5. Every build gets a folder in `TRL tests/` with SUMMARY.md, screenshots, proxy log, and source snapshot
6. PASS builds include `miracle` in the folder name
7. Every build — pass or fail — pushed immediately
8. Test at Croft Manor: both red+green stage lights visible in all 3 screenshots, lights shift on strafe