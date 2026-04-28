# TRL RTX Remix — Hourly Review Report
**Date:** 2026-04-28 | **Last build:** 077 (2026-04-13, 15 days ago) | **Last proxy change:** H3+H4 water/jitter fixes (not yet tested as a numbered build)

---

## Critical Finding: 15-Day Stall on the Only Remaining Blocker

The project has been stuck on **"stale anchor mesh hashes in mod.usda"** since build 075 (April 9). The solution is known and documented as "Easy" difficulty: do a fresh Remix capture near the Peru stage, extract current mesh hash IDs, and update mod.usda. This hasn't happened because it requires manual interaction with the Remix developer menu in-game.

The last 15 days of work (H3 water UV scroll detection, H4 VP jitter lock, deploy script fixes) were quality improvements that don't address the blocker.

**The pipeline is confirmed working.** Build 075 proved it: a purple test light appeared, was stable, and shifted with camera movement. The only missing piece is correct mesh hash IDs.

---

## New Theories

### 1. H4 VP Lock Discontinuity Could Cause Hash Flicker at Threshold Boundary

The H4 jitter lock (`d3d9_device.c:2173-2181`) uses a threshold of `1.5e-2`. When View/Proj drift stays below this, the locked values are reused. When drift exceeds it, the lock snaps to the new values. At the exact transition point, Remix sees a sudden VP change — the accumulated drift from all suppressed frames hits at once. This could cause a single-frame hash discontinuity in the generation hash (which includes positions).

**Validation:** Log when the VP lock snaps (add a counter to `vpLockValid` updates). If it snaps every N frames on a slow camera pan, the generation hash will flicker at those frames. Not a blocker for asset hashes (which use model-space positions under `useVertexCapture=True`), but could affect Remix's internal geometry tracking.

### 2. DrawCache Replays Geometry with Stale World Matrix + Current View/Proj

`DrawCache_Replay` (line 2910) reads fresh View/Proj from game memory but uses the cached `c->world` from the original draw. If the camera has moved since the draw was last live, the replayed geometry appears at the correct world position but with the current camera. This is correct behavior — but Remix's generation hash includes the final transformed positions, so the replayed draw produces a different generation hash than the original. This means Remix may not recognize replayed geometry as "the same object" and could create duplicate instances.

**Not blocking:** Asset hash uses model-space positions (stable), and generation hash flicker is cosmetic. But worth monitoring if Remix reports duplicate geometry counts growing over time.

### 3. Untested H3+H4 Changes Risk Regression

The H3 (water UV scroll via c6) and H4 (VP lock) changes are significant proxy modifications that have NOT been tested as a numbered build. The c6 detection (`d3d9_device.c:1307-1313`) routes water draws to the shader path, which means those draws bypass the FFP transform pipeline entirely. If any non-water draw accidentally matches the c6 signature (c6.y != 0 or c6.z != 0 or |c6.w| > 1e-4), it will be misclassified as animated and stay on the shader route, potentially breaking its hash stability.

**Validation:** Run `begin testing` as build 078. Check that draw count breakdown (S4 vs FLOAT3 vs shader-passthrough) matches build 077's numbers.

---

## Priority Experiments (Ranked)

### 1. [HIGH] Fresh Remix Capture to Update mod.usda Hashes
This is the **only** remaining blocker. Everything else is polish.
- Position Lara near the Peru stage
- Open Remix developer menu (X key per rtx.conf)
- Capture a frame
- Extract the 5 building mesh hashes from the capture
- Update mod.usda with new hashes
- Retest

**Expected outcome:** Red and green stage lights appear immediately.
**Validation:** Both colors visible in all 3 camera positions, lights shift with camera pan.

### 2. [HIGH] Build 078 — Test H3+H4 Changes
The proxy has changed significantly since build 077. Run `begin testing` to verify:
- Hash stability still holds
- Draw count breakdown is consistent
- Water surfaces render correctly on shader route
- VP lock doesn't cause visible artifacts

**Expected outcome:** Same hash stability as build 077, water UV animation visible.
**Validation:** Hash debug screenshots show same-colored geometry across camera positions.

### 3. [MED] Anchor to Lara's Always-Drawn Mesh (Fallback)
If fresh capture is delayed further, anchor a test light to Lara's body mesh hash (visible since build 071b). This provides a guaranteed-visible reference light that proves the anchor pipeline works at all camera positions, independent of building mesh hashes.

**Expected outcome:** Light attached to Lara visible everywhere she goes.
**Validation:** Light moves with Lara model, never disappears.

### 4. [MED] Test rtx.geometryHashGenerationRoundPosTo
Not currently configured. Adding this to rtx.conf could absorb float rounding differences in the generation hash:
```ini
rtx.geometryHashGenerationRoundPosTo = 0.01
```
**Expected outcome:** Generation hash flicker reduced or eliminated.
**Validation:** Hash debug mode 277 shows stable colors even during slow camera drift.

