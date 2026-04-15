# CLAUDE.md — TombRaiderLegendRTX

## Identity
- **Game:** Tomb Raider: Legend (2006, Crystal Dynamics, cdcEngine, Steam PC, 32-bit x86)
- **Project:** Vibe Reverse Engineering toolkit — D3D9 FFP proxy DLL + RE tools for RTX Remix compatibility
- **Repo:** github.com/skurtyyskirts/TombRaiderLegendRTX-
- **Owner:** Jeffrey (skurtyyskirts), Temple TX
- **Builds completed:** 001–077, 071b (003–015, 034, 043, 048–063 not preserved)

## DLL Chain
```
NvRemixLauncher32.exe → trl.exe → dxwrapper.dll → d3d9.dll (FFP proxy) → d3d9_remix.dll
```

## Architecture Summary
TRL renders exclusively through programmable vertex shaders. RTX Remix requires Fixed-Function Pipeline (FFP). The proxy intercepts D3D9 calls, reconstructs W/V/P matrices from VS constants, and feeds them to Remix through FFP calls — so Remix sees TRL as a native FFP game. The proxy also maps 36 culling layers at runtime via `VirtualProtect` + memory write (32 confirmed patched, 2 irrelevant to Remix, 2 unexplored). FLOAT3 character draws (Lara's model) are now correctly processed through FFP as of build 071b.

### VS Constant Register Layout (TRL-specific)
```
c0–c3:   World matrix (transposed)
c8–c11:  View matrix
c12–c15: Projection matrix
c48+:    Skinning bone matrices (3 regs/bone)
```
Note: View and Projection are SEPARATE registers, not a fused ViewProj.

### Proxy Method Hooks
| Method | What it does |
|--------|-------------|
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
- **Asset hash stability confirmed (build 075)** — purple test light anchored to `mesh_574EDF0EAD7FC51D` was stable and visible; Toolkit replacement pipeline works end-to-end. Stage lights absent only because those 8 mesh hashes are stale and need a fresh capture.

## Current Status

### DONE
- FFP proxy DLL builds and chains to Remix
- Transform pipeline (View/Proj/World from VS constants)
- Automated test pipeline (two-phase: hash debug + clean render, camera pan)
- All 36 culling layers mapped — 32 confirmed patched (light pipeline gates re-enabled build 068; RenderQueue_FrustumCull bypass build 072; scene traversal null guard build 076)
- SHORT4→FLOAT3 VB expansion with content fingerprint cache
- ProcessPendingRemovals crash fix at 0xEE88AD
- FLOAT3 draw path fixed — null VS before FLOAT3 draws; Lara now visible (build 071b)
- Replacement asset pipeline confirmed working end-to-end (build 075) — purple test light visible and stable
- `user.conf` `enableReplacementAssets=False` override identified and fixed (was silently disabling all mod content builds 016–074)
- Crash protections restored (null-crash guard at 0x40D2AC + PUREDEVICE stripping) — build 076
- Cold launch stable — DrawCache use-after-free fixed, AddRef'd all cached resources — build 077

### ONE REMAINING BLOCKER

#### Blocker: Stale Anchor Mesh Hashes in mod.usda

**Build 075 breakthrough:** `user.conf` in the game directory had `rtx.enableReplacementAssets=False`, silently disabling ALL mod content in every build from 016 to 074. Fixed in build 075. Replacement asset pipeline is now confirmed working end-to-end — a purple test light was visible, stable, and shifted position with camera movement.

**Current state:** Stage lights absent because the 8 anchor mesh hashes in `mod.usda` are stale. They were captured under a previous Remix configuration (likely before `positions` was added to the hash rule and before SHORT4→FLOAT3 expansion). The geometry IS being rendered (3749 draw calls per scene), but no rendered mesh matches the stored hashes.

**NEXT STEP:** Fresh Remix capture near the Peru stage. Extract current mesh hash IDs from the Toolkit capture, update `mod.usda`, re-test.

**PASS criteria:** Both red and green stage lights visible in all 3 clean render screenshots, lights shift as Lara strafes, hashes stable, no crash.

## 36-Layer Culling Map

| # | Layer | Address(es) | Patched? | Build |
|---|-------|------------|----------|-------|
| 1 | Frustum distance threshold | 0xEFDD64 | Yes — -1e30f per BeginScene | 016 |
| 2 | Per-object frustum function | 0x407150 | Yes — 11 NOP jumps inside function (NOT RET at entry) | 016 |
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
| 22 | Terrain rendering path | TerrainDrawable (0x40ACF0) / TERRAIN_DrawUnits | Yes — terrain flag gate NOPed (0x40AE3E) | 045–063 |
| 23 | Null-check guard | 0xEDF9E3 | Yes — trampoline patched | 045–063 |
| 24 | ProcessPendingRemovals stale field | FUN_00ProcessPendingRemovals | Yes — patched (resolved crash at 0xEE88AD) | 045–063 |
| 25 | MeshSubmit visibility gate | MeshSubmit_VisibilityGate (0x454AB0) | Yes — `xor eax,eax; ret` | 045–063 |
| 26 | Sector already-rendered skip | 0x46B7F2 | Yes — NOPed | 045–063 |
| 27 | Post-sector bitmask/distance culls | 0x40E30F, 0x40E3B0 | Yes — NOPed | 045–063 |
| 28 | Stream unload gate | 0x415C51 | Yes — NOPed | 045–063 |
| 29 | Mesh eviction | SectorEviction (×2) + ObjectTracker_Evict | Yes — all 3 NOPed | 045–063 |
| 30 | Post-sector loop | 0xF12016 (enable flag), 0x10024E8 (gate) | Yes — enabled | 045–063 |
| 31 | Render queue frustum culler | 0x40C430 (RenderQueue_FrustumCull) | Yes — JMP to 0x40C390 (uncull path); +29% draws | 072 |
| 32 | Frustum screen-size rejection | 0x46C242, 0x46C25B | Yes — NOPed | 045–063 |
| 33 | SectorPortalVisibility resets | 4 write sites | Yes — all 4 NOPed | 045–063 |
| 34 | Sector_SubmitObject gates | 0x40C666, 0x40C68B | Yes — NOPed | 045–063 |
| 35 | Level writers | 0x46CCB4, 0x4E6DFA | Yes — NOPed | 045–063 |
| 36 | Null crash guard (scene traversal) | 0x40D2AC | Yes — trampoline patched | 076 |

## Known Dead Ends — DO NOT RETRY

| # | Approach | Why It Failed | Build |
|---|----------|--------------|-------|
| 1 | Re-parenting lights to largest mesh (7DFF31ACB21B3988) | Worse — large mesh not always drawn | 042 |
| 2 | Aggressive 7-NOP set in SceneTraversal | Crashed, build not preserved | 043 |
| 3 | Assuming "red at distance" was real stage light | Was fallback light — reframed at build 038 | 019–037 |
| 4 | All 11 conditional exits in 0x407150 | Draw counts 190K but anchors still vanish | 040 |
| 5 | Config flag at 0x01075BE0 ("disable extra static light culling") | No code xrefs, not connected to light collection | 032 |
| 6 | Pending-render flag NOPs (0x603832, 0x60E30D) | No effect on bottleneck | 025 |
| 7 | LightVolume_UpdateVisibility state NOPs (5 addrs) | Patches not confirmed in proxy log — silent VirtualProtect failure | 026 |
| 8 | D3DPOOL_MANAGED + per-frame VB flush | Flush too aggressive (512 VB creates/frame); D3DPOOL_MANAGED is correct, flush is not | 045 |
| 9 | Null VS for ALL draws (content fingerprint cache) | Breaks view-space geometry — FLOAT3 view-space positions render at extreme scale | 046 |
| 10 | Remove `positions` from asset hash | Catastrophic hash collision — all geometry gets same hash; positions required | 047 |
| 11 | Draw cache disabled (`DRAW_CACHE_ENABLED 0`) | No effect — cache only replays 3 draws, stale pointer concern unfounded | 066 |
| 12 | Remove VP inverse epsilon threshold | No effect — VP changes are large enough to always trigger recalculation | 067 |
| 13 | RenderQueue_FrustumCull bypass (0x40C430 → 0x40C390) | +29% draws, no crash — but lights still absent; anchor hash mismatch likely cause | 072 |
| 14 | `user.conf` overriding `rtx.enableReplacementAssets=True` | `user.conf` had `enableReplacementAssets=False` generated by in-game Remix menu; silently disabled all mod content builds 016–074 — fixed build 075 | 075 |
| 15 | CPU smooth normals in proxy (stream 1 FLOAT3 injection) | Built and compiled but didn't work at runtime — changes all geometry descriptor hashes, possible IB lock/stream 1/Remix capture conflicts | 075+ |

## What Has NOT Been Tried

| Idea | Why It Matters | Difficulty |
|------|---------------|------------|
| **Fresh Remix capture** to update mod.usda hashes | Stale hashes confirmed as root cause in build 075; geometry IS rendering (3749 draws/scene) but no mesh matches stored hashes | Easy |
| Anchor to Lara's always-drawn mesh | Lara's hash visible in build 071b+; anchor lights to her for guaranteed-visible reference | Easy |
| dx9tracer frame diff: near stage vs far | Shows which specific draw calls contain the building geometry | Medium |
| Investigate per-object flags at [obj+8] | Bits 0x01/0x02/0x04 may independently gate draw submission (proxy only NOPs `test bit 0x10`) | Medium |
| LOD alpha fade at 0x446580 | 10 callers — may fade geometry invisible at distance | Medium |
| **`rtx.smoothNormalsTextures` with texture hash dump** | Remix computes smooth normals on GPU for tagged textures — no proxy changes, no hash disruption. Dump all texture hashes from dx9tracer capture. | Easy |
| Draw call replay in proxy | Record anchor DIP calls on first frame; replay every subsequent frame unconditionally | Hard |

## Repository Layout

| Path | Description |
|------|-------------|
| `proxy/` | D3D9 FFP proxy DLL source |
| `retools/` | Offline static analysis — decompile, xrefs, CFG, RTTI, signatures |
| `livetools/` | Live dynamic analysis — Frida-based tracing, breakpoints, memory r/w |
| `graphics/directx/dx9/tracer/` | Full-frame D3D9 API capture and offline analysis |
| `autopatch/` | Autonomous hypothesis-test-patch loop |
| `automation/` | Screenshot automation and test replay infrastructure |
| `patches/TombRaiderLegend/` | Runtime patches applied by proxy |
| `docs/` | Full documentation — research, reference, guides |
| `docs/status/WHITEBOARD.md` | **Live status** — 36-layer culling map, build history, decision tree |
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
| `sector+0x1B0` | Per-sector light list count |
| `sector+0x1B8` | Per-sector light list array pointer |
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

# Full stage-light release gate
python patches/TombRaiderLegend/run.py test --build --randomize

# Hash-only nightly screening flow
python patches/TombRaiderLegend/run.py test-hash --build

# Autonomous patch-and-test loop
python -m autopatch
```

Say **"begin testing"** to run the full stage-light release gate.
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
