# Documentation Index

Technical documentation for the TRL RTX Remix project, organized by type.

For the main project overview and current status, see the [root README](../README.md).

---

## Status

Live project tracking — updated at the end of each development phase.

| File | Description |
|------|-------------|
| [`status/WHITEBOARD.md`](status/WHITEBOARD.md) | **Live status**: 36-layer culling map, full build history narrative, decision tree, key addresses |
| [`status/TEST_STATUS.md`](status/TEST_STATUS.md) | Build-by-build pass/fail table, what's done, what remains |

---

## Reference

Quick-lookup technical reference documents.

| File | Description |
|------|-------------|
| [`reference/TECHNICAL_BUILD_DOCUMENT.md`](reference/TECHNICAL_BUILD_DOCUMENT.md) | Complete technical specification: proxy design, VS register layout, game memory patches, build steps |
| [`reference/pipeline-architecture.md`](reference/pipeline-architecture.md) | Render pipeline architecture diagram and description |
| [`reference/transform-matrices.md`](reference/transform-matrices.md) | Matrix register layout, decomposition math |
| [`reference/hash-stability.md`](reference/hash-stability.md) | Asset hash stability rules and findings |
| [`reference/hash-debugger.md`](reference/hash-debugger.md) | Hash debug mode: how to use and interpret |
| [`reference/rtx-conf-reference.md`](reference/rtx-conf-reference.md) | `rtx.conf` settings reference for this project |
| [`reference/d3d9-short4-vertex-decoding.md`](reference/d3d9-short4-vertex-decoding.md) | `D3DDECLTYPE_SHORT4` vertex encoding — hardware-level interception for Remix compatibility |
| [`reference/TRL-RTX-Remix-Index.md`](reference/TRL-RTX-Remix-Index.md) | Master index of all known addresses and symbols |
| [`reference/TRL-RTX-Remix-Definition-Book.md`](reference/TRL-RTX-Remix-Definition-Book.md) | Glossary of engine terms, Remix concepts, and project-specific definitions |
| [`reference/TRL-RTX-Remix-Rosetta-Stone.md`](reference/TRL-RTX-Remix-Rosetta-Stone.md) | Cross-reference: engine internals ↔ Remix concepts |
| [`reference/TRL_MODDING_TOOLS.md`](reference/TRL_MODDING_TOOLS.md) | Available modding tools and integration points |
| [`reference/tools-llm-reference.md`](reference/tools-llm-reference.md) | Tools, debuggers, and MCP servers for the TRL × RTX Remix workflow |

---

## Guides

Step-by-step how-to documentation.

| File | Description |
|------|-------------|
| [`guides/TRL-RTX-Remix-Project-Setup.md`](guides/TRL-RTX-Remix-Project-Setup.md) | Initial project setup and environment configuration |
| [`guides/rtx-remix-integration.md`](guides/rtx-remix-integration.md) | Integrating the proxy with RTX Remix |
| [`guides/light-placer.md`](guides/light-placer.md) | Using the Remix light placer for stage lights |
| [`guides/troubleshooting.md`](guides/troubleshooting.md) | Common issues and fixes |
| [`guides/GhidrAssist-Ghidra-Install.md`](guides/GhidrAssist-Ghidra-Install.md) | Ghidra + GhidrAssist installation and setup |
| [`guides/menuhook/`](guides/menuhook/) | MENUHOOK mod system (overview, development, features, mods, patches) |

---

## Research

Deep-dive analysis reports and experiment logs.

| File | Description |
|------|-------------|
| [`research/TERRAIN_ANALYSIS.md`](research/TERRAIN_ANALYSIS.md) | Terrain rendering pipeline: TerrainDrawable, 3-layer culling architecture, Layer 31 frustum culler at 0x40C430 |
| [`research/TRL-RTX-Remix-Paper.md`](research/TRL-RTX-Remix-Paper.md) | Full technical paper: FFP proxy design and RTX Remix integration |
| [`research/trl-vs-constant-layout.md`](research/trl-vs-constant-layout.md) | VS constant register layout analysis (c0–c96 call sites, matrix upload paths) |
| [`research/TRL-Lara-Visibility-Fix-Report.md`](research/TRL-Lara-Visibility-Fix-Report.md) | Lara character visibility investigation |
| [`research/TRL_FFP_Proxy_RTX_Remix_Technical_Pipeline.md`](research/TRL_FFP_Proxy_RTX_Remix_Technical_Pipeline.md) | Technical pipeline: proxy → FFP → Remix |
| [`research/TRL-RTX-Remix-Experiment-Log.md`](research/TRL-RTX-Remix-Experiment-Log.md) | Chronological experiment log |
| [`research/TRL-RTX-Remix-RE-Roadmap.md`](research/TRL-RTX-Remix-RE-Roadmap.md) | Reverse engineering roadmap and open questions |
| [`research/TRL-RTX-Remix-Deep-Analysis-Report.md`](research/TRL-RTX-Remix-Deep-Analysis-Report.md) | Deep analysis of engine internals |
| [`research/TRL_RenderDoc_Capture_Analysis.md`](research/TRL_RenderDoc_Capture_Analysis.md) | RenderDoc frame capture analysis |
| [`research/TR7_RTX_Remix_Research.md`](research/TR7_RTX_Remix_Research.md) | TR7 engine research applicable to TRL |
| [`research/dxvk-debug-usd-analysis-design.md`](research/dxvk-debug-usd-analysis-design.md) | DXVK debug + USD analysis design spec |
| [`research/trl-ffp-discovery.md`](research/trl-ffp-discovery.md) | Initial FFP discovery: how proxy intercepts shader-bound draws |
| [`research/trl-ffp-ghidra-caps-gate.md`](research/trl-ffp-ghidra-caps-gate.md) | Ghidra analysis of the CAPS gate in the FFP pipeline |
| [`research/deep-research-report.md`](research/deep-research-report.md) | Comprehensive deep-research report on TRL engine internals |

---

## Archive

Historical artifacts from earlier sessions. Not actively maintained.

```
archive/
├── sessions/                          # Session handoffs from early development
├── prompts/                           # AI prompts and skill files from early development
│   └── SESSION_PROMPTS.md             # Ready-to-paste Claude Code session starters
├── compass/                           # Compass AI assistant analysis outputs
├── TRL-RTX-Remix-Workspace-Analysis.md  # Workspace analysis from initial project setup (2026-03-19)
├── DEEP_RESEARCH_QUERIES.md           # Research queries for Claude.ai deep research mode
└── Combined_Research_Docs.md          # Consolidated early research dump
```
