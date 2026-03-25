# Build 018 — miracle: Stable Hashes + Stage Lights PASS

**Date:** 2026-03-25
**Build:** Shader passthrough + transform override + comprehensive anti-culling
**Result: PASS — Asset hashes STABLE, both stage lights VISIBLE in all clean screenshots**

## What Changed (vs Build 017)

Build 017 failed with lights not visible. Build 018 re-ran the same proxy code with randomized movement to confirm stability.

| Change | Build 017 | Build 018 |
|--------|-----------|-----------|
| Stage lights | FAIL (not visible) | PASS (both red+green in all 3) |
| Hash stability | UNCERTAIN | STABLE (no color shifts) |
| Movement | Fixed timing | Randomized (A=1064ms, D=2086ms) |

## Configuration

| Setting | Value |
|---------|-------|
| Resolution | 1024x768 |
| Proxy mode | Shader passthrough (shaders stay active, transforms overridden) |
| Skinning | Disabled (`ENABLE_SKINNING=0`) |
| Frustum culling | 3-layer disable (ret patch + threshold 0.0 + 7 NOP jumps) |
| Asset hash rule | `indices,texcoords,geometrydescriptor` (excludes positions) |
| Vertex capture | Enabled (`rtx.useVertexCapture = True`) |
| Fused world-view | Disabled (`rtx.fusedWorldViewMode = 0`) |
| Replacement assets | Enabled (`rtx.enableReplacementAssets = True`) |

## Anti-Culling Patches

1. **Frustum cull function** (`0x407150`): patched to immediate `ret` — skips all frustum checks
2. **Frustum threshold**: set to `0.0` (nothing culled), re-stamped every `BeginScene`
3. **Scene traversal cull jumps**: 7/7 conditional jumps NOPed

## Test Results

### Phase 1 — Hash Debug (Asset Hash View 277)
- All 3 screenshots show **consistent colors** — same geometry keeps same hash color across camera positions
- Ground (tan/yellow-green), rocks, foliage, Lara all stable
- No color shifts between baseline, A-strafe, and D-strafe

### Phase 2 — Clean Render
- **All 3 gameplay screenshots show both RED and GREEN stage lights**
- Red light illuminates left side, green light illuminates right side
- Lights remain visible and correctly positioned across all camera positions
- Confirms: hashes are stable (lights stay anchored) AND anti-culling works (anchor geometry always rendered)

### Proxy Log
- No crashes, no skipped draws, no transform blocks
- Draw calls: ~1400/scene during gameplay, 123K+ during heavy scenes
- All patches active: frustum ret, threshold 0.0, 7/7 NOP jumps

## Screenshots

| File | Description |
|------|-------------|
| `hash-debug-baseline.png` | Asset hash view — baseline position |
| `hash-debug-strafe-A.png` | Asset hash view — after A strafe |
| `hash-debug-strafe-D.png` | Asset hash view — after D strafe |
| `clean-render-baseline.png` | Clean render — baseline (both lights visible) |
| `clean-render-strafe-A.png` | Clean render — after A strafe (both lights visible) |
| `clean-render-strafe-D.png` | Clean render — after D strafe (both lights visible) |
