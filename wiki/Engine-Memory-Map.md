# Engine Memory Map

> Every cdcEngine global, sector field offset, and structural address discovered in the course of the TRL RTX Remix port.

This is the authoritative one-stop reference for "what lives at what address" inside `trl.exe`. Companion to [[36-Layer-Culling-Map]] (which tabulates code addresses) and [[Rosetta-Stone]] (which adds the *why* and source-build for each row).

## Engine globals (data)

| Address | Name | Type | Notes |
|---------|------|------|-------|
| `0x01392E18` | `g_pEngineRoot` | `EngineRoot*` | Root engine object — proxy reads this via dereference chain to find the live IDirect3DDevice9 |
| `0x010FC780` | View matrix source | `D3DMATRIX` | Read by proxy each frame; cross-validates VS-constant recovery |
| `0x01002530` | Projection matrix source | `D3DMATRIX` | Read by proxy each frame |
| `0xEFDD64` | Frustum threshold (was `16.0f`) | `float` | Stamped to `-1e30f` per BeginScene to disable distance culling |
| `0xF2A0D4 / D8 / DC` | Cull mode globals | `DWORD ×3` | Stamped to `D3DCULL_NONE` (1) per BeginScene |
| `0x10FC910` | Far clip distance | `float` | Stamped to `1e30f` per BeginScene |
| `0xEFD404` | Screen boundary min | `float` | Used by boundary cull checks |
| `0xEFD40C` | Screen boundary max | `float` | Used by boundary cull checks |
| `0x01075BE0` | "Disable extra static light culling" | `DWORD` | Config flag found via table at `0xF1325C` → string at `0xEFF384`. **No code xrefs** — stamping has no effect (see [[Dead-Ends]] #5) |
| `0xF12016` | Post-sector loop enable flag | `BYTE` | Stamped to 1 per scene |
| `0x10024E8` | Post-sector gate | `DWORD` | Stamped — enables post-sector submission loop |

## Renderer chain

The path from the engine root to the active D3D9 device:

```
g_pEngineRoot (0x01392E18)
   └─ +0x214 → TRLRenderer*
                  └─ +0x0C → IDirect3DDevice9*
```

Build 021 mapped this chain via Ghidra; the proxy reuses it in any code path that needs the live device without going through the COM-wrapped one it intercepts.

## Sector data layout

cdcEngine stores levels as arrays of fixed-size sector structs.

| Field | Notes |
|-------|-------|
| `*(renderCtx + 0x220)` | Sector data base pointer |
| `sector_data + N*0x684 + 0x664` | Native static light count for sector N — stride is `0x684` bytes |
| `sector + 0x1B0` | Per-sector light list count |
| `sector + 0x1B8` | Per-sector light list array pointer |
| `sector + 0x84` | First gate field for light pass in `RenderScene_Main` (`0x603810`) |
| `sector + 0x94` | Second gate field — `RenderScene_Main` requires `[+0x84] + [+0x94] != 0` to run light pass |

Of the discovered TRL sectors, only `mesh_AB241947CA588F11` (green stage light anchor) sits in a sector with non-zero `[sector_data + 0x664]`. All other sectors have zero static-light data — which is why builds 035–037 partial-passed only the green light.

## Key functions (code addresses)

The big ones referenced across the project:

| Address | Name | Role |
|---------|------|------|
| `0x407150` | `SceneTraversal_CullAndSubmit` | Per-object frustum + visibility, calls Sector_SubmitObject |
| `0x40C430` | `RenderQueue_FrustumCull` | Recursive BVH frustum culler |
| `0x40C390` | `RenderQueue_NoCull` | The "no-cull" path inside the BVH; proxy JMPs to this |
| `0x603810` | `RenderScene_Main` | Iterates sectors, runs light pass |
| `0x60A0F0` | `RenderScene_TopLevel` | Calls `FUN_006033d0` + `FUN_00602aa0` (suspected sector light list builders) |
| `0x60E2D0` | `RenderScene_LightPass` | Owns the light gate at `0x60E3B1` |
| `0x60C7D0` | `RenderLights_FrustumCull` | Two draw paths inside — immediate (mode=1) and deferred (mode=0) |
| `0x60CE20` | Light frustum 6-plane test | JNP that rejects out-of-frustum lights |
| `0x60CDE2` | Light broad-visibility test | JZ that skips broadphase-rejected lights |
| `0x60B050` | `Light_VisibilityTest` | Per-light visibility — proxy forces it to always return true |
| `0x6124E0` | `LightVolume_UpdateVisibility` | Owns 5 state-update NOPs |
| `0x46B7D0` | `RenderSector` | Per-sector dispatch — owns proximity filter at `0x46B85A` |
| `0x46C180` | `RenderVisibleSectors` | Sector iteration; owns visibility resets and frustum-screen-size rejection |
| `0x454AB0` | `MeshSubmit_VisibilityGate` | Final per-mesh visibility check before submit |
| `0x40ACF0` | `TerrainDrawable` (constructor) | Zero culling logic — real terrain dispatch at `0x40AE20` |
| `0x40AE3E` | Terrain flag gate | NOPed for terrain culling bypass |
| `0x446580` | LOD alpha fade | 10 callers — **unexplored** |
| `0x450B00` | `RenderFrame` | Top of the render frame |
| `0xEC62A0` | Sector light list populator | Reads from `[sector_data + 0x664]` |
| `0xEDF9E3` | Null-check guard | Trampoline-patched to skip stale-field dereference |
| `0xEE88AD` | (Crash site, now resolved) | `ProcessPendingRemovals` stale-field deref — fixed via patch at the calling function |
| `0x40D2AC` | Scene-traversal null deref site | Trampoline-patched (build 076) — guards `NULL+0x20` deref at `0x40D2AF` |

The full inventory with rationale and source builds: [[Rosetta-Stone]].

## VS constant register layout (TRL-specific)

```
c0–c3:   World matrix (transposed)
c8–c11:  View matrix
c12–c15: Projection matrix
c48+:    Skinning bone matrices (3 regs/bone)
```

View and Projection are **separate** registers — not a fused ViewProj. The proxy un-transposes World and forwards W / V / P to `SetTransform(D3DTS_WORLD / VIEW / PROJECTION)`. See [[VS-Constant-Register-Layout]] for the full c0–c96 map and the 34 `SetVertexShaderConstantF` call sites.

## Light anchor mesh hashes

These hashes have lights anchored in `mod.usda`:

| Hash | Color | Vertices |
|------|-------|----------|
| `mesh_2509CEDB7BB2FAFE` | Red | 365 |
| `mesh_47AC93EAC3777CA5` | Red | 332 |
| `mesh_DD7F8EE7F4F3969E` | Green | 315 |
| `mesh_CE011E8D334D2E48` | Green | 312 |
| `mesh_2AF374CD4EA62668` | Red | 298 |
| `mesh_5601C7C67406C663` | Red | (build 071 addition) |
| `mesh_ECD53B85CBA3D2A5` | Red | (build 071 addition) |
| `mesh_AB241947CA588F11` | Green | (build 071 addition) — the only one in a sector with non-zero static light data |
| `mesh_574EDF0EAD7FC51D` | Purple (test) | Proven visible in build 075 |

**These hashes are confirmed stale** as of build 075. Geometry IS rendering (~3,749 draw calls per scene at Croft Manor) but the rendered meshes' current hashes do not match the stored values. A fresh Remix capture is the next action. See [[Build-074-077-Asset-Pipeline]].

## Sky and UI texture hashes

From `rtx.conf`:

```
rtx.skyBoxTextures = 0x443B45FB9971FC90, 0x78AD1D0EDA0FFC21, 0x8405ADDE0AE29A5F
rtx.uiTextures     = 0x03016D2FBBF5C65D, 0x2164293A60D148AC
animatedWaterTextures = 0x95011A686BA05DFF
```

## Verification commands

```bash
# Verify a code patch is in place (on-disk byte check)
python -m retools.readmem trl.exe 0x407150 bytes -n 10

# Verify a runtime memory address
python -m livetools mem read 0xEFDD64 4 --as float32

# Disassemble a patched function
python -m retools.disasm trl.exe 0x40C430 -n 30

# Find every caller of a function
python -m retools.xrefs trl.exe 0x60B050 -t call

# Verify VS constant register layout
python -m livetools trace 0x... --read "[esp+c]:64:float32 [esp+10]:1:uint32"
```

## See also

- [[36-Layer-Culling-Map]] — every patched location grouped by culling layer
- [[Rosetta-Stone]] — every address × register × config value with rationale
- [[VS-Constant-Register-Layout]] — the c0–c96 register map
- [[Transform-Matrices]] — how W, V, P are recovered from VS constants
- [[FFP-Proxy-Pipeline]] — section 4 explains the recovery in code
