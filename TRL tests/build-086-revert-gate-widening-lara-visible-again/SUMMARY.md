# Build 086 — revert variant-7 gate widening — Lara visible again

**Date:** 2026-05-19 11:10 CT
**Result:** REGRESSION FIXED. Lara visible at the menu with normal idle animation; hash colors drift again (cache inactive at the menu — build 081 design behavior).

## What Changed

Single revert in `proxy/d3d9_device.c` (`TRL_ForceSkinnedNullVS`):

```diff
-    int isLaraClass = (self->curDeclPosType == D3DDECLTYPE_FLOAT3
-                       && self->curDeclHasColor);
+    int isLaraClass = (self->curDeclPosType == D3DDECLTYPE_FLOAT3
+                       && self->curDeclTexcoordType == D3DDECLTYPE_FLOAT4);
```

Variant 7's widened gate caught the menu Lara decl A and forced her onto
the null-VS FFP path. Her menu animation is shader-skinned — without the
VS, FFP produced degenerate geometry and she vanished. Debug view 277
still visualized the cached bind-pose mesh, so the test gate falsely
declared PASS in builds 083-085.

Retained from earlier iterations:
- Build 084 lookup-key relaxation: `Lara_LookupCacheSlot` keys on
  `(nv, pc, tex0)` only — `bvi` / `mi` are no-ops for matching.
- Build 085 nv-cap raise: `useLaraCache` accepts `nv <= 65535`.

Both are inert with the restored restrictive gate, but ready to engage
in scenes where Decl C actually appears.

## Proxy Log Summary

```
SkinnedFloat3Route: null_vs (Lara-class FLOAT3+FLOAT4tex forced to null-VS for stable asset hash)
LaraClassBindPoseCache: 1
```

No `MOVABLE forced null_vs`, no `LaraVB cache hits=`, no `first bind-pose
snapshot committed`. Gate stays closed at the main menu — intended.

## Screenshot Analysis

Both shots show Lara visible on the right side of the menu. Her hand,
shoulder, and head positions differ between shot 1 (11:10:16) and shot 2
(11:10:19) — three seconds of normal idle animation. Menu UI overlay
rendered correctly (Resume Game / Load / Save / Croft Manor / Options /
Extras / Exit Game). Title text and play-time stamp render normally.

Hash colorization across Lara's body changes between frames because the
live CPU-skinned vertex buffer content is forwarded to Remix unmodified.
That is the pre-cache behavior — drift is expected at the menu under the
restored gate.

## Lessons Logged

- Recorded in `build-085-FAIL-regression-lara-invisible-debug277-misled-
  pass/REGRESSION_NOTE.md`: hash-color stability in debug view 277 is
  not proof of correctness when the geometry might be off-screen or
  degenerate in normal rendering.
- The `--main-menu` test mode added in build 082 needs a second phase
  that captures debug view 0 (clean render) and verifies Lara is visible
  before declaring PASS — same Phase 1 + Phase 2 pattern that
  `do_test_hash_stability` already uses for in-level scenes.

## Open Questions

- Is main-menu hash stability even a meaningful target? Replacement
  asset authoring in the Toolkit happens against in-level meshes
  (stage geometry, Lara during gameplay). Menu Lara's frame-to-frame
  hash drift may simply not matter for the project's actual goals.
- If the answer is "we want it anyway," the path is a separate menu
  routing that preserves the vertex shader path but snapshots its
  output (Stream Out / transform feedback equivalent in D3D9), not the
  null-VS FFP redirect this cache uses.
