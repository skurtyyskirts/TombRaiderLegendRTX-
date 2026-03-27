---
name: TRL Renderer Architecture
description: Tomb Raider Legend renderer VS constant register layout, matrix upload pipeline, light class vtable hierarchy, and 5-layer culling/visibility system
type: project
---

Tomb Raider Legend uses a custom D3D8 renderer (converted to D3D9 via dxwrapper). Key architectural findings:

- Main renderer object accessed via global at 0x01392E18 -> [root+0x20] -> device at this+0xC
- Matrices stored row-major internally, transposed to column-major for HLSL upload
- Primary matrix upload via Renderer_UploadViewProjMatrices (0xECBB00): uploads c0-c7 (WVP) and c8-c15 (VP) as two 8-register batches
- Dirty flags at this+0x580 (view) and this+0x581 (proj) control which batch uploads
- Generic SetVSConstantF wrapper at 0xECBA40 with 34 callers covering c0 through c96
- Blend mode switcher at 0xECBC20 is NOT a constant upload function (maps blend IDs to render states)

**Light class hierarchy (no RTTI):**
- BaseLight: outer vtable 0xF085D4, inner vtable 0xF085E8 at this+8. Ctor 0x60B320.
- LightGroup (container): vtable 0xF08618, secondary 0xF08614 at this+4. Ctor 0x60C240.
- LightVolume (concrete light): vtable 0xF08740, secondary 0xF08738 at this+4. Ctor 0x6106A0. Size=0x1F0.
  - vtable[5] (+0x14) = GetBoundingSphere (0x612C80)
  - vtable[6] (+0x18) = **Draw (0x6124E0)** -- the concrete per-light draw method
- Multi-inheritance pattern: each light object has two vtable ptrs at [this+0] and [this+4].
- RenderLights_FrustumCull (0x60C7D0) dispatches via vtable[6] at +0x18 offset.

**3-Gate Light Culling Pipeline (discovered 2026-03-27):**

Gate 1 -- LightVisibilityCheck (0x60B050): Mode-dependent AABB test. For point lights (mode 1), computes bounding AABB and tests camera intersection via 0x5F9BE0. Returns 0 for distant lights, causing je at 0x60CDE2 to skip the light entirely. This is the PRIMARY reason lights disappear at distance. Patch: B0 01 C2 04 00 at 0x60B050.

Gate 2 -- Frustum Plane Test (0x60CDF1-0x60CE2D): 6-plane dot product loop. JNP at 0x60CE20 rejects lights outside frustum. Already patched with NOP in build 030.

Gate 3 -- Sector Light List (0x60E345): Checks [lightGroup+0x1B0] for light count. If 0, entire RenderLights_FrustumCull is skipped via je at 0x60E3B1. This is sector-scoped -- lights only exist in the sector they were placed in. If Lara crosses sector boundary, the new sector may have 0 lights, making both Gate 1 and Gate 2 patches irrelevant.

**Critical insight:** Even with Gates 1+2 patched, Gate 3 (sector light list) can prevent all lights from rendering if Lara enters a sector without lights. The sector light list is populated upstream, not by the render path itself.

**5-Layer Geometry Culling System (discovered 2026-03-26):**

Layer 1 -- Sector/Portal Visibility (0x46C180): The level is divided into 8 sectors stored in a fixed array at 0x11582F8 (0x5C bytes each). Only sectors with [entry+5]&0x8 set are rendered. This flag is computed per-frame from portal connectivity relative to the camera's current sector. This is the PRIMARY reason distant geometry disappears even when frustum culling is patched. To disable: NOP je at 0x46C194 (6 bytes) and jne at 0x46C19D (6 bytes).

Layer 2 -- Mesh-Level Frustum Cull (0x407150): Per-mesh AABB vs frustum planes. Already patched with RET.

Layer 3 -- Object Linked List Filtering (0x450BC7): Objects iterated from g_pObjectListHead (0x10C5AA4). Flag check [obj+0xA4]&0x800 and type dispatch via Object_HasComponentType (types 0x1F, 0x24, 0x2A).

Layer 4 -- Mesh Flags in Sector Renderer (0x46B7D0/0x46C320): Per-mesh flag checks [mesh+0x5C]&0x82000000 and [mesh+0x20]&0x1/0x20000/0x200000 skip individual meshes.

Layer 5 -- MeshSubmit Visibility Gate (0x454AB0): Called at top of MeshSubmit (0x458630), returns nonzero to cull.

**Render call chain:**
0x452510 -> 0x450DE0 -> 0x450B00 (RenderScene) -> 0x46C180 (sector vis) + 0x443C20 (scene traversal incl. 0x407150) + object linked list loop.

**Why:** Understanding all 5 culling layers is essential for RTX Remix hash stability. Patching only one layer (frustum cull) still leaves sector visibility hiding distant geometry.
**How to apply:** Use the register map in kb.h when writing replacement vertex shaders. c0-c3 = WVP, c8-c11 = View (transposed), c39 = utility {2,0.5,0,1}. For light patching, the critical address is LightVolume::Draw at 0x6124E0. For sector culling, patch 0x46C194 and 0x46C19D. For the full list see patches/TombRaiderLegend/findings.md and kb.h.
