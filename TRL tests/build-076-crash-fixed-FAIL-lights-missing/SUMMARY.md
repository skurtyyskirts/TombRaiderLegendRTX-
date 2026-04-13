# Build 076 — Crash Fixed, Rendering Restored

## Result
**FAIL-lights-missing** — crash resolved, rendering fully working, stage lights not yet confirmed in clean render.
Stage lights require fresh mod.usda hash capture (separate next step).

## Test Configuration
- **Date:** 2026-04-13
- **Proxy:** S4 SHORT4→FLOAT3 CPU expansion, draw cache, null VS, useVertexCapture=False
- **rtx.conf:** positions included in hash rule, no vertex capture, enableReplacementAssets=True
- **user.conf:** rtx.enableReplacementAssets=True ✓ (preserved from build 075 breakthrough)
- **Level:** Peru (Croft Manor stage lights area)

## What Changed This Build
**Two crash protections restored** that were accidentally removed in the previous session:

1. **`patch_null_crash_40D2AF()`** — code-cave patch at 0x40D2AC to guard null pointer crash at 0x40D2AF.
   Applied in DllMain on DLL load. Without this, function 0x40D290 dereferences NULL+0x20 during scene traversal → immediate crash.

2. **PUREDEVICE stripping** — `behFlags &= ~0x00000010` in W9_CreateDevice.
   Restored in `patches/TombRaiderLegend/proxy/d3d9_wrapper.c`. Without this, dxvk-remix would create device in PUREDEVICE mode, breaking Get* state query calls.

3. **FourCC format rejection** — restored D3DERR_NOTAVAILABLE for `cf > 0xFF` in W9_CheckDeviceFormat.

4. **build.bat VS 18 fallback** — added `C:\Program Files\Microsoft Visual Studio\18\Community` to VSDIR detection chain.

Both proxy support files in `patches/TombRaiderLegend/proxy/d3d9_main.c` and `d3d9_wrapper.c` were correctly
updated (the `proxy/` root template files were also updated but are not what run.py builds from).

## Phase 1: Hash Debug Analysis
3 screenshots captured in Peru with debug view 277.

**Hash stability: PASS** — same geometry gets same color across all 3 camera positions.
- Lara visible ✓
- Buildings visible with consistent hash colors ✓
- Camera panned between shots (geometry shifts relative to frame center) ✓
- Green floor plane consistent across all shots ✓

No hash color shifts detected.

## Phase 2: Light Anchor Analysis
Clean render screenshots captured in transitional area (Bolivia/loading), not Peru.
The clean render phase was captured before the test navigated to the Peru stage lights area.

**From no-build baseline test (same session, old rtx.conf):**
- Phase 2 clean render captured correctly in Peru area
- **Purple replacement light VISIBLE** in center of frame — confirms replacement assets pipeline is working
- This is likely one of the stage anchor lights from mod.usda (hash may match from old capture)

Stage lights verdict: **INCONCLUSIVE** — replacement assets are working (purple light visible),
but the red/green stage lights for the PASS criteria were not captured in a proper clean render test.

## Phase 3: Live Diagnostics

### Draw Call Census (from proxy diagnostic log, scene 1484-1492)
| Metric | Value |
|--------|-------|
| Total draws/scene | 3,733–3,760 |
| S4 (SHORT4 expanded) | 3,413 |
| FLOAT3 (character/hair) | 320 |
| Passthrough | 0 |
| DrawCache replays | 3 |

Draw counts are healthy. No draw drop detected.

### Patch Integrity (from proxy log)
All 31 culling patches applied successfully:
- Frustum threshold: patched to -1e30 ✓
- NOP cull jumps: 11/11 ✓
- Null check trampoline at 0x4071D9 ✓
- ProcessPendingRemovals: patched ✓
- Sector visibility: 2/2 ✓
- SectorPortalVisibility: 4/4 bounds NOPed ✓
- Cull mode globals: D3DCULL_NONE ✓
- Light_VisibilityTest: always TRUE ✓
- RenderLights gate NOPed ✓
- Terrain cull gate NOPed ✓
- MeshSubmit_VisibilityGate: always 0 ✓
- RenderQueue_FrustumCull: redirected to NoCull ✓
- Level writers NOPed: 2/2 ✓
- Data pages unlocked: 8/8 ✓
- BehaviorFlags cleaned: 0x50 → 0x40 (PUREDEVICE stripped) ✓ ← NEW

### VS Registers
Written per-scene: c0-c3 (WVP), c8-c11 (View), c12-c15 (Proj), c28 (misc) ✓

## Phase 4: Frame Capture Analysis
Not performed this build.

## Phase 5: Static Analysis
Not performed this build.

## Phase 6: Vision Analysis
- Phase 1 hash debug: Lara visible, buildings with consistent colors, camera panned ✓
- Phase 2 clean render (baseline no-build): Purple replacement light visible in Peru ✓
- Phase 2 clean render (build test): Captured in wrong level (Bolivia), not evaluable

## Proxy Log Summary
- Proxy log: 299KB, 16,356 lines, full diagnostic data
- 1,484+ scenes rendered (no crash)
- Draw calls stable ~3,733/scene
- PUREDEVICE stripping confirmed: `BehaviorFlags (original): 0x00000050 → (cleaned): 0x00000040`
- All patches applied and active

## Open Hypotheses
1. Stage lights in mod.usda may have wrong hashes for the new proxy configuration (positions now in asset hash rule → different mesh hashes than build 075 capture)
2. A replacement asset light IS visible (purple glow) confirming the pipeline works; likely the red/green stage lights' hashes need updating from a fresh capture

## Next Steps
1. **Fresh Remix capture** near Peru stage with current proxy active
2. Extract mesh hash IDs for the 5 building anchor meshes from the Toolkit capture
3. Update `mod.usda` with new hashes (5 meshes: red ×3, green ×2)
4. Run full test to confirm PASS criteria: red AND green stage lights visible, shifting with camera pan
