# Build 047 — Strip Positions from Asset Hash

## Result
**FAIL-hash-collision**

## Test Configuration
- `rtx.useVertexCapture = False`
- `rtx.geometryAssetHashRuleString = indices,texcoords,geometrydescriptor` (NO positions)
- `rtx.geometryGenerationHashRuleString = positions,indices,texcoords,geometrydescriptor,vertexlayout,vertexshader`
- `rtx.antiCulling.object.enable = False`

## What Changed This Build

### 1. Removed `positions` from asset hash rule (NEW)
Changed `geometryAssetHashRuleString` from `positions,indices,texcoords,geometrydescriptor` to `indices,texcoords,geometrydescriptor`. The idea: if position data is the source of hash instability (FLOAT3 view-space positions change with camera), removing positions from the hash makes hashes completely position-invariant.

### 2. Reverted VS null for FLOAT3 draws
Restored the original draw routing: VS is only nulled for SHORT4 draws (via S4_ExpandAndDraw). FLOAT3 draws pass through with the shader active.

### 3. Kept fingerprint cache + managed pool from Build 046

## Phase 1: Hash Debug Analysis
All 3 screenshots show a **solid hot pink/magenta screen** — ALL geometry received the SAME hash. This is a catastrophic hash collision.

**Root cause**: Without positions in the hash, `indices,texcoords,geometrydescriptor` is not unique enough to distinguish different meshes. Many TRL meshes share the same:
- Index buffer layout (same triangle strip patterns)
- Texcoord format (same D3DDECLTYPE_FLOAT2 in same offset)
- Geometry descriptor (same vertex stride, same element count)

The only differentiator between these meshes was their position data. Without it, Remix sees them all as "the same mesh."

## Phase 2: Light Anchor Analysis
Clean render shows the Peru street scene with world geometry visible (buildings, street, objects). The scene is lit by fallback light (neutral white). Camera pan confirmed (slight angle difference between shots).

**No red lights.** **No green lights.**

Lights cannot anchor because all geometry shares the same hash. The anchor hash `mesh_5601C7C67406C663` cannot exist as a unique entity when everything has the same hash.

## Phase 3: Live Diagnostics
### Draw Call Census
dipcnt not installed.

### Patch Integrity
- Frustum threshold: -1e30 (CORRECT)
- Cull mode globals: D3DCULL_NONE (CORRECT)
- Cull function entry 0x407150: 0x55 (NOT patched — same as builds 045-046)
- LightVisibilityTest 0x60B050: original bytes (NOT patched)

### Function Collection
- 0x00413950 (SetWorldMatrix): 43,604 hits in 15s (~2,907/sec) — 3x Build 045

## Key Learning: Positions Are Essential for Hash Uniqueness

This build definitively proves that `positions` MUST be in the asset hash. The remaining hash components (`indices`, `texcoords`, `geometrydescriptor`) are NOT sufficient to uniquely identify meshes in TRL. Many meshes share the same vertex format and similar index/texcoord patterns.

## The Hash Stability Dilemma (Updated)

| Component | SHORT4 Draws | FLOAT3 View-Space Draws |
|-----------|-------------|------------------------|
| positions | STABLE (expanded from static VB) | UNSTABLE (camera-dependent) |
| indices | STABLE | STABLE |
| texcoords | STABLE | STABLE |
| geometrydescriptor | STABLE | STABLE |

The instability is ISOLATED to FLOAT3 view-space draws (Lara, hair, foliage, characters). SHORT4 world geometry hashes are already stable.

**The anchor meshes are SHORT4 world geometry** — their hashes should be stable. The fact that they disappear when the camera moves is a **culling problem**, not a hash instability problem.

## Brainstorming: Reframing the Problem

The 3-build experiment series (045-047) reveals:
1. SHORT4 draw hashes ARE stable (Build 045 hash debug proved this)
2. Positions MUST be in the hash (Build 047 proved this)
3. FLOAT3 draws can't have their VS nulled (Build 046 proved this)

**Reframe**: The "hash instability" may actually be "geometry culling." The anchor meshes have stable hashes, but they're not being drawn at certain camera positions because of:
- Sector-based portal/PVS culling (not all layers patched)
- Terrain rendering path (unexplored)
- LOD distance fade (unexplored)
- Mesh eviction despite prevention patches

## Open Hypotheses
1. The one-shot patches (0x407150 RET, 0x60B050 LightVisTest) are NOT sticking across all 3 builds — needs urgent investigation
2. The real blocker may be culling, not hash instability
3. Content fingerprint cache is validated and should be kept

## Next Steps
1. **URGENT**: Investigate why 0x407150 and 0x60B050 patches aren't visible at runtime
2. Restore `positions` in asset hash (this build confirms they're needed)
3. Focus on culling: decompile terrain rendering path, LOD fade system
4. Keep fingerprint cache + managed pool as permanent improvements