### 5. [MED] Chapter 2 Crash Fix Verification
The findings.md analysis identified the SectorPortalVisibility bounds persistence NOPs as the probable cause of chapter 2 crashes. The flags at 0x46D202/0x46D205 are already preserved (not NOPed) per the fix. But this hasn't been tested with a chapter 2 load.

**Expected outcome:** Chapter 2 loads without FAST_FAIL crash.
**Validation:** `livetools mem read 0x1158300 0x2E0` shows clean sector state after chapter transition.

### 6. [LOW] LOD Alpha Fade Investigation (Layer 14)
Address 0x446580, 10 callers. Still unexplored. Could fade geometry invisible at distance, which would break hash anchors for distant meshes. Low priority because geometry IS being submitted (3749 draws/scene) — if LOD fade were active, draw counts would be lower.

### 7. [LOW] Read TRLAU-Menu-Hook Source for Culling Addresses
The TRLAU-menu-hook (TheIndra55) has portal visualization for TRL Legend. Its source likely has sector/portal traversal addresses already resolved. Could reveal culling layer 14 (LOD alpha fade) or layer 15 (scene graph sector early-outs) addresses without RE work.

---

## Stale Experiments to Remove from Pending List

| Experiment | Why Stale |
|-----------|-----------|
| Tiered frustum threshold binary search | Frustum threshold already at -1e30, all 36 culling layers mapped. Geometry IS submitting at 3749 draws/scene. Problem is hashes, not culling. |
| Per-frame view distance ramping | Same — culling is solved. |
| Aggressive Remix anti-culling (fovScale=10, etc.) | Anti-culling disabled because proxy's own patches + draw cache handle it. Re-enabling would double-count and cause freezes. |
| Box culling around player (Painkiller approach) | All culling layers already patched. No remaining culling to work around. |
| Vertex buffer content stability check | Confirmed stable — `useVertexCapture=True` with CPU SHORT4→FLOAT3 expansion gives deterministic VB content. |
| cdcEngine SceneLayer::Render identification | cdcEngine decompilation doesn't contain SceneLayer/ISceneCell code yet (last commit Dec 2024). |

---

## Config Changes to Test

| Setting | Current | Proposed | Expected Effect |
|---------|---------|----------|----------------|
| `rtx.geometryHashGenerationRoundPosTo` | (unset) | `0.01` | Absorb float jitter in generation hash |
| `rtx.fallbackLightMode` | `0` (disabled) | Keep at `0` | Correct — fallback light masked stage lights in builds 019-037 |
| `rtx.antiCulling.object.enable` | `False` | Keep at `False` | Correct — proxy handles anti-culling via draw cache |

No config changes recommended until after the fresh capture resolves the hash blocker.

---

## Community Intel (2026-04-28)

### RTX Remix (NVIDIAGameWorks/rtx-remix)
- **remix-1.3.6** (Jan 27, 2026): Vertex shader precision fix ("Fixed precision issues in vertex shader based games — wobbling or explosions"). Directly relevant to TRL's VS+FFP proxy pipeline. Verify the game is running 1.3.6+.
- **remix-1.3.6**: Vertex color as baked lighting with FF state heuristic. May affect how Remix interprets FFP draws. Test if Lara's shading changes.
- **remix-1.3.6**: Fallback directional light fix — confirms `fallbackLightMode=1` is more reliable. Currently disabled (mode=0) to avoid masking stage lights, which is correct.
- **remix-1.3.6**: `user.conf`/`rtx.conf` split now properly handled in UI — validates the build 075 fix.
- **remix-1.4.2** (Apr 21, 2026): No changes relevant to hash stability, culling, or FFP. Focused on VFX/shaders/UI.
- **No new `fusedWorldViewMode`, hash rule, or anti-culling settings in either release.**

### dxvk-remix (NVIDIAGameWorks/dxvk-remix)
- No commits touching vertex capture, geometry hashing, hash rules, `fusedWorldViewMode`, FFP, or anti-culling in March-April 2026. All changes are rendering features (SSS, PSR, POM, etc.).
- **Conclusion:** No upstream fixes coming for the hash blocker. Fresh capture + hash update is the only path.

### TRLAU-Menu-Hook (TheIndra55)
- No new commits in March-April 2026.
- Has portal visualization source code for TRL Legend — likely contains sector/portal traversal addresses. Worth reading for unexplored culling layers 14-15.

### cdcEngine (TheIndra55)
- No new commits since December 2024. SceneLayer/ISceneCell not yet decompiled.
- Not useful for the current blocker.

---

## Summary

The project is 15 days stalled on a known-easy fix (fresh Remix capture for hash IDs). The proxy has untested H3+H4 changes that need a build 078 test. All 36 culling layers are mapped (32 patched, 2 irrelevant, 2 unexplored but low-priority). The replacement pipeline is confirmed working. No upstream Remix changes help — the fix is local.

**Single highest-priority action:** Fresh Remix capture near Peru stage → extract mesh hashes → update mod.usda → test. This should produce the first PASS build with visible red+green stage lights.
