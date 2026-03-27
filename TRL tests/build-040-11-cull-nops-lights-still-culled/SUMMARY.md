# Build 040 — 11 Cull NOPs (FAIL)

## Result

**FAIL** — Both stage lights visible in shot 1 (near stage), only fallback red in shots 2-3 (distance). Adding 4 new cull NOPs (disable flags, far clip, draw distance) did not recover the lights.

## What Changed This Build

Added 4 new NOP patches inside SceneTraversal_CullAndSubmit (0x407150):
- **0x4071CE** — object disable flag (Phase A), bit 0x10 at [node+8]
- **0x407976** — object disable flag (Phase B), bit 0x10 at [obj+8]
- **0x407B06** — far clip distance rejection
- **0x407ABC** — draw distance fade-out rejection

Total cull NOPs now 11/11 (all identified conditional exits except LOD-count-0 safety check).

## Proxy Log Summary

- NOPed cull jumps: 11/11
- Draw counts: ~190K (slight increase from ~180K in build 039)
- All other patches active, vpValid=1, no crashes

## Retools Findings (from static-analyzer subagent)

Full decompilation of 0x407150 revealed 12 conditional exits. 11 are now NOPed. The remaining one (0x407A1F, LOD count==0) is a safety check — NOPing would crash on objects with no mesh data.

The -1e30 frustum threshold at 0xEFDD64 is confirmed effective at 4 usage sites within the function.

## Ghidra MCP Findings

Carried forward from previous builds.

## Open Hypotheses (what we think is still wrong and why)

All 11 safe exit paths in SceneTraversal_CullAndSubmit are now NOPed, yet lights still vanish at distance. The culling is NOT happening in this function. Remaining possibilities:

1. **Scene graph traversal skips entire branches.** The function at 0x407150 processes objects that are ALREADY in the render queue. A higher-level function (e.g., SceneLayer::Render, the sector iteration loop) decides which objects enter the queue. If a sector's object list doesn't include the stage light anchors, 0x407150 never sees them.

2. **Instance-level visibility.** Individual game instances (InstanceDrawable) may have their own visibility flags set by game logic (cutscene state, trigger zones, etc.) BEFORE the cull function runs.

3. **Terrain vs Instance rendering paths.** The stage light anchors might be terrain geometry (submitted via TERRAIN_DrawUnits) rather than instance objects (submitted via InstanceDrawable::Draw). These are separate code paths with separate culling.

## Next Build Plan

1. The culling problem is NOT in SceneTraversal_CullAndSubmit — we've exhausted all 11 paths. Need to look upstream at the sector iteration loop or the scene graph traversal that feeds objects into this function.
2. Consider using the TRLAU-Menu-Hook source code to find the exact rendering globals and visibility mechanisms.
3. dx9tracer frame capture at near vs far positions to diff the actual draw call lists.
