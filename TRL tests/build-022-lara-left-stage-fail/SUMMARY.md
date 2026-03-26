# Build 022 — lara-left-stage-fail

## Result

**FAIL** — Only 1 of 3 clean render screenshots shows both stage lights. Shots 2 and 3 show Lara has left the stage area (shot 2: completely black; shot 3: outdoor castle/fortress at night). Hash debug shots also show 3 wildly different scenes, suggesting screenshots are captured across level/area transitions rather than stable gameplay in one location.

## What Changed This Build

No proxy code changes from previous build. This is a clean re-run with randomized movement:
- A hold: 2588ms
- D hold: 3362ms

## Proxy Log Summary

- Patches applied cleanly:
  - Frustum cull function patched to RET at **0x407150**
  - Scene traversal cull jumps NOPed: **7 sites**
- `vpValid=1` throughout (no invalid viewport draws)
- `skippedQuad=0` (no quads skipped)
- Draw counts: **2,184,243** (main session), **251**, **257** (cleanup/teardown)
- No crashes, no errors

## Retools Findings (from static-analyzer subagent)

*Static-analyzer still running at time of packaging — see findings.md for full output when available.*

Key patch sites to verify:
- `0x407150`: Should be `RET` (C3)
- `0x4070F0 + N`: Should be `NOP` bytes (90) at 7 cull jump sites

## Ghidra MCP Findings

Decompiled `RenderLights_FrustumCull` (0x0060C7D0):
- **Frustum culling code is still active** — 6-plane frustum test loop (`FUN_0060b050`) runs every frame
- Reads `DAT_00f2a0d4` (g_cullMode_pass1) and `DAT_00f2a0d8` (g_cullMode_pass2) for D3DRS_CULLMODE
- Lights that fail the frustum test are deferred; their vtable draw dispatch is skipped in the main loop
- The function correctly sets multiple render states (stencil, Z-func, blend ops) before dispatching lights
- No proxy-side changes affected this function — it is executing normally

## Open Hypotheses

1. **Lara moved out of the stage room** — The D hold of 3362ms may be too long, walking Lara past the stage lights area and into adjacent geometry or off a ledge. This explains: clean shot 1 shows both lights (Lara is mid-stage), then she exits → black (no lit area), then outdoor area loads.

2. **Screenshot timing spans transitions** — The 3 clean render shots (02:47:50, 02:47:54, 02:48:00) span 10 seconds. With A+D hold totaling ~6s, the last screenshots are captured after Lara has completed the strafe and entered a new area.

3. **Hash debug instability** — The 3 hash debug shots show completely different scenes (outdoor jungle, dark red interior, architectural area), suggesting either camera cuts or Lara moving to entirely different regions during the hash test phase too.

4. **False positive risk** — If Lara consistently ends up back in the same position (build-021 precedent), shorter hold times may keep her in the stage area for all 3 shots.

## Next Build Plan

- **Reduce D hold time** to ~1500-2000ms to keep Lara within the stage area for all 3 screenshots
- OR investigate why the stage is not stable across all 3 shots — compare with build-019 (miracle) movement parameters
- Verify with build-019 SUMMARY what A/D hold times produced a confirmed pass
- Consider adding a stabilization delay after movement ends before capturing screenshots, if possible
