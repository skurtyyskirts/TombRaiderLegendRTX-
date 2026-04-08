# Build 072 — RenderQueue_FrustumCull Bypass

## Result
**FAIL-lights-missing** — No red or green stage lights visible. BUT: Lara now visible in all hash debug screenshots (first time). Draw counts up 29% (2845 -> 3657). No crash or GPU device lost.

## Test Configuration
- Level: Peru (Chapter 4)
- Camera: Mouse pan only (300px left, 600px right)
- Mod: 8 mesh hashes (5 original + 3 from package)

## What Changed This Build
- **NEW PATCH**: Redirected `RenderQueue_FrustumCull` (0x40C430) to `RenderQueue_NoCull` (0x40C390) via 5-byte JMP
- This is Layer 30 in the culling map — the recursive BVH frustum culler that was the prime suspect
- The engine's own "fully inside frustum" codepath processes the same BVH tree without plane tests

## Phase 1: Hash Debug Analysis
- **Lara IS visible** in all 3 screenshots — first time she appears after the frustum cull bypass
- Camera clearly panned between shots (different angles)
- Hash colors appear consistent across positions
- More geometry visible than build 071

## Phase 2: Light Anchor Analysis
- Scene is very dark — only faint ambient illumination
- Small bright dots visible but NOT colored red/green
- Lara's silhouette faintly visible in center of shots
- **No distinct red or green stage lights in any screenshot**

## Phase 3: Live Diagnostics

### Draw Call Census
- dipcnt: "Not installed" (instrumentation issue)
- Proxy log draw counts: **3657** stable (up from 2845 in build 071)
- s4=3413 (SHORT4 expanded draws), f3=244 (FLOAT3 draws)
- No draw count drop during camera pan

### Patch Integrity
| Address | Expected | Actual | Status |
|---------|----------|--------|--------|
| 0xEFDD64 | -1e30 float | Confirmed | PASS |
| 0xF2A0D4/D8/DC | D3DCULL_NONE (1) | Confirmed | PASS |
| 0x60B050 | `B0 01 C2 04` | Confirmed | PASS |
| 0x40C430 | `E9 5B FF FF FF` (jmp) | Confirmed via log | PASS (NEW) |

### Memory Watch
- SetWorldMatrix: 36,905 calls in 15s (down from 48,339 — fewer redundant calls?)

### Function Collection
- 0x00413950 (SetWorldMatrix): 36,905 records

## Phase 4: Frame Capture Analysis
Skipped

## Phase 5: Static Analysis
Previous run confirmed:
- 0x40C430 has 10 conditional frustum exits
- 0x40C390 is structurally identical without frustum tests
- Patch is correct

## Phase 6: Vision Analysis
- Hash debug: Lara visible, camera moved, hash colors stable
- Clean render: Very dark, no colored lights, faint geometry outlines

## Proxy Log Summary
- Build successful, all patches applied including NEW RenderQueue redirect
- Draw counts: 3657 (up 29% from 2845)
- Opacity Micromap out of memory warning (more geometry to process)
- No VK_ERROR_DEVICE_LOST (previous comment in code was wrong or outdated)
- No crash

## Brainstorming: Why Lights Still Missing

The frustum cull bypass increased draws by 812 and made Lara visible — it IS submitting more geometry. But the anchor meshes for the lights still aren't appearing. Possible reasons:

1. **Hash mismatch**: The 8 hashes in the mod were captured with different settings (possibly `useVertexCapture = True`). Current config has `useVertexCapture = False`. Need a fresh Remix capture to compare.
2. **Geometry not loaded**: The anchor meshes may be in a different sector that hasn't been streamed in yet. The stream unload gate is NOPed but the stream LOAD gate may not be forcing distant sectors to load.
3. **Wrong level area**: The Peru street scene at the spawn point may not contain the stage/performance area where lights were originally placed.
4. **Draw call doesn't reach proxy**: Some geometry may bypass the proxy's DrawIndexedPrimitive hook entirely.

## Open Hypotheses
1. **Hash verification needed**: Do a fresh Remix capture at Peru and compare mesh hashes against mod
2. **Try `useVertexCapture = True`**: Match the setting used when hashes were originally captured
3. **dx9tracer diff**: Capture near-stage vs far to identify which specific draws disappear

## Next Steps
1. Toggle `useVertexCapture = True` in rtx.conf and test — hashes were likely captured with this enabled
2. Fresh Remix scene capture to compare current hashes with mod hashes
3. dx9tracer near/far frame diff to definitively identify missing geometry
