# Build 016 — Anti-Culling NOP Patches + Stage Light Verification

**Date:** 2026-03-25
**Build:** Shader passthrough + transform override + comprehensive anti-culling
**Result: PASS — Asset hashes STABLE, stage lights VISIBLE, culling DISABLED**

## What Changed (vs Build 015)

| Change | Before | After |
|--------|--------|-------|
| Frustum threshold value | 1e30 (WRONG — skips everything) | 0.0 (correct — skips nothing) |
| Scene traversal cull jumps | Not patched | 7 conditional jumps NOPed |
| Frustum threshold re-stamp | Not applied per-frame | Re-stamped every BeginScene |
| `rtx.enableReplacementAssets` | False (user.conf override) | True |
| Draw count stability | 40K-189K (massive variation) | 91,800 consistent |

## Configuration

| Setting | Value |
|---------|-------|
| Resolution | 1024x768 |
| Proxy mode | Shader passthrough (shaders stay active, transforms overridden) |
| Skinning | Disabled (`ENABLE_SKINNING=0`) |
| Frustum culling | 3-layer disable (see Anti-Culling section) |
| Asset hash rule | `indices,texcoords,geometrydescriptor` (excludes positions) |
| Vertex capture | Enabled (`rtx.useVertexCapture = True`) |
| Fused world-view | Disabled (`rtx.fusedWorldViewMode = 0`) |
| Replacement assets | Enabled (`rtx.enableReplacementAssets = True`) |

## Anti-Culling Patches

Three layers of culling disabled for RTX Remix compatibility:

| Layer | Target | Patch | Purpose |
|-------|--------|-------|---------|
| 1. Per-object frustum test | `0x407150` | `ret` (0xC3) | Prevents per-object frustum plane visibility marking |
| 2a. Distance threshold | `0xEFDD64` | Set to 0.0f | Objects skip when `distance <= threshold`; 0.0 = skip nothing |
| 2b. Distance cull jumps | `0x4072BD`, `0x4072D2`, `0x407AF1` | NOP (6 bytes each) | Scene traversal distance-check conditional jumps |
| 2c. Screen boundary jumps | `0x407B30`, `0x407B49`, `0x407B62`, `0x407B7B` | NOP (6 bytes each) | Viewport boundary culling in scene traversal |
| 3. D3D backface culling | `SetRenderState` hook | Force `D3DCULL_NONE` | All backfaces visible for ray tracing |

**Per-frame re-stamp:** The game recomputes the frustum threshold from camera parameters every frame, overwriting the one-shot patch. BeginScene now re-stamps `0xEFDD64 = 0.0f` every scene.

## Proxy Log Metrics

| Metric | Value | Status |
|--------|-------|--------|
| Scene summaries | 20 | OK |
| vpValid | 1 (all frames) | PASS |
| passthrough | 0 (all frames) | PASS — 100% draws processed |
| xformBlocked | 0 | PASS |
| skippedQuad | 0 | OK |
| Crash | None | PASS |
| Cull jumps NOPed | 7/7 | PASS |

Draw calls stable at **91,800** per 120-frame batch during gameplay (previously varied 40K-189K with active culling).

## Hash Stability Analysis

### Method
Two-phase test with randomized A/D strafing (hold durations 1-10 seconds, 3-7 bursts per run):
- Phase 1: `debugViewIdx = 277` (Geometry/Asset Hash view), screenshots at varied positions
- Phase 2: `debugViewIdx = 0` (clean render), screenshots at varied positions

### Hash Debug View Results

| Geometry | Across Strafes | Across Runs |
|----------|---------------|-------------|
| Ground plane tiles | SAME tan/dark-yellow | STABLE |
| Rock faces | SAME maroon/purple/teal | STABLE |
| Foliage (ferns, plants) | SAME orange/blue/green | STABLE |
| Lara character model | SAME cyan/green | STABLE |
| Cave walls | SAME varied colors | STABLE |
| Distant cliffs | SAME lavender/green | STABLE |

**Result: ALL tracked elements STABLE across randomized strafing and camera angles.**

### Stage Light Verification

The Remix mod (`rtx-remix/mods/pee/mod.usda`) has 4 sphere lights anchored to specific mesh hashes:

| Mesh Hash | Light Color | Visible? |
|-----------|-------------|----------|
| `mesh_ECD53B85CBA3D2A5` | Red (intensity 200, radius 40) | YES |
| `mesh_AB241947CA588F11` | Green (intensity 100, radius 40) | YES |
| `mesh_5601C7C67406C663` | Red (intensity 100, radius 20) | Not in test area |
| `mesh_6AF01B710C2489F5` | Pink (intensity 100, radius 30) | Not in test area |

**Red and green stage lights are clearly visible** in clean render screenshots, confirming:
1. Asset hashes match between proxy output and mod definitions
2. Anti-culling keeps light-anchored geometry rendered at all camera angles
3. `rtx.enableReplacementAssets = True` is required (was `False` in `user.conf`)

### Key Observations

1. **Frustum threshold was inverted**: The original patch set threshold to 1e30, but the game's check is `skip if distance <= threshold` — meaning 1e30 would skip everything. The `0x407150` ret patch masked this by preventing the check function from running at all, but the scene traversal at `0x4072A0` had its own inline checks that weren't patched.

2. **Draw count stabilization**: With all culling disabled, draw counts went from highly variable (40K-189K) to near-constant (91,800). The remaining variation (91K-93K during transitions) comes from level streaming, not culling.

3. **Per-frame re-stamp necessary**: The game recomputes the frustum threshold from camera parameters every frame. Without re-stamping in BeginScene, the one-shot patch from device creation gets overwritten within the first frame.

4. **`user.conf` overrides `rtx.conf`**: Remix loads config layers in order (dxvk.conf -> rtx.conf -> user.conf), with later values winning. `rtx.enableReplacementAssets = False` in `user.conf` silently disabled all mod lights/materials/meshes.

## Files

- `d3d9_device.c` — proxy source with 3-layer anti-culling
- `ffp_proxy.log` — diagnostic output showing 7/7 NOP patches applied
- `screenshots/` — hash debug + clean render captures with stage lights
- `SUMMARY.md` — this analysis
