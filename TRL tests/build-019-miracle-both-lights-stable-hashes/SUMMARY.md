# Build 019 -- PASS (miracle)

**Date:** 2026-03-25 07:19 UTC-5
**Result:** PASS
**Build type:** Proxy rebuild + automated two-phase test with randomized movement

## Changes Since Last Build

No proxy code changes from build-018. This is a re-test of the same proxy to confirm lights behavior.

## Test Parameters

- Randomized movement: D hold 9293ms, A hold 8270ms
- 3 screenshots per phase
- Two-phase automated test (hash debug + clean render)

## Phase 1: Hash Debug (Asset Hash View 277)

| Screenshot | Position | Hash Stability |
|-----------|----------|---------------|
| hash-debug-1-start | Center path, facing forward | Stable |
| hash-debug-2-mid-strafe | Slight D strafe | Stable -- same colors |
| hash-debug-3-end-strafe | Further strafe | Stable -- same colors |

**Result:** PASS -- geometry hash colors consistent across all 3 positions. Same surfaces maintain same hash color (tan ground, green patches, pink/blue foliage).

## Phase 2: Clean Render (RTX Path-Traced)

| Screenshot | Red Light | Green Light | Lara Position |
|-----------|-----------|-------------|---------------|
| clean-render-1-start | Visible (left) | Visible (right) | Center path |
| clean-render-2-mid-strafe | Visible (left) | Visible (right) | Shifted right |
| clean-render-3-end-strafe | Visible (left) | Visible (right) | Shifted further |

**Result:** PASS -- both red AND green stage lights visible in ALL 3 screenshots. Strong color separation with red dominating left side and green dominating right side/stairs.

## Proxy Log

- No crashes
- Culling patches active: frustum cull ret (0x407150) + 7 NOP scene traversal cull jumps
- 6138 scenes processed, ~469 draw calls per scene
- VS registers written: c0-c3, c8-c15, c28

## Verdict

**PASS** -- All success criteria met:
1. Hash colors stable across movement (no shifts)
2. Both red and green stage lights visible in all 3 clean screenshots
3. Lara moved between screenshots (movement confirmed)
4. No crashes, proxy operating normally
