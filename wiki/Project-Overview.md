# Project Overview

> Vibe Reverse Engineering toolkit — D3D9 FFP proxy DLL + RE tools for RTX Remix compatibility on Tomb Raider: Legend.

[![Build CI](https://github.com/skurtyyskirts/TombRaiderLegendRTX-/actions/workflows/build.yml/badge.svg)](https://github.com/skurtyyskirts/TombRaiderLegendRTX-/actions/workflows/build.yml)
[![Tests CI](https://github.com/skurtyyskirts/TombRaiderLegendRTX-/actions/workflows/tests.yml/badge.svg)](https://github.com/skurtyyskirts/TombRaiderLegendRTX-/actions/workflows/tests.yml)
[![License](https://img.shields.io/badge/license-See%20LICENSE-blue)](https://github.com/skurtyyskirts/TombRaiderLegendRTX-/blob/main/LICENSE)

## The problem

Tomb Raider: Legend (2006, Crystal Dynamics) was built on cdcEngine — one of the earliest D3D9 engines that abandoned the fixed-function pipeline (FFP) entirely. Every draw call goes through programmable vertex shaders. NVIDIA RTX Remix, the path-tracing replacement renderer, requires FFP draw calls so it can recover World / View / Projection matrices and substitute its own ray-traced lighting.

Standard Remix injection (`d3d9.dll` replacement) sees only shader-bound geometry and skips the entire scene. Without a translation layer, TRL cannot use Remix at all.

## The solution

A D3D9 FFP **proxy DLL** that sits between the game and Remix:

```
NvRemixLauncher32.exe → trl.exe → dxwrapper.dll → d3d9.dll (FFP proxy) → d3d9_remix.dll
```

The proxy intercepts every D3D9 call, reconstructs WVP matrices from the game's VS constant uploads (`SetVertexShaderConstantF`), and re-issues each draw using `SetTransform` + FFP state so Remix can hash the geometry, anchor lights to specific meshes, and path-trace the scene.

In parallel, the proxy applies **32 runtime memory patches** at process load via `VirtualProtect` + memory write, disabling every culling layer found in cdcEngine that would otherwise prevent geometry from reaching the renderer. See [[36-Layer-Culling-Map]].

## Status

- ✅ Proxy DLL builds and chains to Remix cleanly
- ✅ Transform pipeline reconstructs World, View, Projection from VS constants
- ✅ 36 culling layers mapped — 32 confirmed patched
- ✅ FLOAT3 character draws (Lara) correctly process through FFP (build 071b)
- ✅ Replacement asset pipeline confirmed working end-to-end (build 075)
- ✅ Cold-launch crashes resolved (build 077)
- ⚠️ Stage lights still absent — the eight anchor mesh hashes in `mod.usda` are stale and need a fresh Remix capture
- ⚠️ Open: skinned-character hash drift (Lara + NPCs)

Live status: [[Current-Status]].

## Repository layout

| Path | Description |
|------|-------------|
| `proxy/` | D3D9 FFP proxy DLL source |
| `patches/TombRaiderLegend/` | Runtime patches applied by proxy + build scripts |
| `retools/` | Offline static analysis — decompile, xrefs, CFG, RTTI, signatures |
| `livetools/` | Live dynamic analysis — Frida-based tracing, breakpoints, memory r/w |
| `graphics/directx/dx9/tracer/` | Full-frame D3D9 API capture and offline analysis |
| `autopatch/` | Autonomous hypothesis-test-patch loop |
| `automation/` | Screenshot automation and test replay infrastructure |
| `wiki/` | This wiki — full project knowledge base |
| `TRL tests/` | Test build archive — every build with SUMMARY.md, screenshots, proxy log, source |
| `TRL traces/` | Full-frame D3D9 API captures |
| `rtx_remix_tools/` | RTX Remix integration utilities |
| `tools/` | Build scripts, test utilities |

## Quick start

```bash
# One-time setup
pip install -r requirements.txt
python verify_install.py

# Build proxy + run full test
python patches/TombRaiderLegend/run.py test --build --randomize
```

See [[Setup-Guide]] for the full installation, build, and deployment walkthrough.

## How to read this wiki

- **First time?** Start with [[Home]], then [[DLL-Chain-and-Architecture]] and [[FFP-Proxy-Pipeline]].
- **Continuing the project?** Open [[Current-Status]] first, then [[Dead-Ends]] before proposing experiments.
- **Building a different RTX Remix port?** The transferable knowledge is in [[FFP-Proxy-Pipeline]], [[Hash-Stability]], [[Transform-Matrices]], [[Reverse-Engineering-Toolkit]], and the [[Dead-Ends]] catalog of approaches that don't work.
- **Looking up an address?** [[Rosetta-Stone]] and [[Engine-Memory-Map]] are the master cross-references.

## Engineering standards

Adopted across all sessions on this project:

1. Every session: read [[Current-Status]] and the most recent build summary before doing anything else.
2. Log all findings to the Changelog with timestamps.
3. Failed approaches go in [[Dead-Ends]] with **why** and **which build**.
4. **Never retry a documented dead end without new evidence.**
5. Every build gets a folder in `TRL tests/` with SUMMARY.md, screenshots, proxy log, source snapshot.
6. PASS builds include `miracle` in the folder name.
7. Every build — pass or fail — is pushed immediately.
8. Test at Croft Manor: both red+green stage lights visible in all 3 screenshots, lights shift on strafe.

See [[Contributing]] for full guidelines and the contribution workflow.
