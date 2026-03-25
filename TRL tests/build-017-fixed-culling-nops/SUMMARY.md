# Build 017 — Fixed Culling NOPs + BeginScene Re-stamp

**Date:** 2026-03-25
**Build:** Shader passthrough + transform override + 3-layer anti-culling (fixed)
**Result: FAIL — Stage lights missing after D strafe, hash colors may shift**

## What Changed (vs Build 016)

| Change | Before | After |
|--------|--------|-------|
| Frustum threshold value | 1e30 (WRONG) | 0.0 (correct — skip nothing) |
| Scene traversal cull NOPs | Not in proxy source | 7 jumps NOPed in proxy |
| BeginScene re-stamp | Comment only, no code | Per-frame 0.0 re-stamp active |
| VK_MAP `]` key | Missing (no NVIDIA screenshots) | Added (0xDD) |

## Test Parameters

| Parameter | Value |
|-----------|-------|
| A strafe | 9.6 seconds |
| D strafe | 7.4 seconds |
| Screenshots per phase | 3 (baseline, after A, after D) |

## Results

### Hash Debug View (Phase 1)
- **Baseline**: Colorful, all geometry visible
- **After A strafe**: Colors present but geometry composition shifted — hash instability suspected
- **After D strafe**: Colors present, different geometry visible

**Verdict: UNCERTAIN** — need to verify same objects keep same colors

### Clean Render (Phase 2)
- **Baseline**: RED + GREEN both visible — PASS
- **After A strafe**: RED + GREEN both visible — PASS
- **After D strafe**: DARK, no stage lights visible — **FAIL**

**Verdict: FAIL** — stage lights disappear after D strafe, indicating either culling or hash instability at that position

## Proxy Log

| Metric | Value |
|--------|-------|
| Frustum threshold | 0.0 (correct) |
| Cull function ret | Applied (0x407150) |
| NOP cull jumps | 7/7 |
| Scene draws | 1416 |
| vpValid | 1 |
| passthrough | 0 |
| skippedQuad | 0 |
| Crash | None |

## Analysis

Despite all 3 layers of anti-culling applied:
1. Per-object frustum function patched to ret
2. Frustum threshold set to 0.0 with per-frame re-stamp
3. 7 scene traversal cull jumps NOPed

The stage lights still disappear at certain positions. Possible causes:
- **Additional culling mechanisms** not yet patched (LOD fade via 0x446580, other scene graph paths)
- **Hash instability** causing light-anchor meshes to get different hashes at different positions
- **Geometry streaming** — level sections may load/unload based on player position independently of frustum culling

## Next Steps

- Investigate if geometry streaming/sector loading is separate from frustum culling
- Check if LOD alpha fade (0x446580) is culling the light-anchor geometry
- Verify hash stability more rigorously — compare specific mesh colors across positions
- Consider hooking the game's scene graph to force all sectors loaded
