# FFP Proxy DLL

A no-CRT `d3d9.dll` that sits between Tomb Raider Legend and RTX Remix. It converts TRL's vertex-shader draw calls into Fixed-Function Pipeline (FFP) calls that Remix can hash, assign materials to, and path-trace — then chains to the real Remix `d3d9.dll`.

---

## Why It Exists

TRL renders entirely through vertex shaders. RTX Remix requires the D3D9 Fixed-Function Pipeline to identify geometry — shader-based draws produce unstable hashes and incorrect material assignments because Remix cannot decode shader constant semantics.

This proxy reverse-engineers TRL's VS constant register layout, reconstructs the world/view/projection matrices per draw call, NULLs the vertex shaders, and feeds the matrices through `SetTransform`. From Remix's perspective, TRL is a native FFP game.

---

## Files

| File | Purpose |
|------|---------|
| `d3d9_device.c` | Core proxy — ~2100 lines, intercepts ~15 of 119 `IDirect3DDevice9` methods |
| `d3d9_main.c` | DLL entry point, logging, chain-load to Remix |
| `d3d9_wrapper.c` | `IDirect3D9` wrapper (create + relay) |
| `d3d9_skinning.h` | Optional GPU skinning support (`ENABLE_SKINNING=0` by default) |
| `d3d9.def` | DLL export table |
| `build.bat` | MSVC x86 build script (uses `vswhere` to locate Visual Studio) |
| `proxy.ini` | Runtime config: Remix chain-load path, albedo stage settings |

> **Authoritative source:** `patches/TombRaiderLegend/proxy/` (git-ignored workspace). This `proxy/` directory is kept in sync with it for version tracking.

---

## Intercepted Methods

| Method | What the proxy does |
|--------|---------------------|
| `SetVertexShader` | When shader is NULLed, activates FFP mode for the upcoming draw |
| `SetVertexShaderConstantF` | Captures VS constant registers into a per-draw constant bank |
| `SetRenderState` | Intercepts `D3DRS_CULLMODE` — forces `D3DCULL_NONE` |
| `DrawIndexedPrimitive` | Reconstructs World/View/Proj from constant bank, calls `SetTransform`, NULLs shader, relays draw |
| `BeginScene` | Stamps anti-culling globals (frustum threshold, cull mode, far clip) |
| `Present` | Logs diagnostics every 120 frames (draw counts, vpValid, patch status) |

---

## VS Constant Register Layout (TRL-specific)

```c
#define VS_REG_WVP_START     0   // c0–c3:   WorldViewProjection (combined 4×4)
#define VS_REG_VIEW_START    8   // c8–c11:  View matrix
#define VS_REG_PROJ_START   12   // c12–c15: Projection matrix
#define VS_REG_BONE_START   48   // c48+:    Skinning matrices (3 regs/bone)
```

View and Projection are also read directly from TRL's in-memory matrix globals (`0x010FC780`, `0x01002530`) for cross-validation. World is reconstructed as `WVP × (VP)⁻¹`.

---

## Anti-Culling Patches

Applied at startup via `VirtualProtect` + memory writes:

| Address | Patch | Effect |
|---------|-------|--------|
| `0x407150` | Write `0xC3` (RET) | Bypasses per-object frustum cull function entirely |
| `0x4070F0` + 10 sites | NOP 6-byte branches | Disables all scene-traversal cull exits |
| `0x46C194`, `0x46C19D` | NOP JE/JNE | Defeats sector/portal visibility gates (65× draw count increase) |
| `0x60B050` | `mov al,1; ret 4` | `Light_VisibilityTest` always returns TRUE |
| `0xEFDD64` | Stamp `-1e30f` | Frustum distance threshold — nothing is "too close" |
| `0xF2A0D4/D8/DC` | Stamp `D3DCULL_NONE` | Cull mode globals |
| `0x10FC910` | Stamp `1e30f` | Far clip distance |

---

## Building

Requires MSVC (Visual Studio 2019+). From the repo root:

```bat
cd proxy
build.bat
```

The compiled `d3d9.dll` is placed in `proxy/`. Deploy it alongside `proxy.ini` to the game directory (`Tomb Raider Legend/`).

To run the full build + test pipeline:

```bash
python patches/TombRaiderLegend/run.py test --build --randomize
```
