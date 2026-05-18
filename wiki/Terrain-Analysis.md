# Terrain Rendering Analysis — Tomb Raider: Legend (cdcEngine)

> **Binary:** trl.exe (32-bit x86, MSVC)  
> **Engine:** cdcEngine (Crystal Dynamics, 2006)  
> **Context:** RTX Remix compatibility — terrain geometry anchors Remix lights that vanish at distance  
> **Date:** 2026-04-08  
> **Sources:** Static analysis via retools (findings.md, kb.h), TheIndra55/cdcEngine decompilation, cdcengine.re docs

---

## 1. Executive Summary

TerrainDrawable at `0x40ACF0` — originally the "prime suspect" for the light-anchor disappearance — is a **constructor** that builds a 0x30-byte draw descriptor. It contains **zero culling logic**. The actual terrain draw function is `TerrainDrawable_Dispatch` at `0x40AE20`, which has two gates: a flag check (patched) and a NULL pointer guard (must not be patched).

The terrain rendering path is **not an independent pipeline** as initially hypothesized. It shares the same three-layer sector rendering architecture as regular meshes. The remaining blocker is **Layer 3: the recursive bounding-volume frustum culler at `0x40C430`**, which operates downstream of all patched gates and silently drops objects outside the camera view frustum — including the geometry that anchors Remix lights.

---

## 2. cdcEngine Terrain Architecture (from decompilation)

### 2.1 Struct Hierarchy

From TheIndra55/cdcEngine (`cdc/runtime/cdcRender/pc/shared/PCTerrain.h`):

```
Level
  └─ Terrain* terrain
       ├─ TerrainGroup[] (terrain->numTerrainGroups)
       │    ├─ cdc::Matrix          (local-to-world transform)
       │    ├─ OctreeSphere*        (spatial acceleration tree)
       │    ├─ XboxPcMaterialList*  (material strip batching)
       │    ├─ globalOffset / localOffset
       │    └─ cdcRenderDataID      (render data handle)
       ├─ StreamUnitPortal[]        (portal connections between stream units)
       ├─ BGInstance[] / BGObject[]  (background geometry)
       └─ XboxPcVertexBuffer*       (shared VB for all groups)
```

**Key types:**

| Struct | Purpose |
|--------|---------|
| `Terrain` | Per-level terrain container. Owned by `Level`, accessed via `StreamUnit→level→terrain` |
| `TerrainGroup` | One spatial chunk. Has its own transform matrix and octree for strip-level spatial queries |
| `OctreeSphere` | Bounding sphere + 8 children. Spatial acceleration for terrain strip visibility |
| `XboxPcMaterialList` | Material-batched strip organization: tpageid, flags, vbBaseOffset, strip pointers |
| `TerrainRenderVertex` | Position (SHORT4), color, UVs, bend data |
| `TerrainVMORenderVertex` | Extends above with vertex morph blend fields |

### 2.2 The Terrain Draw Loop

From decompiled `terrain.cpp`:

```c
void TERRAIN_DrawUnits() {
    for (int i = 0; i < MAX_STREAM_UNITS; i++) {   // MAX_STREAM_UNITS = 8
        StreamUnit* unit = &StreamTracker.StreamList[i];
        if (unit->used == 2) {                       // 2 = fully loaded
            TERRAIN_CommonRenderLevel(unit);
        }
    }
}

void TERRAIN_CommonRenderLevel(StreamUnit* currentUnit) {
    Terrain* terrain = currentUnit->level->terrain;
    for (int i = 0; i < terrain->numTerrainGroups; i++) {
        DRAW_DrawTerrainGroup(terrain, &terrain->terrainGroups[i]);
    }
}
```

`TERRAIN_DrawUnits` iterates all 8 stream unit slots, submits all terrain groups for loaded units. **No culling at this level** — the only filter is `unit->used == 2` (fully loaded).

### 2.3 StreamUnit Fields

| Field | Type | Purpose |
|-------|------|---------|
| `used` | char | 0=empty, 1=loading, 2=ready |
| `unitHidden` | char | Visibility state |
| `draw` | int | Draw enable flag |
| `level` | Level* | Points to level data containing terrain |
| `pCellGroup` | ISceneCellGroup* | Scene cell group for spatial culling |

---

## 3. TRL Binary Analysis — The Three Functions at 0x40ACF0

What was labeled "TerrainDrawable" in the culling map is actually three consecutive functions sharing an address range:

