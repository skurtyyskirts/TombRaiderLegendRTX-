# Build 035 — Sector Light Gate NOP + Directional Red Fallback

## Result
**FAIL** — Both lights visible at baseline. After movement: red (distant fallback) OR green (sphere) visible, but not both simultaneously. The two light types have incompatible intensity profiles.

## What Changed This Build
- **NEW: Sector light count gate NOP at 0xEC6337** — Forces all sectors to load their static light count from `[sector_data+0x664]`, regardless of the visibility flag at `param_2[10]`. This makes the green light's anchor mesh always submit via the native light rendering pipeline.
- **NEW: Light_VisibilityTest patch at 0x60B050** — `mov al, 1; ret 4` forces all lights to pass the pre-frustum visibility gate.
- **NEW: Red directional fallback light** — `rtx.fallbackLightMode = 2`, `rtx.fallbackLightRadiance = 3.5, 0.3, 0.3`. Provides ubiquitous red illumination.
- Green stage light on `mesh_AB241947CA588F11` at 10K intensity, exposure 5.
- Red light on `mesh_6AF01B710C2489F5` (changed from purple to red color).

## Proxy Log Summary
- All patches confirmed: frustum threshold, 7/7 cull jumps, frustum RET, 2/2 sector NOPs, cull globals, light frustum NOP, Light_VisibilityTest, sector light gate NOP.
- Draw counts: 1,440-189,960. vpValid=1.

## Retools Findings
- Light_VisibilityTest is `__thiscall` with 1 stack arg. Performs distance/sphere/cone checks per light type. Our `ret 4` is correct.
- Three-gate culling pipeline confirmed: Gate 1 (0x60B050 patched), Gate 2 (0x60CE20 patched), Gate 3 (0xEC6337 patched for sectors with static data).
- Sector light list at `+0x1B0/+0x1B8` populated by `FUN_00EC62A0` from `[sector_data+0x664]`.

## Ghidra MCP Findings
- `RenderScene_Main` (0x603810) iterates ALL sector entries, calls `RenderScene_LightPass` for ones with `+0x84 + +0x94 != 0`.
- `RenderScene_LightPass` (0x60E2D0) calls `RenderLights_FrustumCull` which iterates per-sector light list.
- `FUN_00EC62A0` (0xEC62A0) populates light count from `[sector_data+0x664]`. Gate at 0xEC6337 now NOPed.
- Only `mesh_AB241947CA588F11` (green light) is in a sector with non-zero static light data. All other light anchors are in sectors with 0 static lights.

## Open Hypotheses
1. **Per-sector static light data**: Most sectors have `[sector_data+0x664] = 0` (no native lights). The sector light gate NOP only helps sectors with non-zero static data. Need to find a way to populate light data for ALL sectors or use a global light list.
2. **Draw call replay**: The proxy could record light volume draw calls during the first frame and replay them on subsequent frames to keep anchor hashes always present.
3. **Alternative anchoring**: Find a mesh that's always rendered (Lara's body, ground plane) and anchor both lights to it.

## Next Build Plan
1. Investigate patching the sector data directly to give all sectors a non-zero light count
2. Or implement draw call recording/replay in the proxy for light volume meshes
3. Or find Lara's body mesh hash and anchor both lights to it
