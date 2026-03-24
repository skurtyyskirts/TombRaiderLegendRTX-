# Build 001 — Baseline Passthrough

**Date:** 2026-03-24
**Build:** Shader passthrough + transform override
**Result:** PASS — Hashes STABLE

## Configuration

| Setting | Value |
|---------|-------|
| Resolution | 1024x768 |
| Proxy mode | Shader passthrough (shaders stay active, transforms overridden) |
| Skinning | Disabled |
| Frustum culling | Patched (threshold=1e30, cull function returns immediately) |
| Asset hash rule | `indices,texcoords,geometrydescriptor` (excludes positions) |
| Generation hash rule | `positions,indices,texcoords,geometrydescriptor,vertexlayout,vertexshader` |
| Vertex capture | Enabled (`rtx.useVertexCapture = True`) |
| Fused world-view | Disabled (`rtx.fusedWorldViewMode = 0`) |

## Proxy Log Analysis

| Metric | Value | Status |
|--------|-------|--------|
| Scene summaries | 9 | OK |
| vpValid | 1 (all frames) | PASS |
| passthrough | 0 (all frames) | PASS — 100% draws processed |
| xformBlocked | 0 | PASS |
| skippedQuad | 0 | OK |
| Crash | None | PASS |

### Draw call counts per scene batch (120 frames each):

| Scene | Total draws |
|-------|-------------|
| 120 | 1,416 |
| 240 | 1,440 |
| 360 | 1,440 |
| 480 | 1,440 |
| 600 | 1,440 |
| 720 | 1,440 |
| 840 | 1,440 |
| 960 | 834 (menu transition) |
| 1080 | 119 (loading screen) |

## Hash Stability Analysis

**Method:** Visual inspection of Geometry Hash debug view (Remix Alt+X > Debug > Geometry Hash)

### Screenshots

1. `01-normal-view.png` — Normal rendered view (Bolivia cave, Lara standing)
2. `02-geometry-hash-view1.png` — Geometry Hash debug: each mesh colored by its hash
3. `03-geometry-hash-view2.png` — Same debug view, camera strafed right
4. `04-geometry-hash-view3.png` — Camera moved further, different angle

### Observations

- **Ground planes**: Consistent tan/yellow-green colors across all 3 hash views
- **Rock faces**: Consistent purple/teal/green colors across camera positions
- **Foliage**: Consistent blue/orange colors, no flickering
- **Lara (character)**: Consistent hash color
- **No color flickering** between frames — asset hashes are stable during camera movement

### Conclusion

Asset hashes are **STABLE** with the current configuration. The `geometryAssetHashRuleString` excluding positions means camera movement doesn't affect hash computation. The generation hash (which includes positions) will flicker — this is expected and cosmetic only.

## Matrix Verification (from proxy log)

View and Projection matrices read correctly from game memory:
- View: `0x010FC780` — valid rotation matrix
- Proj: `0x01002530` — valid perspective projection (near=16, FOV consistent)
- VP computed correctly from View * Proj
- World matrix derived and applied successfully

## Files in this build

- `d3d9_device.c` — proxy source for this build
- `ffp_proxy.log` — full proxy diagnostic output
- `screenshots/` — 4 captures (normal + 3 hash debug views)