### 3.1 TerrainDrawable_Ctor (0x40ACF0) — Setup Only

```
Address: 0x40ACF0 – 0x40ADED (ret 0x10)
Convention: __thiscall (ECX = this = output descriptor)
Args: pTerrainData, pMeshBlock, pFlags, pContext (4 stack args)
```

**What it does:**
1. Allocates/initializes a 0x30-byte terrain draw descriptor at `this` (ESI)
2. Sets vtable pointers: `[this+0x00] = 0xEFDE08`, `[this+0x04] = 0xF12864`
3. Copies mesh block flags from `[pMeshBlock]` → `[this+0x1C]` (includes the 0x20000 bit)
4. Tests `[pContext+0x20] & 0x1100000` for flag manipulation
5. Calls shader selection (`0x414280`) and VB lookup (`0xECB0B0`)
6. Returns the descriptor in EAX

**Callees:**
| Address | Purpose |
|---------|---------|
| 0x40A8E0 | Vertex count computation |
| 0x413D70 | Mesh data lookup |
| 0x414280 | Shader/material selection |
| 0xEC9DC0 | Vertex buffer allocation |
| 0xECB0B0 | Vertex shader constant lookup |

**Single caller:** `0x40C0E9` in `RenderQueue_DispatchMeshGroups` (0x40C040), which iterates terrain mesh array from `[sceneData+0x90]`.

**Culling jumps: NONE.** All branches are flag manipulation, not draw-skip decisions.

### 3.2 TerrainDrawable_Submit (0x40ADF0) — Dispatch to Render Queue

```
Address: 0x40ADF0 – 0x40AE16 (ret 4)
Convention: __thiscall
```

Small dispatch function that calls vtable[0] on the allocated batch at `[this+0x28]`, increments global draw counter at `0x10024CC`.

### 3.3 TerrainDrawable_Dispatch (0x40AE20) — The Real Draw Function

```
Address: 0x40AE20 – 0x40B1AC (ret 8)
Convention: __thiscall (ECX = this, mode in [ebp+8])
```

This is the vtable[0] entry for terrain objects. It has **two conditional gates**:

#### Gate 1: Flag 0x20000 Check (0x40AE3E) — PATCHED

```asm
0x40AE29: cmp  [ebp+8], 0x1000        ; check draw mode
0x40AE35: jne  0x40AE44               ; if mode != 0x1000, skip flag check
0x40AE37: test [esi+0x1C], 0x20000    ; check terrain flag bit 17
0x40AE3E: jne  0x40B1A6               ; SKIP ENTIRE DRAW
```

- **Bytes:** `0F 85 62 03 00 00` (JNE, 6 bytes)
- **Condition:** When mode == 0x1000 (terrain batch) AND bit 17 set in descriptor flags
- **Effect:** Skips to function epilogue — no DIP issued
- **Status:** NOP-patched by proxy at `TRL_TERRAIN_FLAG_GATE_ADDR`

This corresponds to the cdcEngine `TerrainGroup` LOD/type flag system. In the decompiled source, terrain groups can be tagged with flags that control which render passes they participate in. The 0x20000 flag likely marks groups designated for a specific LOD tier or render mode that the engine skips during the main pass.

#### Gate 2: NULL Renderer Check (0x40B0F4) — NOT PATCHED (intentional)

```asm
0x40B0F2: test eax, eax               ; eax = [g_pEngineRoot+0x20]
0x40B0F4: je   0x40B1A6               ; SKIP if draw submitter is NULL
```

- **Bytes:** `0F 84 AC 00 00 00` (JE, 6 bytes)
- **Condition:** Global renderer object's draw submitter pointer is NULL
- **Effect:** Skips draw — NOPing would cause NULL dereference crash
- **Status:** Must remain unpatched

#### After Both Gates Pass

The function performs:
1. World matrix setup via FPU 4×4 multiply loop (0x40AFB0–0x40B06C)
2. Vertex buffer binding (call 0x40AA60)
3. Index buffer setup (call 0x40A950)
4. DrawIndexedPrimitive via `[eax+0x148]` or `0xEC91B0`

No additional culling — once past the two gates, terrain geometry is drawn unconditionally.

---

## 4. The Three-Layer Culling Pipeline

The terrain path shares the same architecture as regular mesh rendering. All geometry passes through three culling layers before reaching DrawIndexedPrimitive:

