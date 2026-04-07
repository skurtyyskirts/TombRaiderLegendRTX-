---
name: TRL Renderer Architecture
description: Tomb Raider Legend renderer VS constant register layout, matrix upload pipeline, light class vtable hierarchy, 5-layer culling/visibility system, and full render dispatch chain from WinMain to DIP
type: project
---

Tomb Raider Legend uses a custom D3D8 renderer (converted to D3D9 via dxwrapper). Key architectural findings:

- Main renderer object accessed via global at 0x01392E18 -> [root+0x20] -> device at this+0xC
- Matrices stored row-major internally, transposed to column-major for HLSL upload
- Primary matrix upload via Renderer_UploadViewProjMatrices (0xECBB00): uploads c0-c7 (WVP) and c8-c15 (VP) as two 8-register batches
- Dirty flags at this+0x580 (view) and this+0x581 (proj) control which batch uploads
- Generic SetVSConstantF wrapper at 0xECBA40 with 34 callers covering c0 through c96
- Blend mode switcher at 0xECBC20 is NOT a constant upload function (maps blend IDs to render states)

**Full Render Dispatch Chain (discovered 2026-04-05):**

```
WinMain (0x401F50) -- CRT entry point
  └─ GameLoop_Render (0x45CF80) -- 3715-byte main loop orchestrator
       └─ RenderScene_FullPipeline (0x452510) -- at 0x45DA5D
            ├─ 0x4E0690 (BeginScene?)
            ├─ 0x452140 (setup)
            ├─ SubmitMesh_Generic
            ├─ 0x5D51C0
            └─ RenderFrame_TopLevel (0x450DE0) -- at 0x45252B
                 ├─ 0x40CA60(1) -- pre-frame setup
                 ├─ RenderFrame (0x450B00) -- if bl test passes
                 │    ├─ SetupCameraSector (0x46C4F0) -- if bl != 0 (early path)
                 │    ├─ 0x46D1D0, 0x436AF0 -- camera/matrix setup
                 │    ├─ SectorVisibility_RenderVisibleSectors (0x46C180) -- Layer 1 culling
                 │    │    └─ Sector_RenderMeshes (0x46B7D0) -- per sector
                 │    ├─ 0x5C3C50 -- alternate sector path (if g_sectorBypassFlag)
                 │    ├─ RenderScene (0x443C20) -- scene traversal
                 │    │    └─ SceneTraversal_CullAndSubmit (0x407150) -- Layer 2 frustum cull
                 │    ├─ Object linked list loop (0x450BC7) -- Layer 3
                 │    │    iterates g_pObjectListHead (0x10C5AA4)
                 │    │    checks [obj+0xA4]&0x800, type dispatch via Object_HasComponentType
                 │    │    type 0x1F → 0x54EE70, type 0x24 → 0x534C10, type 0x2A → unused
                 │    ├─ 0x46B340 (ecx=0x10F9078) -- late-pass sector render
                 │    ├─ 0x419550 -- moveable object render dispatch
                 │    ├─ 0x463370/0x463400 -- post-render cleanup
                 │    └─ Cutscene path: 0x44B7A0 if g_pCutsceneObject set
                 └─ FadeController_DrawScene (0x40CBE0) -- fade overlay
                      └─ Renderer_InitAndDraw (0x415A40)
                           ├─ Renderer_DrawPass (0x415260) -- 2003 bytes, full D3D pass
                           └─ 0x4150F0 (Renderer_SetupBaseStates)
```

**6 DIP Return Address Origins (traced 2026-04-05):**
- 0x415613 → inside Renderer_DrawPass (0x415260). This is the D3D9 draw dispatch within the renderer's own multi-pass loop. Called from FadeController path, NOT from RenderFrame's sector/scene paths. This is a **separate draw dispatch** for the fade/overlay system.
- 0x452530 → inside RenderScene_FullPipeline (0x452510), right after call to RenderFrame_TopLevel. The DIP at this return addr comes from the SubmitMesh_Generic call earlier in the pipeline.
- 0x40CD1A → inside FadeController_DrawScene (0x40CBE0). 32 callers. This is the crossroads that bridges scene rendering to the actual D3D draw pass. Every render path goes through here.
- 0x5DD088 → inside a memory allocator/constructor (0x5DD010, writes vtable 0xF076F4). NOT in the render pipeline — DIP from a different subsystem (probably vertex buffer allocation callback).
- 0x40235A → inside WinMain (0x401F50, CRT-called). The DIP here is from the game loop's own draw dispatch at the top level.
- 0x45DA62 → inside GameLoop_Render (0x45CF80), right after call to RenderScene_FullPipeline (0x452510). This is the game loop's render orchestration.

**Key Insight: There are exactly 4 render dispatch paths from RenderFrame, plus 1 direct renderer path:**

Path A: Sector Visibility (0x46C180) → per-sector mesh rendering
Path B: Scene Traversal (0x443C20 �� 0x407150) → frustum-culled scene nodes
Path C: Object Linked List (0x450BC7) → type-dispatched moveable objects
Path D: Late Sector Pass (0x46B340) → conditional second sector render (ecx=0x10F9078)
Path E: Renderer_DrawPass (0x415260) → direct D3D draw calls for overlays/effects (NOT via RenderFrame)

**Critical finding:** Path D (0x46B340 called at 0x450DAB) is a SECOND sector render pass that happens AFTER the main scene. It's gated on `[arg+0xE3] != bl` (a render mode check). This could be a distance-dependent render pass for far-field geometry.

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
