# Research Papers Index

> Index of every long-form research document in the project. Most of these were originally in `docs/research/` and have been moved into the wiki.

## Formal papers and analysis

- **[[TRL-RTX-Remix-Paper]]** — The formal paper. Documents how TRL was brought to a path-tracing-capable state via static RE + live tracing + proxy + config control. Recommended as a first read for anyone joining the project.
- **[[Deep-Analysis-Report]]** — 2026-03-19 comprehensive snapshot covering runtime architecture, draw routing, camera tracking, path tracing engagement. A "where things stood" report from mid-project.
- **[[Experiment-Log]]** — Chronological lab notebook: hypothesis → rationale → outcome table covering baseline chain test through final upstream-projection pivot.
- **[[RE-Roadmap]]** — Forward plan beyond the rigid-path working state: recover true VIEW/WORLD ownership, expand FFP coverage, reduce hacks.

## Engine-specific investigations

- **[[Terrain-Analysis]]** — cdcEngine terrain rendering analysis: `TerrainDrawable` at `0x40ACF0` is a constructor (zero culling logic); real dispatch at `0x40AE20`; the 3-layer sector architecture; Layer 31 (`RenderQueue_FrustumCull` at `0x40C430`) as the remaining blocker.
- **[[Lara-Visibility-Fix-Report]]** — Investigation of Lara invisibility / missing UI bug. Root cause analysis, fixes applied, lessons learned.
- **[[TR7-RTX-Remix-Research]]** — High-level engine compatibility report on cdcEngine: programmable shader pipeline, dynamic D3D9 loading, transforms via shader constants.
- **[[FFP-Discovery-Notes]]** — Original FFP discovery note: capability gate at `0x00ec2d10`, `GetDeviceCaps` wrapper at `0x00ecd480`, hook strategy for forcing legacy FFP.
- **[[FFP-Ghidra-Caps-Gate]]** — Ghidra-driven analysis of the D3D9 caps gate / next-gen check. Companion to FFP-Discovery-Notes.
- **[[RenderDoc-Capture-Analysis]]** — RenderDoc Vulkan-level capture (frame #1084) with FFP proxy + Remix active. Frame statistics, draw classification, vertex-capture coverage.

## Hash and vertex format research

- **[[SHORT4-Vertex-Decoding]]** — Exhaustive D3DDECLTYPE_SHORT4 encoding reference (normalized, AABB, fixed-point), W-component semantics, recovery methodologies, hash-stability implications. Includes the deep-research-report.md content.
- **[[Hash-Stability]]** — How Remix computes geometry hashes, the #1 cause of instability (WVP baked into vertices), software skinning, dynamic VBs, fix strategies.
- **[[Hash-Debugger]]** — Workflow agent doc for diagnosing geometry-hash instability — scope identification, classification rules, debug-view interpretation.

## Technical reference

- **[[FFP-Proxy-Pipeline]]** — 16-section technical deep dive: runtime stack, fundamental mismatch, 119-method vtable, WVP decomposition, draw routing decision tree, texture stage cleanup, hash stability via VP inverse caching, dxwrapper SetTransform conflict, frame lifecycle, deployment.
- **[[Transform-Matrices]]** — Object → World → View → Projection pipeline. The role of `rtx.fusedWorldViewMode` (0/1/2). How Remix recovers world-space positions.
- **[[VS-Constant-Register-Layout]]** — Authoritative VS register map: c0–c3 World transposed, c0–c7 WVP, c8–c15 ViewProjection, c16+ bone matrices. 34 `SetVertexShaderConstantF` call sites cataloged.
- **[[Generic-D3D9-Pipeline-Reference]]** — Generic D3D9 pipeline reference: stages, device types, swap chains, resources, transform/clipping. Background reading for newcomers.

## Other and design

- **[[DXVK-Debug-USD-Analysis-Design]]** — Design doc for two test-pipeline additions: DXVK debug env vars (`DXVK_LOG_LEVEL` / `SHADER_DUMP_PATH`) and lightweight USD capture analysis.
- **[[Stable-Hashes-Technical-Analysis]]** — Extended deep dive on the 30-layer culling architecture, three-phase culling pipeline, why stable hashes matter for Toolkit replacement. Originally from `TRL tests/contenders/build-073-stable-hashes/TECHNICAL_ANALYSIS.md`.
- **[[Proxy-Performance-Audit]]** — Per-draw / per-frame CPU cost map of the proxy + ordered list of optimization candidates. Originally `TRL tests/build-078-perf-build/HOTPATH_AUDIT.md` and `OPTIMIZATION_CANDIDATES.md`.

## See also

- [[Home]] — wiki entry point
- [[Build-History-Index]] — every build with one-line summary
- [[Current-Status]] — live project state
