# Build 018 — Scancode Fix: Movement Now Works, Lights PARTIAL FAIL

**Date:** 2026-03-25
**Build:** Shader passthrough + transform override + anti-culling + scancode input fix
**Result: FAIL — Green stage light disappears when camera moves far enough (D-strafe)**

## What Changed (vs Build 016)

| Change | Before | After |
|--------|--------|-------|
| SendInput key delivery | VK-only (no `KEYEVENTF_SCANCODE`) | VK + scancode flag |
| Movement during tests | Lara never moved (false positives) | Lara moves correctly |
| Test validity | Invalid — static camera | Valid — camera actually moves |

## Key Discovery: Previous Tests Were False Positives

Build-016 and the initial build-018 run reported PASS, but **Lara never actually moved** during testing. The A/D keypresses weren't reaching the game because `SendInput` was sending virtual key codes only. TRL uses DirectInput for gameplay input, which reads scancodes. Adding `KEYEVENTF_SCANCODE` (0x0008) to `_make_key_input()` in `livetools/gamectl.py` fixed input delivery.

**All previous "PASS" results with movement should be considered unverified** — they only proved the static starting viewpoint was correct.

## Test Results

### Phase 1 — Hash Debug (Asset Hash View 277)
- **Baseline**: Standard Bolivia start position, colors look normal
- **After A-strafe (7.5s)**: Camera shifted dramatically left — different geometry visible, large black regions (sky/void)
- **After D-strafe (8.1s)**: Camera shifted right — yet more different geometry, foliage and rocks from different angle
- **Movement confirmed**: Lara is in visibly different positions across all 3 screenshots

### Phase 2 — Clean Render
- **Baseline**: Both red AND green stage lights visible
- **After A-strafe**: Both red AND green stage lights visible, Lara shifted slightly
- **After D-strafe**: **ONLY RED light visible, GREEN LIGHT GONE** — Lara moved to a completely different area (ledge/rock), camera angle changed dramatically

### Failure Analysis

The green stage light disappearing on D-strafe means one of:
1. **Culling still active**: The geometry the green light is anchored to gets culled when the camera faces away from it
2. **Hash shift**: The anchor geometry's hash changed at the new position, detaching the light
3. **Geometry unloaded**: The game's level streaming unloaded the geometry entirely (not frustum culling, but distance-based LOD/streaming)

The 8-second D-strafe moved Lara far enough that the green light's anchor geometry is no longer rendered. The anti-culling patches (frustum ret + threshold + NOP jumps) may not cover all culling paths, or this could be level streaming rather than frustum culling.

## Configuration

| Setting | Value |
|---------|-------|
| Resolution | 1024x768 |
| Proxy mode | Shader passthrough + transform override |
| Skinning | Disabled |
| Frustum culling | 3-layer disable (ret + threshold 0.0 + 7 NOPs) |
| Asset hash rule | `indices,texcoords,geometrydescriptor` |
| Vertex capture | Enabled |
| Fused world-view | Disabled |
| Replacement assets | Enabled |
| Movement | Randomized (A=7489ms, D=8143ms) |

## Screenshots

| File | Description |
|------|-------------|
| `hash-debug-baseline.png` | Asset hash view — baseline position |
| `hash-debug-strafe-A.png` | Asset hash view — after 7.5s A strafe (moved left) |
| `hash-debug-strafe-D.png` | Asset hash view — after 8.1s D strafe (moved right) |
| `clean-render-baseline.png` | Clean render — baseline (both lights visible) |
| `clean-render-strafe-A.png` | Clean render — after A strafe (both lights visible) |
| `clean-render-strafe-D.png` | Clean render — after D strafe (GREEN LIGHT MISSING) |
