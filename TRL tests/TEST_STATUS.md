# TRL RTX Remix — Test Status Report

**Last reviewed:** 2026-03-25
**Builds reviewed:** 001, 002, 016, 017, 018
**Overall status:** FAILING — anti-culling incomplete, geometry disappears on camera movement

---

## Current Findings

### What Works

1. **Asset hash stability (static camera):** Hash colors are consistent frame-to-frame and across game restarts when the camera doesn't move. The hash rule `indices,texcoords,geometrydescriptor` (excluding positions) produces deterministic, session-reproducible hashes.

2. **Character model (Lara) hashes are rock-solid.** Cyan/green hash color is identical across all positions, strafes, and sessions in every build tested. With `ENABLE_SKINNING=0`, the raw vertex data doesn't change.

3. **Proxy transform pipeline:** View/Proj matrices read from game memory (`0x010FC780`, `0x01002530`), World computed via WVP decomposition. 100% of draws processed (passthrough=0, xformBlocked=0) in all builds.

4. **Per-frame frustum threshold re-stamp:** BeginScene overwrites `0xEFDD64` to `0.0f` every scene, defeating the game's per-frame recomputation from camera parameters.

5. **Input delivery fixed (build 018):** `SendInput` now sends `KEYEVENTF_SCANCODE` flag so DirectInput-based TRL actually receives A/D keypresses. All builds before 018 had false-positive movement tests — Lara never actually moved.

### What Fails

1. **Stage lights disappear on D-strafe (build 017, 018).** The green light (`mesh_AB241947CA588F11`) vanishes after ~8s of rightward movement. The red light sometimes also vanishes. This means either:
   - The anchor geometry is being culled by an unpatched path
   - The anchor geometry is unloaded by level streaming (not frustum culling)
   - The mesh hash changed at the new position (LOD swap)

2. **World geometry hash colors shift (build 017).** Ground, rocks, and foliage show different hash colors after D-strafe vs baseline. Lara stays stable but environment hashes change — either different geometry is loaded or the same geometry produces different hashes.

3. **Incomplete anti-culling.** Three layers patched but geometry still disappears:
   - Layer 1: `0x407150` → `ret` (per-object frustum test)
   - Layer 2: threshold `0.0` + 7 NOP jumps (scene traversal distance/boundary checks)
   - Layer 3: `D3DCULL_NONE` (backface culling)
   - **Missing:** level streaming/sector system, LOD alpha fade (`0x446580`), possible scene graph sector early-outs

### Hurdles

1. **Level streaming vs frustum culling ambiguity.** When geometry disappears at distance, it's unclear whether it's frustum-culled (patchable) or streamed out by the sector/room system (much harder to disable). Need to trace the scene graph to distinguish these paths.

2. **`0x407150 → ret` may be over-aggressive.** This patches the entire `SceneTraversal_CullAndSubmit` function (4049 bytes) to return immediately. Some geometry may depend on this function for *submission*, not just visibility marking. The ret patch might prevent geometry from being submitted at all in certain configurations.

3. **LOD system at `0x446580`** has 10 callers and may fade geometry to invisible at distance. Not yet patched or investigated.

4. **False positives in earlier builds.** Builds 001-016 passed hash stability tests but Lara wasn't moving — the scancode fix in build 018 revealed that all prior movement tests were invalid. Only the static-camera baseline results are trustworthy.

---

## Build-by-Build Summary

| Build | Date | Result | Key Change | Key Finding |
|-------|------|--------|------------|-------------|
| 001 | 2026-03-24 | PASS | Baseline passthrough + transform override | Hashes stable (static camera), cross-session reproducible |
| 002 | 2026-03-24 | PASS | Stable hash confirmation | Two-phase test confirms hash stability, RTX path tracing works |
| 016 | 2026-03-25 | PASS* | 3-layer anti-culling, frustum threshold fix | Draw count stabilized 91.8K; *movement was broken (false positive) |
| 017 | 2026-03-25 | FAIL | NOPs moved into proxy, BeginScene re-stamp | Lights disappear after D-strafe, hash colors shift |
| 018 | 2026-03-25 | FAIL | Scancode fix — movement actually works now | Green light disappears on D-strafe; confirms culling still active |

*Build 016 PASS is unreliable — movement input wasn't reaching the game.

---

## What's Been Done

- [x] D3D9 proxy DLL with shader passthrough + transform override
- [x] Asset hash rule tuned (`indices,texcoords,geometrydescriptor`, excluding positions)
- [x] View/Proj matrix reading from game memory
- [x] World matrix decomposition from WVP
- [x] Frustum threshold patch (`0xEFDD64 → 0.0f`) with per-frame re-stamp
- [x] Per-object frustum function patched (`0x407150 → ret`)
- [x] 7 scene traversal cull jumps NOPed (distance + boundary checks)
- [x] D3D backface culling forced to `D3DCULL_NONE`
- [x] Automated two-phase test pipeline (hash debug + clean render)
- [x] Scancode input fix for DirectInput-based games
- [x] Stage light anchoring via mod.usda mesh hashes
- [x] `user.conf` override issue identified and fixed (`rtx.enableReplacementAssets`)
- [x] VK_MAP `]` key added for NVIDIA screenshot capture

## What Still Needs To Be Done

- [ ] **Investigate level streaming/sector system** — determine if geometry disappearance is streaming (room unload) or an unpatched culling path
- [ ] **Trace `0x446580` LOD fade system** — 10 callers, may fade geometry to invisible at distance
- [ ] **Re-evaluate `0x407150 → ret` patch** — may be too aggressive; consider patching individual cull checks inside the function instead of disabling it entirely
- [ ] **Identify scene graph sector boundaries** — the scene traversal may have sector-based early-outs not covered by current NOP patches
- [ ] **Test with shorter strafe distances** — determine the exact distance threshold where lights disappear (helps distinguish culling vs streaming)
- [ ] **Achieve a "miracle" build** — both stage lights visible in ALL positions (baseline + A-strafe + D-strafe) with stable hash colors

---

## Review Schedule

This document is reviewed and updated every 5 commits to track progress across builds.
