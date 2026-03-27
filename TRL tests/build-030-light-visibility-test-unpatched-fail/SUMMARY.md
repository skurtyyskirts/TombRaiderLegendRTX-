# Build 030 — Light Visibility Test Unpatched (FAIL)

## Result

**FAIL** — Only 1 of 3 clean render screenshots shows both red and green stage lights. Shots 2 and 3 are extremely dark with no visible lights — Lara walked far enough from the stage that `Light_VisibilityTest` rejected the lights before the frustum test could run.

## What Changed This Build

No proxy code changes from build-029. This is a baseline run to confirm the failure mode and gather Ghidra MCP evidence for the root cause.

## Proxy Log Summary

- **No crashes** — game ran and closed cleanly for both phases
- **All patches applied successfully**:
  - Frustum threshold set to -1e30
  - 7/7 cull jumps NOPed in scene traversal
  - Frustum cull function patched to RET at 0x407150
  - 2/2 sector visibility checks NOPed
  - Cull mode globals stamped to D3DCULL_NONE
  - Light frustum rejection NOPed at 0x0060CE20
- **Draw counts**: Steady at ~93,600 processed per scene during gameplay, vpValid=1 across all scenes
- **Zero skips**: skippedQuad=0, passthrough=0, xformBlocked=0

## Retools Findings

Static analyzer ran disassembly verification on patch sites (0x407150, 0x4070F0) and proxy log addresses. Full findings in `patches/TombRaiderLegend/findings.md`.

## Ghidra MCP Findings

**Root cause identified: `Light_VisibilityTest` (0x0060B050) is an unpatched culling gate.**

The light rendering pipeline in `RenderLights_FrustumCull` (0x0060C7D0) has TWO culling stages:

1. **`Light_VisibilityTest`** (0x0060B050) — called per-light BEFORE the frustum test. For type 0/1 lights, calls sub-functions:
   - Type 0: `FUN_0060ad20` — sphere/distance visibility check
   - Type 1: `FUN_005f9a60` — cone/spotlight visibility check
   - Type 2: **Always returns 1** (visible)
   - If this returns 0, the light is **completely skipped** — never reaches the frustum test

2. **Frustum 6-plane test** — runs only if visibility test passed. The proxy already NOPs the rejection jump at 0x0060CE20, so all lights that reach this stage are rendered.

The proxy patches stage 2 but NOT stage 1. When Lara walks away from the stage lights, `Light_VisibilityTest` rejects them (distance/sector check fails), and they vanish before the frustum NOP can help.

**Only 1 caller** of `Light_VisibilityTest` exists (from `RenderLights_FrustumCull`), so patching it has no side effects.

## Open Hypotheses

1. **Primary (confirmed by Ghidra)**: `Light_VisibilityTest` at 0x0060B050 needs to be patched to always return 1. This is the remaining light culling gate. Patch: `mov al, 1; ret 4` (5 bytes at 0x0060B050).

2. **Secondary**: After patching `Light_VisibilityTest`, there may still be sector-level light list population that prevents lights from even entering the `RenderLights_FrustumCull` loop. The loop iterates over `*(iStack_160 + 0x1B0)` lights from array at `*(iStack_160 + 0x1B8)`. If the sector system doesn't populate this list with all lights, they won't be iterated. This would require deeper investigation.

## Next Build Plan

**Patch `Light_VisibilityTest` to always return visible:**

Add to `d3d9_device.c`:
```c
#define TRL_LIGHT_VISIBILITY_TEST_ADDR 0x0060B050
```

In the patching section, add:
```c
/* Force Light_VisibilityTest to always return 1 (visible).
 * This is the pre-frustum gate that rejects lights by distance/sector.
 * __thiscall with 1 stack param: mov al, 1; ret 4 */
{
    unsigned char *p = (unsigned char *)TRL_LIGHT_VISIBILITY_TEST_ADDR;
    if (VirtualProtect(p, 5, PAGE_EXECUTE_READWRITE, &oldProtect)) {
        p[0] = 0xB0; p[1] = 0x01;           /* mov al, 1 */
        p[2] = 0xC2; p[3] = 0x04; p[4] = 0x00; /* ret 4 */
        VirtualProtect(p, 5, oldProtect, &oldProtect);
        log_str("  Patched Light_VisibilityTest to always return visible\r\n");
    }
}
```

**Expected result**: Lights should remain visible regardless of Lara's position, since both culling stages (visibility test + frustum test) would be bypassed. If lights still disappear, the issue is in the sector-level light list population (hypothesis #2).
