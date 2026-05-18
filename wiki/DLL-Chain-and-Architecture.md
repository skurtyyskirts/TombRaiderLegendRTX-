# DLL Chain and Architecture

> How four DLLs cooperate to make a 2006 shader-only D3D9 game render through NVIDIA RTX Remix.

## The chain

```
┌─────────────────────────┐
│ NvRemixLauncher32.exe   │  RTX Remix loader (NVIDIA)
└────────────┬────────────┘
             │  spawns
             ▼
┌─────────────────────────┐
│ trl.exe                 │  Game executable (32-bit x86, cdcEngine)
└────────────┬────────────┘
             │  imports d3d9.dll
             ▼
┌─────────────────────────┐
│ dxwrapper.dll           │  Compatibility shim (CnCNet's dxwrapper)
└────────────┬────────────┘     - fixes 2006-era D3D9 quirks
             │  forwards         - thunks D3DPOOL_DEFAULT issues
             ▼
┌─────────────────────────┐
│ d3d9.dll (FFP proxy)    │  This project — translates VS draws → FFP
└────────────┬────────────┘     - reconstructs W/V/P from VS constants
             │  chains          - applies 32 runtime culling patches
             ▼                  - expands SHORT4→FLOAT3 vertex buffers
┌─────────────────────────┐     - null-VS path for FFP rendering
│ d3d9_remix.dll          │  RTX Remix renderer (NVIDIA)
└────────────┬────────────┘     - hashes geometry, anchors lights
             │  vulkan          - path-traces, denoises, reconstructs
             ▼
       NVIDIA GPU
```

## Why each link is required

### `dxwrapper.dll`
TRL was built before D3DPOOL_DEFAULT semantics stabilized and before the modern reset/lost-device contract. It dies on alt-tab, dies on Win10 fullscreen, and has fragile vsync handling. `dxwrapper` papers over the worst of this without changing renderer behavior.

### `d3d9.dll` (this project's proxy)
TRL renders every draw through a **vertex shader**. RTX Remix only recognizes geometry submitted via:
1. The Fixed-Function Pipeline (`SetTransform(D3DTS_WORLD/VIEW/PROJECTION)` + `DrawPrimitive`)
2. **OR** "vertex capture" mode where Remix snapshots the post-VS vertex stream

Either way, Remix needs to know **where** the geometry is in world space. With a programmable VS, Remix has no access to the matrices — they live in the game's VS constants (`c0..c96`), not in any D3D9 state.

The proxy's job is **transform recovery**: every `SetVertexShaderConstantF` is captured into a per-draw register bank, and on every `DrawIndexedPrimitive` the proxy reconstructs World/View/Projection from those registers and issues the equivalent `SetTransform` calls before chaining to Remix's `DrawIndexedPrimitive`.

In addition, the proxy applies all the runtime patches that disable cdcEngine's per-object, per-sector, per-frustum, and per-LOD culling (see [[36-Layer-Culling-Map]]) so that Remix sees the entire scene, not just the small fraction that survives cdcEngine's seven-layer culling.

### `d3d9_remix.dll`
The actual path-tracing renderer. Reads `rtx.conf` (see [[rtx-conf-Reference]]) and `mod.usda` from the game's `rtx-remix/` folder.

## Proxy method hooks

| D3D9 method | What the proxy does |
|------------|---------------------|
| `SetVertexShaderConstantF` | Captures VS constants into per-draw register bank (`g_vs_regs[256]`) |
| `DrawIndexedPrimitive` | Reconstructs W/V/P matrices, calls `SetTransform`, may swap vertex declaration, may null the VS, chains to Remix |
| `DrawPrimitive` | Same as above for non-indexed draws |
| `SetRenderState` | Intercepts `D3DRS_CULLMODE` — forces `D3DCULL_NONE` so Remix sees both sides of every triangle |
| `BeginScene` | Stamps anti-culling globals (frustum threshold, cull mode, far clip) each frame |
| `EndScene` | Diagnostic logging, optional VB cache flush |
| `Present` | Logs diagnostics every 120 frames (DIAG-only build), bumps frame counter |
| `CreateVertexBuffer` | Tracks VB stride and FVF for SHORT4 expansion |
| `Lock/Unlock` on VB | Captures vertex data when needed for content fingerprint cache |
| `CreateDevice` | Strips `PUREDEVICE` flag, FourCC format rejection, 119-vtable wrap |

