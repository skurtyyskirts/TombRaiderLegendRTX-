# Build 071 — FLOAT3 FFP Draw Fix (Lara Now Visible)

## Result
**FAIL-lights-missing** — Stage lights still not visible, but Lara is now rendered for the first time.

## What Changed This Build
Fixed FLOAT3 draw path to null the vertex shader before drawing, matching how SHORT4 draws work via `S4_ExpandAndDraw`. Previously, FLOAT3 draws (characters, hair, foliage — ~255 per frame) were submitted with the VS still bound. With `useVertexCapture=False`, Remix skipped all shader-bound draws, making Lara and all FLOAT3 geometry invisible.

**Root cause:** In `WD_DrawIndexedPrimitive` and `WD_DrawPrimitive`, the SHORT4 branch correctly called `S4_ExpandAndDraw` which nulls the VS (line 1646), sets FFP texture/lighting state, draws, and restores. But the FLOAT3 `else` branch just forwarded the draw with the VS active — Remix ignored these entirely.

**Fix:** For both `DrawIndexedPrimitive` and `DrawPrimitive`, the FLOAT3 branch now:
1. Nulls the vertex shader (`SetVertexShader(NULL)`)
2. Sets FFP texture stage state (MODULATE texture x diffuse)
3. Sets FFP lighting (ambient white, color vertex)
4. Draws
5. Restores the vertex shader

## Test Configuration
- Chapter 4 (Peru)
- Camera pan: mouse only (no WASD), 300px left then 600px right
- rtx.conf: useVertexCapture=False, fusedWorldViewMode=0
- Debug view 277 (hash), then view 0 (clean render)

## Phase 1: Hash Debug Analysis
- Lara visible as colored geometry in all 3 camera positions
- Buildings and street geometry show stable hash colors across positions
- Camera pan confirmed (different view angles in each screenshot)
- Ground hash color changes between positions (pink center, green left/right) — generation hash includes positions, expected to change

## Phase 2: Light Anchor Analysis
- Lara clearly visible with textures in all 3 screenshots
- Lara's position shifts in frame as camera pans (confirmed real movement)
- Black triangle artifact near Lara's feet (shadow/decal geometry)
- **No red stage lights visible**
- **No green stage lights visible**
- Scene lit only by fallback light

## Phase 3: Live Diagnostics

### Draw Call Census
- dipcnt not installed (livetools issue — counters not attached)
- Proxy log shows ~684 draws/frame (429 S4 + 255 F3)

### Patch Integrity
- Frustum threshold (0xEFDD64): -1e30 confirmed
- Cull mode globals (0xF2A0D4/D8/DC): D3DCULL_NONE confirmed
- Cull function (0x407150): 0x55 (PUSH EBP) — NOT RET. Proxy uses NOP-jump strategy inside the function, not RET at entry.
- Light_VisibilityTest (0x60B050): B0 01 C2 04 confirmed (always TRUE)

### Memory Watch
- SetWorldMatrix trace: 52589 calls in 15s at 0x00413950

### Function Collection
- Single address traced: 0x00413950 (SetWorldMatrix)

## Phase 4: Frame Capture Analysis
Skipped (dx9tracer not run in this build)

## Phase 5: Static Analysis
Not run in this build.

## Phase 6: Vision Analysis
- Hash debug: Lara appears as a multi-colored character shape, consistent across 3 positions
- Clean render: Lara rendered with correct textures (black outfit, boots, ponytail), positioned on cobblestone street. Black triangle artifact at feet.
- No colored stage lights visible in any clean render screenshot

## Proxy Log Summary
- 684 total draws/frame (429 SHORT4 expanded, 255 FLOAT3 now FFP)
- DrawCache replayed 3 culled draws
- 11/11 cull jumps NOPed
- All memory patches applied successfully
- View/Proj matrices valid at startup

## Brainstorming: New Hash Stability Ideas
- FLOAT3 draws now go through FFP — their positions (view-space) are captured by Remix. May need to exclude positions from asset hash for FLOAT3 draws to maintain stability (view-space positions change with camera).
- The black triangle artifact suggests a degenerate geometry or incorrect transform for shadow/decal draws.

## Open Hypotheses
1. **Stage lights anchored to geometry hashes that aren't being submitted** — the light anchor meshes may be culled by the engine before the proxy sees them. The 22-layer culling map shows several unexplored paths (TerrainDrawable, LOD alpha fade).
2. **Black triangle artifact** — likely a shadow map or projected decal with incorrect FFP transforms. May need special handling for shadow pass draws.

## Next Steps
1. Investigate the black triangle artifact — identify which draw call produces it and filter/fix
2. Continue investigating the light anchor geometry submission problem (Blocker 1 from CLAUDE.md)
3. Verify hash stability with Toolkit mesh replacements now that FLOAT3 draws are FFP
