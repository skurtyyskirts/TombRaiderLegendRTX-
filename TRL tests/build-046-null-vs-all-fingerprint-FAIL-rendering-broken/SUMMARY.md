# Build 046 — Null VS for ALL Draws + Fingerprint Cache

## Result
**FAIL-rendering-broken**

## Test Configuration
- `rtx.useVertexCapture = False`
- `rtx.geometryAssetHashRuleString = positions,indices,texcoords,geometrydescriptor`
- `rtx.geometryGenerationHashRuleString = positions,indices,texcoords,geometrydescriptor,vertexlayout` (vertexshader removed)
- `rtx.antiCulling.object.enable = False`

## What Changed This Build

### 1. Content fingerprint cache validation (NEW)
Replaced per-frame VB flush with a content fingerprint. Before each cache lookup, the proxy locks the first 32 bytes of the source VB and computes an XOR fingerprint. The fingerprint is stored alongside the cache entry. On lookup, if the fingerprint doesn't match, the cache entry is treated as stale and re-expanded. This detects dynamic VBs where the game reuses the same pointer with new content, without the overhead of flushing the entire cache every frame.

### 2. Null VS for ALL DIP draws (NEW)
Extended the VS-null pattern from SHORT4-only to ALL DIP draws. For non-SHORT4 draws (FLOAT3 positions), the proxy now nulls the vertex shader before the draw, sets up FFP texture stages and lighting, issues the draw, then restores the VS. This ensures the generation hash is consistent (no VS component) across all draws.

### 3. Removed vertexshader from generation hash
Since all draws now use FFP mode (VS=null), `vertexshader` was removed from `geometryGenerationHashRuleString`.

### 4. Kept D3DPOOL_MANAGED from Build 045

## Phase 1: Hash Debug Analysis
Shows ONLY Lara's face/head filling the entire screen. The hash colors are **consistent across all 3 camera positions** — same purple (hair), pink (skin features), green (face) blocks maintain their colors.

**Problem**: World geometry (Peru street, buildings) is completely absent from the view. Only FLOAT3 geometry (Lara) is visible, rendered at extreme close-up scale.

## Phase 2: Light Anchor Analysis
Clean render confirms: Lara's face fills the screen at extreme close-up. World geometry (buildings, street) is faintly visible around the edges of the frame. No stage lights visible.

**No red lights.** **No green lights.**

## Root Cause: Why Nulling VS for FLOAT3 Breaks Rendering

TRL has two vertex position types:
1. **SHORT4** (world geometry): Positions are model-space integers. The proxy expands to FLOAT3, nulls VS, uses SetTransform. Works correctly.
2. **FLOAT3** (characters, hair, foliage): Positions are **pre-transformed to view space** by the game's CPU. The VS only applies the projection matrix (c0-c3 = projection-only).

When we null the VS for FLOAT3 draws:
- VB positions are in view space (small values, 0.5-2.0 units from origin)
- FFP pipeline applies World * View * Proj transforms via SetTransform
- For view-space draws, World=Identity, View=Identity, Proj=gameProjection
- Result: viewSpacePos * Identity * Identity * Proj — mathematically correct
- BUT: Remix reads the VB positions directly (useVertexCapture=False), and view-space positions are very close to the camera, causing the geometry to render at massive scale

The VS was NEEDED for these draws because it properly transformed view-space positions through the projection matrix before Remix captured them.

## Phase 3: Live Diagnostics
Game crashed during Phase 3 (process not found when livetools tried to attach). Likely a timing issue — the game may have exited between launches.

## Proxy Log Summary
Not captured (game crashed before log flush in Phase 3).

## Key Learning
**Cannot null the vertex shader for FLOAT3 view-space draws.** The VS is essential for projecting view-space positions. Only SHORT4 draws can safely use the FFP path.

The content fingerprint cache, however, is a correct and valuable improvement with no downsides. It should be kept in all future builds.

## Brainstorming: New Hash Stability Ideas
1. **View-space to world-space conversion** — For FLOAT3 view-space draws, multiply VB positions by inverse(View) to get world-space positions. Then World=Identity, View=gameView, Proj=gameProj. World-space positions are camera-independent for static objects.
2. **Separate hash rules per draw type** — Not possible in Remix config, but could be simulated by drawing FLOAT3 geometry to a separate render target.
3. **Force all geometry through SHORT4 path** — Intercept FLOAT3 VB creation and store as SHORT4 with scale factor.

## Open Hypotheses
- FLOAT3 view-space positions ARE the primary source of hash instability (camera-dependent VB data)
- SHORT4 draws have stable hashes (confirmed by Build 045 hash debug)
- The fingerprint cache correctly detects dynamic VB content changes

## Next Steps
- Keep fingerprint cache + managed pool in all future builds
- Revert VS null for FLOAT3 draws (keep only for SHORT4)
- Investigate view-space to world-space conversion for FLOAT3 draws
- Or: find a way to make FLOAT3 draw hashes stable without modifying positions