The full 119-method vtable is wrapped by an auto-generated `d3d9_wrapper.c` (codegen lives in `graphics/directx/dx9/`).

## Transform recovery

See [[Transform-Matrices]] for the math. In summary:

1. TRL uploads its **transposed** world matrix to `c0..c3`.
2. View matrix to `c8..c11`.
3. Projection matrix to `c12..c15`.
4. Skinning bone matrices (when present) to `c48+`, three regs per bone (3×4 affine).

These positions were discovered by:
- Running `find_vs_constants.py` for a list of `SetVertexShaderConstantF` call sites.
- Decompiling the four most-frequent callers in Ghidra.
- Cross-referencing with live `livetools trace --read` of the registers during a known camera move.
- Verifying matrices were transposed by comparing the recovered upper-left 3×3 to the runtime view matrix at `0x010FC780`.

The proxy un-transposes World and feeds untransposed W, V, P to `SetTransform`. `rtx.conf` is configured with `fusedWorldViewMode = 0` (separate W and V) and `useWorldMatricesForShaders = True` so Remix uses the per-call World matrix.

## Hash architecture

Remix computes **two** geometry hashes per draw:

| Hash | Inputs | Purpose |
|------|--------|---------|
| Asset hash | `positions, indices, texcoords, geometrydescriptor` | Identifies a mesh — used to anchor lights and replacements |
| Generation hash | `positions, indices, texcoords, geometrydescriptor, vertexlayout, vertexshader` | Identifies a specific draw — drives capture and dedup |

Both rules **require `positions`** — build 047 proved that dropping positions produces catastrophic collisions ([[Dead-Ends]] #10). See [[Hash-Stability]] for the deep theory and [[SHORT4-Vertex-Decoding]] for the specific SHORT4 problem that makes TRL hashes drift unless the proxy expands them to FLOAT3 first.

## Runtime memory patching

The proxy uses `VirtualProtect(PAGE_READWRITE)` to unlock TRL's `.text` and selected `.data` pages, writes patch bytes (mostly `0x90` NOPs and a few code-cave trampolines), and restores `PAGE_EXECUTE_READ` for code pages. After build 074, all patches are **deferred** to the first `BeginScene` where `viewProjValid=1` — this fixes a menu crash when patches landed before the renderer was initialized.

`*data* pages remain PAGE_READWRITE permanently after build 074, so per-frame stamping is a plain MOV.

The full patch list, addresses, and rationale: [[36-Layer-Culling-Map]] and [[Engine-Memory-Map]].

## Why this is unusual

Most D3D9 → RTX Remix ports work because the game already uses FFP draw calls. TRL is exceptional because:

1. **No FFP draws at all.** Every single one of TRL's ~3,000 draws per scene is shader-bound.
2. **Aggressive culling.** cdcEngine's seven distinct culling stages combine to drop ~75% of submitted draws during a normal session.
3. **Mixed vertex formats.** Static geometry uses SHORT4 normalized positions packed in a tight AABB; characters use FLOAT3 pre-transformed to view space; skinned characters add BLENDWEIGHT/BLENDINDICES streams.
4. **Pre-view-space FLOAT3.** Character meshes are CPU-transformed into view space before being uploaded. The VS only applies projection. Nulling the VS for these meshes produces meshes at extreme scale ([[Dead-Ends]] #9).

The full technical narrative of how each problem was identified and solved lives in [[FFP-Proxy-Pipeline]] (16 sections) and the [[Build-History-Index]].

## See also

- [[FFP-Proxy-Pipeline]] — the 16-section deep dive
- [[Transform-Matrices]] — WVP decomposition math
- [[VS-Constant-Register-Layout]] — exact register map
- [[36-Layer-Culling-Map]] — every culling layer
- [[Engine-Memory-Map]] — globals and sector layout
- [[Rosetta-Stone]] — master cross-reference
