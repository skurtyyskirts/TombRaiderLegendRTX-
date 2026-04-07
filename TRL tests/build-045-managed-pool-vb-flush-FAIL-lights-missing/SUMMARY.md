# Build 045 — Managed Pool + Per-Frame VB Flush

## Result
**FAIL-lights-missing**

## Test Configuration
- `rtx.useVertexCapture = False`
- `rtx.geometryAssetHashRuleString = positions,indices,texcoords,geometrydescriptor`
- `rtx.geometryGenerationHashRuleString = positions,indices,texcoords,geometrydescriptor,vertexlayout,vertexshader`
- `rtx.antiCulling.object.enable = False`

## What Changed This Build

### 1. D3DPOOL_MANAGED for expanded VBs
Changed `S4_GetCachedExpVB` to create expanded vertex buffers in `D3DPOOL_MANAGED` (1) instead of `D3DPOOL_DEFAULT` (0). Managed pool VBs survive device state changes and are backed by system memory, preventing silent data loss when the driver reclaims video memory.

### 2. Per-frame VB cache flush
Added `S4_FlushVBCache()` called every EndScene. Releases all cached expanded VBs and resets the cache count. Forces fresh SHORT4-to-FLOAT3 expansion from source VBs every frame, eliminating stale data from dynamic VBs where the game reuses the same VB pointer with different content.

## Phase 1: Hash Debug Analysis
Hash colors are **consistent across all 3 camera positions**. The same geometry blocks maintain the same colors (cyan, green, yellow, magenta, blue) across center, left, and right pans. This is a positive signal for hash stability.

Camera movement confirmed: geometry shifts in frame between shots 1-3.

## Phase 2: Light Anchor Analysis
All 3 clean render screenshots show a **uniform brown/amber screen** with no visible geometry or stage lights. The fallback light (neutral white) appears to be the only illumination, flooding the viewport.

**No red lights visible.** **No green lights visible.**

## Phase 3: Live Diagnostics
### Draw Call Census
dipcnt not installed (tool issue).

### Patch Integrity
- Frustum threshold: -1e30 (CORRECT)
- Cull mode globals: D3DCULL_NONE (CORRECT)
- Cull function entry 0x407150: 0x55 (PUSH EBP, NOT patched to RET — one-shot patch may have been overwritten)
- LightVisibilityTest 0x60B050: 0x55 8B EC 83 (original bytes, NOT patched)

### Function Collection
- 0x00413950 (SetWorldMatrix): 14,100 hits in 15s (~940/sec)

## Analysis

The per-frame VB flush forces re-expansion of every SHORT4 draw every frame. With ~528 draws per scene and multiple scenes per frame, this creates significant overhead:
- Each expansion locks source VB, creates new managed VB, copies/expands data, unlocks
- 512 VB creations per frame is expensive even for managed pool
- The blank amber render suggests Remix received draw calls but they may have been too slow or the VBs weren't ready in time

**Hypothesis**: Per-frame flushing is too aggressive. The expanded VBs should persist across frames for static geometry. The correct fix is a content fingerprint to detect ONLY dynamic VBs that actually changed.

## Proxy Log Summary
- 11/11 cull jumps NOPed
- Null-check trampoline patched
- Sector visibility forced
- Draw counts: ~526-528 per scene
- DrawCache replayed 3 culled draws
- S4 expanded decl created (stride 24)

## Brainstorming: New Hash Stability Ideas
1. **Content fingerprint instead of flush** — XOR first 32 bytes of source VB to detect content changes without flushing the entire cache
2. **Separate expansion paths for static vs dynamic VBs** — never flush static VBs (SHORT4 world geometry), only invalidate dynamic VBs
3. **View-space position stabilization** — for FLOAT3 draws, multiply VB positions by inverse(View) to remove camera dependency

## Open Hypotheses
- The managed pool change is objectively correct (DEFAULT pool VBs can be silently evicted)
- The per-frame flush is too aggressive and kills performance
- The one-shot memory patches at 0x407150 and 0x60B050 are not sticking — need investigation

## Next Steps
- Keep D3DPOOL_MANAGED (proven improvement)
- Replace per-frame flush with content fingerprint cache validation
- Investigate why one-shot patches aren't holding (may need per-BeginScene restamping)
