# Tomb Raider Legend — RTX Remix

Port Tomb Raider Legend (2006) to NVIDIA RTX Remix for full path-traced lighting, stable geometry hashes, and complete scene visibility.

**Status: In progress** — 44 builds completed. All major culling layers patched. `TerrainDrawable (0x40ACF0)` is the remaining prime suspect.

---

## Project Status

| Milestone | Status |
|-----------|--------|
| FFP proxy DLL — builds and chains to Remix | Done |
| Transform pipeline (View / Proj / World) | Done |
| Asset hash stability (static + moving camera) | Done |
| Automated test pipeline | Done |
| Backface / frustum / distance culling disabled | Done |
| Sector / portal visibility disabled | Done |
| Per-light culling gates disabled | Done |
| **Both stage lights stable at all positions** | **Failing** |

**Last confirmed PASS:** `build-019` (2026-03-25) — both lights visible, hashes stable.
**Latest:** `build-044` — all three render paths patched; anchor geometry still disappears at distance.

---

## The Problem

TRL renders exclusively via programmable vertex shaders. RTX Remix requires the D3D9 Fixed-Function Pipeline (FFP) to identify geometry, assign stable asset hashes, and inject path-traced lighting — shader-based draws produce unstable hashes and incorrect material assignments because Remix cannot decode shader constant semantics.

Remix also anchors scene lights to geometry draw calls. When TRL's frustum and sector culling hides geometry from the renderer, Remix loses the anchor points and the lights disappear.

## The Solution

A custom `d3d9.dll` proxy sits between TRL and RTX Remix. It intercepts D3D9 calls, reverse-engineers TRL's vertex shader constant layout, reconstructs world/view/projection matrices, and feeds them to Remix through FFP — so Remix sees TRL as a native FFP game. The proxy also patches TRL's culling systems at runtime so Remix can hash and light all geometry regardless of camera position.

See [docs/TECHNICAL_BUILD_DOCUMENT.md](docs/reference/TECHNICAL_BUILD_DOCUMENT.md) for the full technical specification.

---

## Navigation

| Document | Description |
|----------|-------------|
| [docs/status/WHITEBOARD.md](docs/status/WHITEBOARD.md) | Live project status: 22-layer culling map, build history, decision tree, key addresses |
| [docs/status/TEST_STATUS.md](docs/status/TEST_STATUS.md) | Build-by-build pass/fail results and what remains |
| [TRL tests/](TRL%20tests/) | Test build archive — every build committed with `SUMMARY.md` + screenshots + proxy source |
| [docs/](docs/) | Technical documentation: research, reference, guides |
| [proxy/](proxy/) | Current proxy DLL source (`d3d9_device.c`, `build.bat`, `proxy.ini`) |
| [retools/](retools/) | Static analysis toolkit — offline PE analysis (decompile, xrefs, search, RTTI) |
| [livetools/](livetools/) | Live dynamic analysis — Frida-based (trace, breakpoints, memory r/w) |
| [graphics/directx/dx9/tracer/](graphics/directx/dx9/tracer/) | D3D9 frame capture and analysis |
| [autopatch/](autopatch/) | Autonomous hypothesis testing and patching |

---

## How the Proxy Works

The proxy is a no-CRT `d3d9.dll` compiled with MSVC x86, loaded by TRL in place of the system D3D9 DLL.

| Method | What it does |
|--------|-------------|
| `SetVertexShaderConstantF` | Captures VS constants into a per-draw register bank |
| `DrawIndexedPrimitive` | Reconstructs W/V/P matrices from the constant bank, calls `SetTransform`, chains to Remix |
| `SetRenderState` | Intercepts `D3DRS_CULLMODE` — forces `D3DCULL_NONE` |
| `Present` | Logs diagnostics every 120 frames (draw counts, `vpValid`, patch confirmations) |

**VS Constant Register Layout (TRL-specific):**

```c
c0–c3:   World matrix (transposed)
c8–c11:  View matrix
c12–c15: Projection matrix
c48+:    Skinning bone matrices (3 regs/bone)
```

**Anti-Culling Patches — applied at proxy startup via `VirtualProtect` + memory write:**

| Address | Patch | Effect |
|---------|-------|--------|
| `0x407150` | `RET` | Bypasses the per-object frustum cull function entirely |
| `0x4070F0` + 11 sites | NOP | Disables scene-traversal cull branches |
| `0x46C194`, `0x46C19D` | NOP | Defeats sector/portal visibility gates (65× draw count increase) |
| `0x60B050` | `mov al,1; ret 4` | `Light_VisibilityTest` always returns TRUE |

---

## Running Tests

```bash
# Full build + test pipeline
python patches/TombRaiderLegend/run.py test --build --randomize

# Test only (skip build, use last compiled proxy)
python patches/TombRaiderLegend/run.py test --randomize
```

Say **"begin testing"** and the agent runs the full automated pipeline (build → deploy → launch → macro → collect results).

**PASS criteria:** Both red and green stage lights visible in all 3 clean render screenshots, lights shift position as Lara strafes, hashes stable, no crash.

---

## Build Archive

Every test run creates a folder in `TRL tests/`:

```
TRL tests/
├── build-NNN-<description>/
│   ├── SUMMARY.md                  # Result, what changed, proxy log, findings, next plan
│   ├── phase1-hash-debug-posN.png  # Hash debug view (geometry colored by asset hash)
│   ├── phase2-clean-render-posN.png # Path-traced clean render
│   ├── ffp_proxy.log               # Proxy diagnostics
│   └── proxy/                      # Proxy source snapshot
```

PASS builds include "miracle" in the folder name. Every build — pass or fail — is pushed immediately.

---

## Quick Start for a New Session

1. Read [`docs/status/WHITEBOARD.md`](docs/status/WHITEBOARD.md) — current culling layer map, build history, decision tree
2. Read [`docs/status/TEST_STATUS.md`](docs/status/TEST_STATUS.md) — build-by-build results and open items
3. Check the latest build folder in `TRL tests/` and its `SUMMARY.md`
4. Read `patches/TombRaiderLegend/kb.h` — accumulated address map and struct layouts
5. Say **"begin testing"** to run a full test, or **"begin testing manually"** to play the game yourself
