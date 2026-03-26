# Build 027 — Lights Fade at Distance

## Result

**FAIL** — Both stage lights visible in clean screenshot 1, but screenshots 2 and 3 lose lights as Lara walks far from the stage area. Randomized movement (A 1.1s, D 8.9s) took Lara too far right.

## What Changed This Build

New patches added to d3d9_device.c since build-026:
- **LightVolume visibility-state NOPs** at 5 addresses in `LightVolume_UpdateVisibility` (0x6124E0): NOP'd the `cmp visState,1; jg skip` checks at 0x6125EC, 0x61264C, 0x6126AA, 0x612701, 0x61279A
- These force all light sub-elements to write render data regardless of frustum culling

All previous patches remain active:
- Frustum cull function RET at 0x407150
- 7 scene traversal cull jump NOPs at 0x4070F0
- Light broad-visibility skip NOP at 0x60CDE2
- Light frustum plane test jump NOP at 0x60CE20
- Light scene-list pending-flag skip NOP at 0x603832
- Light render-gate pending-flag check NOP at 0x60E30D

## Proxy Log Summary

- **Draw counts**: ~1416-1440 total per scene, all processed, 0 skipped
- **vpValid**: 1 (always valid)
- **skippedQuad**: 0
- **passthrough**: 0
- **xformBlocked**: 0
- No crashes, no errors
- **Suspicious**: LightVolume visibility-state NOP log line missing from proxy output — these new patches may not have been applied despite being in the source

## Retools Findings

Static analyzer verified all 6 proxy-reported patch sites against on-disk PE:
- **0x407150** (cull function): On-disk has full prologue (`push ebp; mov ebp, esp`), confirming proxy patches RET at runtime
- **0x4070F0** (scene traversal): On-disk has original code (AND, PUSH, CALL, MOVAPS), no NOPs — runtime-only patches
- **0x60CDE2** (light broad-visibility): Confirmed `74 61` (JE +0x61) on disk, proxy NOPs at runtime
- **0x60CE20** (light frustum plane): Confirmed `0F 8B 8D 01 00 00` (JNP +0x18D) on disk, proxy NOPs at runtime
- **0x603832** and **0x60E30D**: Pending-flag patches also runtime-only
- Draw counts steady at ~1440/scene, peak ~190K at scene 960
- All patches confirmed applied correctly by proxy at runtime

Key finding from prior analysis: deferred lights (failing frustum test) still get drawn via a second loop at 0x60CF18 with mode=0. So NOPing the frustum jump changes mode from deferred(0) to immediate(1), not whether they draw at all.

## Ghidra MCP Findings

Ghidra MCP not available in this session. Prior findings from findings.md:
- `RenderLights_FrustumCull` (0x60C7D0) uses cull globals at 0xF2A0D4/D8
- Renderer layout confirmed: device at +0x0C, cached cull at +0x144, flags at +0x464
- Light dispatch path involves broad visibility check, 6-plane frustum test, pending-flag gate, and render-gate check — all now NOP'd

## Open Hypotheses

1. **LightVolume visibility-state patches not applied**: The proxy log doesn't show the expected "NOPed light visibility-state checks: N/5" line. VirtualProtect may have failed at those addresses, or the build didn't pick up the new code. This is the most likely cause — the new layer of culling at the sub-element level in `LightVolume_UpdateVisibility` is still active.

2. **Light attenuation range**: Even with all culling disabled, lights have finite range. At large distances the light simply doesn't illuminate geometry. This would be a game design limit, not a culling issue. However, build-019 (the miracle build) passed with random movement, suggesting the stage area range should cover normal walking distances.

3. **Additional culling layer**: There may be yet another visibility check in the light rendering pipeline between the frustum cull and the actual draw call that we haven't identified.

## Next Build Plan

1. **Verify LightVolume NOPs are being applied**: Add explicit error logging if VirtualProtect fails at the 5 visibility-state addresses. Check if the addresses are correct in the running binary.
2. **If NOPs confirmed applied**: Investigate light attenuation range — the game may have a range falloff that's separate from culling. Check for a light range/radius field in the light data structure that could be patched to a larger value.
3. **If NOPs not applied**: Fix the address targets (the function may have been inlined differently) and rebuild.