```
                    ┌─────────────────────────────────────┐
                    │  Layer 1: Portal/Sector Visibility   │
                    │  SetupCameraSector (0x46C4F0)        │
                    │  SectorPortalVisibility (0x46D1D0)   │
                    │  RenderVisibleSectors (0x46C180)     │
                    │  STATUS: PATCHED                     │
                    └──────────────┬──────────────────────┘
                                   ▼
                    ┌─────────────────────────────────────┐
                    │  Layer 2: Mesh Submission Gates       │
                    │  Sector_RenderMeshes (0x46B7D0)      │
                    │  Sector_SubmitObject (0x40C650)       │
                    │  MeshSubmit_VisibilityGate (0x454AB0) │
                    │  TerrainDrawable_Dispatch (0x40AE20)  │
                    │  STATUS: MOSTLY PATCHED               │
                    └──────────────┬──────────────────────┘
                                   ▼
                    ┌─────────────────────────────────────┐
                    │  Layer 3: Render Queue Frustum Cull   │
                    │  RenderQueue_FrustumCull (0x40C430)   │
                    │  Tests bounding sphere vs view frustum│
                    │  STATUS: *** NOT PATCHED ***          │
                    └──────────────┬──────────────────────┘
                                   ▼
                              DrawIndexedPrimitive
```

### Layer 1: Portal/Sector Visibility — PATCHED

The portal walk at `SetupCameraSector` (0x46C4F0) discovers which of 8 sector slots are reachable from the camera's sector. `SectorPortalVisibility` (0x46D1D0) resets all sector bounding rects to negative (impossible) values — only portal-reachable sectors get valid bounds.

**Patches applied:**
| Address | Patch | Effect |
|---------|-------|--------|
| 0x46C194 | NOP | Sector visibility bit gate |
| 0x46C19D | NOP | Sector enabled flag gate |
| 0x46C242 | NOP | Screen-size width rejection |
| 0x46C25B | NOP | Screen-size height rejection |
| 0x46D1F1–0x46D205 | 23× NOP | Sector bounds reset loop |

### Layer 2: Mesh Submission Gates — MOSTLY PATCHED

14 conditional gates across 5 functions that can suppress individual mesh draw calls:

| # | Address | Size | Function | Gate | Patched? |
|---|---------|------|----------|------|----------|
| 1 | 0x40AE3E | 6 | TerrainDrawable_Dispatch | Flag 0x20000 terrain batch skip | **YES** |
| 2 | 0x40B0F4 | 6 | TerrainDrawable_Dispatch | NULL draw submitter | NO (crash guard) |
| 3 | 0x454AB0 | 3 | MeshSubmit_VisibilityGate | PVS bitfield sector check | **YES** (xor eax,eax; ret) |
| 4 | 0x45864F | 6 | MeshSubmit | VisibilityGate consumer | Covered by #3 |
| 5 | 0x46B7F2 | 6 | Sector_RenderMeshes | Sector already-rendered skip | **YES** |
| 6 | 0x46B83C | 2 | Sector_RenderMeshes | Per-object hidden flag (bit 0) | NO (crash risk) |
| 7 | 0x46B844 | 2 | Sector_RenderMeshes | Per-object cull flag (0x20000) | NO (crash risk) |
| 8 | 0x46B85A | 2 | Sector_RenderMeshes | Camera-sector proximity filter | **YES** |
| 9 | 0x46C33E | 2 | Sector_IterateMeshArray | Mesh cull flags 0x82000000 | NO (0x02000000 marks invalid) |
| 10 | 0x40C666 | 6 | Sector_SubmitObject | `[g_pEngineRoot+0x10]` renderer state | **YES** |
| 11 | 0x40C68B | 6 | Sector_SubmitObject | `[0x10024E8]` submission lock | **YES** |
| 12 | 0x40E30F | 6 | PostSector_ObjectLoop | Per-sector visibility bitmask | **YES** |
| 13 | 0x40E3B0 | 2 | PostSector_ObjectLoop | Distance/LOD threshold | **YES** |
| 14 | 0x40E2CA | 6 | PostSector_ObjectLoop | Master enable gate | **YES** |

### Layer 3: Render Queue Frustum Culler — NOT PATCHED

`RenderQueue_FrustumCull` (0x40C430) is a **recursive bounding-volume frustum culler** that operates on the render command buffer AFTER mesh submission succeeds at Layer 2.

