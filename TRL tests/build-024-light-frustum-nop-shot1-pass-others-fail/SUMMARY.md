# Build 024 — light-frustum-nop-shot1-pass-others-fail

## Result

**FAIL** — Significant improvement over build-023: clean render shot 1 (center stage) shows **BOTH red AND green** stage lights. However shots 2 and 3 fail: shot 2 is completely black (Lara moved far left into a dark corner), shot 3 shows green only — no red. The light frustum NOP patches ARE working (confirmed by shot 1) but there is a remaining position-dependent issue — when Lara moves far enough from center, the red light is no longer submitted.

Hash debug shots: All 3 show Lara in different outdoor positions (movement confirmed). Hash colors appear consistent across positions but the outdoor geometry in all 3 shots makes hash stability for the stage area difficult to evaluate.

## What Changed This Build

Fixed build-023's critical bug: light frustum NOP patches are now applied to the CORRECT source file (`patches/TombRaiderLegend/proxy/d3d9_device.c`). Build-023 edited the repo-root `proxy/d3d9_device.c` which is not compiled.

Active patches this build (all 5 layers):
- **0x407150**: RET — frustum cull function returns immediately
- **Frustum threshold → 0.0**: reduces near-plane cull aggressiveness
- **0x4070F0 area: 7 NOPs** — scene traversal cull jumps disabled
- **0x60CDE2: 2 NOPs** (NEW, working) — broad-visibility skip disabled in `RenderLights_FrustumCull`
- **0x60CE20: 6 NOPs** (NEW, working) — frustum plane test jump disabled in `RenderLights_FrustumCull`

## Proxy Log Summary

- RET @ 0x407150: applied ✓
- Frustum threshold → 0.0: applied ✓
- NOPed scene traversal cull jumps: 7/7 ✓
- **NOPed light broad-visibility skip (0x60CDE2): applied ✓** ← first time working
- **NOPed light frustum plane test jump (0x60CE20): applied ✓** ← first time working
- vpValid=1 throughout
- skippedQuad=0
- Draw counts: 4,230,831 / 770 / 773

Movement: A hold 2495ms, D hold 8986ms (randomized)

## Retools Findings (from static-analyzer subagent)

Static-analyzer verified on-disk bytes at all patch sites:
- 0x407150: original prologue present (`55 8B EC...`) — RET applied at runtime only ✓
- 0x60CDE2: **`74 61`** (JE +0x61) confirmed — runtime NOP patch feasible ✓
- 0x60CE20: **`0F 8B 8D 01 00 00`** (JNP +0x18D) confirmed — runtime NOP patch feasible ✓

Full disassembly of the light loop in `RenderLights_FrustumCull`:
```
outer loop for each light:
  call FUN_0060b050          ; broad visibility check → AL
  test al, al
  je 0x60CE45                ; CULL JUMP 1 @ 0x60CDE2 — NOPed in build-024
  ; 6-plane frustum test inner loop:
  fcomp [esp+0x10]
  fnstsw ax
  test ah, 5
  jnp 0x60CFB3               ; CULL JUMP 2 @ 0x60CE20 — NOPed in build-024
  inc edx / add ecx,0x20
  cmp edx, 6 / jl loop_top
  ; all 6 planes passed → draw immediately
  push 1
  call [eax+0x18]            ; virtual draw call (mode=1)
```

Deferred lights (from 0x60CFB3) also get drawn in a second pass with mode=0. NOPing CULL JUMP 2 routes all lights to mode=1 (immediate) instead.

## Ghidra MCP Findings

Full decompilation of `RenderLights_FrustumCull` (0x0060C7D0) and its sole caller `FUN_0060E2D0` (0x0060E2D0).

**`RenderLights_FrustumCull` structure:** Iterates over a light list from `iStack_160+0x1B0` (count) and `iStack_160+0x1B8` (array pointer). For each light: calls broad-visibility check `FUN_0060b050`, then 6-plane frustum dot-product test. Both checks NOPed in build-024. Both lights should be drawn from this function regardless of position.

**`FUN_0060E2D0` — the critical gate:** `RenderLights_FrustumCull` is only called when `bVar6 = true`. `bVar6` requires ALL of:
1. `*(int *)(param_1 + 0x84) != 0` — a per-light-group flag
2. `*(char *)(DAT_01392e18 + 0x166) != '\0'` — global render lights enabled flag
3. `(*(byte *)(*(int *)(param_1 + 0x74) + 0x444) & 1) != 0` — light type flag
4. `*(int *)(param_1 + 0x1b0) != 0` — **light COUNT in list**

**Key hypothesis:** `param_1` is a zone/sector light-group structure. The light list (count at +0x1b0) is populated during scene traversal for the current visible zone. When Lara moves far enough from the stage, the red light's zone may become inactive, resulting in a 0 light count and `FUN_0060E2D0` not calling `FUN_0060C7D0` for the red light at all.

This is upstream of our frustum NOP patches — they work correctly inside the function, but the function may not be called for certain lights when the player moves.

## Open Hypotheses

1. **Zone-based light list population**: The stage lights belong to a zone/sector. When Lara walks far right (D hold ~9s), she may exit the red light's zone, removing it from the active light list. The scene traversal RET at 0x407150 may not affect light-zone activation separately from geometry-zone activation.

2. **Black shot is genuine darkness**: With D hold ~9s, Lara walks far beyond the stage. The black shot likely captures the game with Lara facing a wall or in a section with no light sources submitting geometry to RTX Remix.

3. **Green light is a closer zone**: The green light may be in a zone that remains active from Lara's rightward position (or is in a larger zone), while the red light's zone cuts off sooner. This would explain consistent red-only failure when moving right.

4. **Caller of `FUN_0060E2D0` has additional culling**: Unknown who calls `FUN_0060E2D0` and with what light lists. This call chain, upstream of our patches, may need investigation to ensure the red light group is always passed in.

## Next Build Plan

Build-025: Investigate zone-based light list population.

**Approach A — Trace `FUN_0060E2D0` with livetools:**
- Attach to running game, `trace 0x0060E2D0 --count 50 --read ecx` to log how many times it's called and with what `param_1` values
- While Lara is at center stage (both lights visible) vs. far right (red missing), compare the number of calls and what light lists are passed in
- If call count drops when moving right, the issue is in the caller of `FUN_0060E2D0`

**Approach B — Force light count at `param_1+0x1b0`:**
- If we can find the code that zeros out the light list count for the red light's zone, patch it to keep a minimum count of 1
- This would require finding who writes `param_1 + 0x1b0`

**Approach C — Trace who calls `FUN_0060E2D0`:**
- Get xrefs to 0x0060E2D0 via Ghidra, decompile the caller, find the light group enumeration logic
- Patch the condition that skips light groups to ensure all groups are always processed

Expected result for build-025: If the zone light list is forced to always include stage lights, all 3 clean render shots should show both lights.
