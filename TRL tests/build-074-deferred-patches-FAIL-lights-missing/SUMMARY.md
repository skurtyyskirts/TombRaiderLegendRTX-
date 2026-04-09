# Build 074 — Deferred Patches + Page Unlock Optimization

## Result
**FAIL-lights-missing** — Deferred patches work correctly (no menu crash), all 31 culling layers active, 3749 draw calls. Stage lights absent in all 3 clean render screenshots.

## What Changed This Build
1. **Deferred memory patches** — `TRL_ApplyMemoryPatches()` now runs on first `BeginScene` where `viewProjValid=1` instead of at device creation. Fixes main menu crash (game no longer needs TR7.arg to skip menu).
2. **Permanent page unlock** — Data pages for per-scene stamps are unlocked once via `VirtualProtect(PAGE_READWRITE)`, eliminating ~28 kernel calls/frame (7 address pairs x 2 scenes x 2 calls each).
3. **memcpy optimization** — Dword-aligned copies with `#pragma intrinsic(memcpy)` for compiler-generated copies.
4. **Release() ordering fix** — Proxy COM objects released before forwarding to real device, preventing use-after-free on shutdown.
5. **Forward declaration** — Added `static void TRL_ApplyMemoryPatches(WrappedDevice*)` before `WD_BeginScene` to fix C2371 redefinition error.
6. **DIAG gating** — `vsConstWriteLog` writes guarded behind `DIAG_ENABLED` + `DIAG_ACTIVE`.
7. **rtx.conf** — `hashCollisionDetection=False`, `enableDebugMode=False`, DLSS Balanced added.

## Test Configuration
- `useVertexCapture = True`
- `geometryAssetHashRuleString = positions,indices,texcoords,geometrydescriptor`
- Anti-culling disabled (causes freeze)
- Fallback light: mode=1, radiance=100/100/100
- Peru level (via TR7.arg for automation compatibility)

## Phase 1: Hash Debug Analysis
Hash colors appear **stable** across all 3 camera positions. Same geometry blocks maintain consistent colors. Lara visible and moving. Camera pan confirmed (scene shifts between shots).

## Phase 2: Light Anchor Analysis
**No red or green stage lights visible** in any of the 3 clean render screenshots. Scene is very dark with only dim fallback lighting. No white dots visible either (unlike build 073 which showed small white dots).

## Proxy Log Summary
- Draw calls: 3749 per scene
- All 31 memory patches applied successfully
- 8/8 data pages permanently unlocked
- VS registers written: c0-c3, c8-c15, c28
- S4 expanded decls: 3413 draws, FLOAT3: 251 draws
- DrawCache: replayed 3 culled draws
- No crashes

## Root Cause Analysis
The mod.usda contains 8 mesh hashes with anchored lights:
- `mesh_2509CEDB7BB2FAFE`, `mesh_47AC93EAC3777CA5`, `mesh_DD7F8EE7F4F3969E`, `mesh_CE011E8D334D2E48`, `mesh_2AF374CD4EA62668` (original 5)
- `mesh_5601C7C67406C663`, `mesh_ECD53B85CBA3D2A5`, `mesh_AB241947CA588F11` (additional 3)

These hashes were captured under a previous Remix configuration. The current config includes `positions` in the asset hash rule and uses `useVertexCapture=True`. Either or both changes likely produce different hash IDs for the same geometry, causing all light anchors to miss.

## Next Steps
1. **Capture fresh mesh hashes** — Use Remix developer tools (debug view 277) to identify current hash IDs for the stage light geometry
2. **Update mod.usda** — Replace stale hashes with current ones
3. **Re-test** — Verify lights appear with correct hashes