```c
// Pseudocode of 0x40C430
void RenderQueue_FrustumCull(node, shadowMask, sectorMask) {
    vec3 pos = TransformToViewSpace(node->bounds, viewMatrix_0xF48A70);
    float radius = node->boundingRadius;
    
    if (pos.x < -radius || pos.x > _level + radius) return;  // X clip
    if (pos.y < -radius) return;                              // Y clip
    if (pos.z < -radius) return;                              // Z clip
    // Also tests against secondary matrix at 0xF48AB0
    
    if (node->childCount == 0) {
        // LEAF: queue for actual rendering
        PostSector_AddToVisibilityMask(node->data, sectorMask, ...);
        RenderQueue_InsertCommand(node);                      // → 0x40ACB0
    } else if (fully_inside_frustum) {
        for each child: RenderQueue_DirectDispatch(child);    // → 0x40C390 (no test)
    } else {
        for each child: RenderQueue_FrustumCull(child);       // recurse
    }
}
```

**References:**
| Address | Type | Purpose |
|---------|------|---------|
| 0xF48A70 | 4×4 float | View matrix for frustum tests |
| 0xF48AB0 | 4×4 float | Secondary transform matrix |
| 0x10FC910 | float | `_level` — far-plane boundary (stamped to 1e30f by proxy) |

This function doesn't care about portal visibility or sector state — it performs actual 3D bounding-volume intersection against the camera frustum planes. Objects that pass Layers 1 and 2 but are outside the view frustum are silently dropped here before any DrawIndexedPrimitive call.

**This is the primary remaining suspect for the ~650 draw count ceiling and distant geometry disappearance.**

---

## 5. Cross-Reference: cdcEngine Source vs. TRL Binary

| cdcEngine Source Concept | TRL Binary Address | Notes |
|--------------------------|-------------------|-------|
| `TERRAIN_DrawUnits` | Called within `RenderFrame` (0x450B00) | Iterates 8 stream unit slots |
| `TERRAIN_CommonRenderLevel` | Inside the sector render loop | Per-level terrain group iteration |
| `DRAW_DrawTerrainGroup` | Leads to 0x40ACF0 (TerrainDrawable_Ctor) | Builds descriptor, queues for dispatch |
| `DrawOctreeSphere` | Within TerrainDrawable_Dispatch (0x40AE20) | Octree traversal for strip-level rendering |
| `OctreeSphere` bounding test | Part of Layer 3 (0x40C430) | Recursive frustum cull on bounding spheres |
| `StreamUnit.used == 2` check | StreamUnit state field | Only fully-loaded units render terrain |
| `TerrainGroup.flags` | `[descriptor+0x1C]` — bit 0x20000 | The flag gate at 0x40AE3E |
| Vertex morph (VMO) system | `TerrainVMORenderVertex` format | Geometry blending, not LOD |

### Key Architectural Insight

The cdcEngine decompilation confirms that `TERRAIN_DrawUnits` and `DrawOctreeSphere` contain **no distance or LOD culling** — terrain strips are submitted unconditionally once `DRAW_DrawTerrainGroup` is called. The octree's `boundsphere` exists for spatial partitioning but the traversal visits all 8 children unconditionally.

All distance-based culling happens at higher levels:
- **Sector eviction** (Object Tracker, 94-slot limit) — streaming system frees distant mesh data
- **Portal reachability** (Layer 1) — unreachable sectors get invalid screen bounds
- **Frustum intersection** (Layer 3) — per-object bounding volume test

---

## 6. Additional Systems Affecting Terrain Visibility

### 6.1 Mesh Streaming / Object Tracker

The Object Tracker at `0x11585D8` manages 94 loaded mesh slots (MAX_OBJECTS=0x5E, stride 0x24). When all slots fill, `ObjectTracker_EvictUnneeded` (0x5D44C0) frees distant meshes. `MeshSubmit` calls `ObjectTracker_Resolve` — evicted meshes return NULL, silently skipping the draw.

**Patched:** Both eviction call sites NOP-ed (0x5D31D9, 0x5D5F59, 0x5D5436).

### 6.2 Sector Table Limitations

The sector iteration loop processes exactly **8 hardcoded slots** at `0x11582F8` (stride 0x5C). Each slot has a type field that must be 1 (standard) or 2 (fullscreen) for any mesh iteration. Sectors with type 0 (uninitialized/empty) produce zero draws regardless of patches.

### 6.3 LOD Alpha Fade (0x446580) — UNEXPLORED

`LOD_AlphaBlend` has 10 callers. May cause geometry to fade to invisible at distance. Separate from terrain LOD — affects instance meshes. Not yet investigated.

