# Build 020 -- FAIL (lights partial)

**Date:** 2026-03-25 07:31 UTC-5
**Result:** FAIL -- red light missing in 2/3 clean screenshots
**Build type:** Proxy rebuild + automated two-phase test with randomized movement

## Changes Since Last Build

- **Fixed screenshot selection**: Previous builds (including build-019) were evaluating the WRONG screenshots -- camera-pan screenshots from menu_nav instead of the actual post-movement screenshots. The original macro's `]` keys during camera pan produced 2 extra screenshots that were being mistaken for movement screenshots. Build-019's PASS was a false positive.
- **Fixed movement input delivery**: Added `focus_hwnd()` re-call before HOLD tokens in `send_keys()` (gamectl.py). Movement now works reliably in both phases.

## Test Parameters

- Randomized movement: A hold 7781ms, D hold 3595ms
- 5 screenshots per phase (2 camera-pan + 3 movement)
- Correct evaluation uses last 3 screenshots (movement ones)

## Phase 1: Hash Debug (Asset Hash View 277)

| Screenshot | Position | Hash Stability |
|-----------|----------|---------------|
| hash-debug-1-baseline | Center path, facing forward | N/A (reference) |
| hash-debug-2-post-A-strafe | Far left, new area visible | Stable -- new geometry has consistent colors |
| hash-debug-3-post-D-strafe | Partially back right | Stable -- ground retains same color |

**Result:** PASS -- geometry hash colors consistent. Movement clearly visible.

## Phase 2: Clean Render (RTX Path-Traced)

| Screenshot | Red Light | Green Light | Lara Position |
|-----------|-----------|-------------|---------------|
| clean-render-1-baseline | Visible (left) | Visible (right) | Center path |
| clean-render-2-post-A-strafe | NOT visible | Visible | Far left, past light zone |
| clean-render-3-post-D-strafe | NOT visible | Visible (dim) | Still left of center |

**Result:** FAIL -- red light visible in only 1/3 screenshots. A strafe (7.8s) moved Lara too far from the dual-light area. D strafe (3.6s) was too short to return her.

## Root Cause

The A hold duration (7781ms) moved Lara completely out of the area where both stage lights are visible. The D hold (3595ms) wasn't long enough to bring her back. Both lights are only visible from the starting area near the junction of the red/green light zones.

## Proxy Log

- No crashes
- Culling patches active: frustum cull ret (0x407150) + 7 NOP scene traversal cull jumps

## Proxy Source

Included in this build folder for reproducibility.