---

## 7. Terrain Descriptor Struct Layout

Reconstructed from struct field accesses in TerrainDrawable_Ctor and TerrainDrawable_Dispatch:

```c
struct TerrainDrawDescriptor {  // 0x30 bytes
    void*    vtable_draw;       // +0x00  = 0xEFDE08 (TerrainDrawable vtable)
    void*    vtable_submit;     // +0x04  = 0xF12864 (submit interface vtable)
    void*    pTerrainData;      // +0x08
    void*    pMeshBlock;        // +0x0C
    uint32_t vertexCount;       // +0x10  (computed by 0x40A8E0)
    uint32_t indexCount;        // +0x14
    uint32_t materialId;        // +0x18  (from shader selection 0x414280)
    uint32_t flags;             // +0x1C  (copied from pMeshBlock; bit 17 = 0x20000)
    void*    pVertexBuffer;     // +0x20  (from VB lookup 0xECB0B0)
    void*    pIndexBuffer;      // +0x24
    void*    pBatchObject;      // +0x28  (used by Submit for vtable dispatch)
    uint32_t reserved;          // +0x2C
};
```

---

## 8. Patch Prioritization for Remaining Work

### Confirmed Patched (11 terrain-relevant patches active)

| Address | Effect |
|---------|--------|
| 0x40AE3E | Terrain flag 0x20000 gate → NOP |
| 0x454AB0 | VisibilityGate → always return 0 |
| 0x40C666 | Renderer state gate → NOP |
| 0x40C68B | Submission lock gate → NOP |
| 0x46B7F2 | Sector already-rendered → NOP |
| 0x46B85A | Proximity filter → NOP |
| 0x46C242/0x46C25B | Screen-size rejection → NOP |
| 0x46D1F1–0x46D205 | Sector bounds reset → NOP |
| 0x40E30F | Post-sector bitmask → NOP |
| 0x40E3B0 | Post-sector distance → NOP |
| 0x5D31D9/0x5D5F59/0x5D5436 | Eviction calls → NOP |

### Priority Targets (NOT patched)

| Priority | Target | Approach | Risk |
|----------|--------|----------|------|
| **P0** | RenderQueue_FrustumCull (0x40C430) | Redirect entry to 0x40C390 (uncull path) OR force "fully inside" branch | May render off-screen geometry (acceptable for Remix) |
| **P1** | Per-object hidden flag (0x46B83C) | Trampoline with NULL check before NOP | Crash if data pointer NULL |
| **P1** | Per-object cull flag (0x46B844) | Same trampoline approach | Same risk |
| **P2** | LOD_AlphaBlend (0x446580) | Decompile, find alpha threshold, force to 1.0 | May cause visual artifacts |

### Suggested Live Verification Steps

1. `livetools collect 0x40C430 0x40C390 0x40ACB0 0x40D9B0 --duration 5` — measure Layer 3 culling impact
2. `livetools trace 0x40C430 --count 200 --read "eax:4"` — trace recursive culler hit rate
3. `livetools mem read 0x10FC910 4 --as float32` — verify far-plane boundary
4. Test Layer 3 bypass: redirect 0x40C430 → 0x40C390 at runtime via `mem write`

---

## 9. References

| Source | URL / Path |
|--------|------------|
| TheIndra55/cdcEngine decompilation | github.com/TheIndra55/cdcEngine |
| cdcResearch docs | github.com/TheIndra55/cdcResearch |
| cdcengine.re documentation | cdcengine.re/docs/ |
| cdcEngineTools | github.com/Gh0stBlade/cdcEngineTools |
| RTX Remix TRL issue | github.com/NVIDIAGameWorks/rtx-remix/issues/287 |
| Terrain culling analysis | patches/TombRaiderLegend/findings.md (line 1042) |
| Complete gate analysis | patches/TombRaiderLegend/findings.md (line 1707) |
| Streaming system analysis | patches/TombRaiderLegend/findings.md (line 1823) |
| Portal/PVS analysis | patches/TombRaiderLegend/findings.md (line 1987) |
| Draw count bottleneck | patches/TombRaiderLegend/findings.md (line 2352) |
| Knowledge base entries | patches/TombRaiderLegend/kb.h (lines 690–740) |
| Proxy terrain patches | patches/TombRaiderLegend/proxy/d3d9_device.c (line 2535) |
| Proxy terrain patches | proxy/d3d9_device.c (line 2535) |
