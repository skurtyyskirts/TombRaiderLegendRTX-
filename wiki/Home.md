# Tomb Raider: Legend — RTX Remix Project Wiki

> **The canonical knowledge base for the Tomb Raider: Legend (2006) RTX Remix port.**
> A 32-bit x86 cdcEngine game with no native fixed-function pipeline brought to the threshold of full path-traced rendering through a D3D9 FFP proxy DLL, runtime memory patching, and three years of cumulative reverse engineering.

---

## What this project is

| Field | Value |
|---|---|
| **Game** | Tomb Raider: Legend (2006, Crystal Dynamics, cdcEngine, Steam PC, 32-bit x86) |
| **Goal** | Make TRL render through NVIDIA RTX Remix path tracer |
| **Approach** | D3D9 Fixed-Function-Pipeline (FFP) proxy DLL + 32 runtime memory patches |
| **Repo** | [skurtyyskirts/TombRaiderLegendRTX-](https://github.com/skurtyyskirts/TombRaiderLegendRTX-) |
| **License** | See [LICENSE](https://github.com/skurtyyskirts/TombRaiderLegendRTX-/blob/main/LICENSE) |
| **Builds completed** | 001–079, 071b (003–015, 034, 043, 048–063 not preserved) |

TRL renders exclusively through programmable vertex shaders. RTX Remix requires Fixed-Function Pipeline. The proxy intercepts D3D9 calls, reconstructs World / View / Projection matrices from VS constants, and feeds them to Remix through FFP calls — so Remix sees TRL as a native FFP game.

```
NvRemixLauncher32.exe → trl.exe → dxwrapper.dll → d3d9.dll (FFP proxy) → d3d9_remix.dll
```

See [[DLL-Chain-and-Architecture]] for the full picture.

---

## Where we are right now

**Last build:** `079` — skinned-character decl normalization (FAIL: shader-route mismatch).
**Last PASS:** `077` — DrawCache use-after-free fixed; cold launch stable.

**The one remaining gameplay blocker:**

> The eight anchor mesh hashes in `mod.usda` are stale. They were captured under a previous Remix configuration. Geometry **is** rendering (3,749 draw calls per scene) but no rendered mesh matches the stored hashes. **Next step: fresh Remix capture near the Peru stage.** See [[Build-074-077-Asset-Pipeline]] for the full story.

The full project state lives at [[Current-Status]].

---

## Sections of this wiki

### Project overview
- [[Project-Overview]] — public-facing summary, badges, quick start
- [[Current-Status]] — live status board (formerly WHITEBOARD.md)
- [[Glossary]] — terminology dictionary
- [[Changelog]] — chronological session log

### Architecture & pipeline
- [[DLL-Chain-and-Architecture]] — the proxy chain and why each link exists
- [[FFP-Proxy-Pipeline]] — 16-section deep dive on the D3D9 → FFP → Remix pipeline
- [[Generic-D3D9-Pipeline-Reference]] — pipeline-architecture background for newcomers
- [[Transform-Matrices]] — World × View × Projection composition, `fusedWorldViewMode`
- [[VS-Constant-Register-Layout]] — the c0–c96 register map for TRL
- [[Hash-Stability]] — how Remix hashes geometry and the three classic instabilities
- [[SHORT4-Vertex-Decoding]] — exhaustive D3DDECLTYPE_SHORT4 reference
- [[Hash-Debugger]] — diagnosing geometry-hash drift
- [[rtx-conf-Reference]] — every line of TRL's `rtx.conf` explained

### Engine memory map
- [[36-Layer-Culling-Map]] — the canonical table of every culling layer found in cdcEngine
- [[Engine-Memory-Map]] — globals, sector layout, renderer chain
- [[Rosetta-Stone]] — master cross-reference (addresses ↔ registers ↔ config ↔ rationale)

### Build history (per-phase narratives)
- [[Build-History-Index]] — every build with one-line summary
- [[Build-001-to-015-Baseline]] — early baselines, hash-stability proofs
- [[Build-016-to-044-Anti-Culling]] — the long culling-layer attack
- [[Build-045-073-Hash-Pipeline]] — VB management, content-fingerprint cache, FLOAT3 fixes
- [[Build-074-077-Asset-Pipeline]] — the `user.conf` foot-gun and cold-launch crash fix
- [[Build-078-079-Performance-and-Skinning]] — proxy optimization, open skinned-char hash drift
- [[Dead-Ends]] — every approach that was tried and failed (do not retry)

### Guides
- [[Setup-Guide]] — environment, build, deploy, launch
- [[Troubleshooting]] — symptom-driven Q&A
- [[Light-Placement]] — anchoring Remix lights to mesh hashes
- [[GhidrAssist-Install]] — Ghidra plugin install
- [[RTX-Remix-Integration-Guide]] — how Remix sees TRL

### Tools & toolkit
- [[Tools-Architecture-Overview]] — what's in `retools/`, `livetools/`, `graphics/`, `autopatch/`
- [[Reverse-Engineering-Toolkit]] — the static + dynamic analysis pipeline
- [[Modding-Tools-Catalog]] — community tools that target TRL / cdcEngine
- [[LLM-Tool-Reference]] — MCP servers and LLM-friendly tooling

### Research & investigations
- [[Research-Papers-Index]]
- [[TRL-RTX-Remix-Paper]] — formal paper
- [[Deep-Analysis-Report]] — 2026-03-19 snapshot
- [[Experiment-Log]] — chronological lab notebook
- [[RE-Roadmap]] — forward plan
- [[Terrain-Analysis]] — cdcEngine terrain pipeline deep dive
- [[Lara-Visibility-Fix-Report]] — the missing-character bug
- [[RenderDoc-Capture-Analysis]] — Vulkan-level frame capture
- [[TR7-RTX-Remix-Research]] — high-level engine compatibility report
- [[FFP-Discovery-Notes]] — original capability-gate findings
- [[FFP-Ghidra-Caps-Gate]] — Ghidra-driven caps gate analysis
- [[DXVK-Debug-USD-Analysis-Design]] — test pipeline additions

### Project meta
- [[Contributing]] — how to contribute
- [[Security-Policy]] — vulnerability reporting
- [[Sync-Setup]] — GitHub ↔ Linear sync configuration
- [[Third-Party-Licenses]] — bundled software acknowledgments
- [[Agents-Index]] — automation subagents (`.claude/agents/`)

---

## Quick reference

### Key addresses

| Address | Name | Purpose |
|---|---|---|
| `0x01392E18` | `g_pEngineRoot` | Root engine object |
| `0x010FC780` | View matrix source | Read by proxy each frame |
| `0x01002530` | Projection matrix source | Read by proxy each frame |
| `0xEFDD64` | Frustum distance threshold | Stamped to `-1e30f` |
| `0xF2A0D4 / D8 / DC` | Cull mode globals | Stamped to `D3DCULL_NONE` |
| `0x10FC910` | Far clip distance | Stamped to `1e30f` |
| `0x407150` | `SceneTraversal_CullAndSubmit` | 11 internal NOPs (NOT a RET) |
| `0x60B050` | `Light_VisibilityTest` | `mov al,1; ret 4` — always returns true |
| `0x40C430` | `RenderQueue_FrustumCull` | JMP to `0x40C390` (uncull path) |

The full set: [[Engine-Memory-Map]] · [[36-Layer-Culling-Map]] · [[Rosetta-Stone]]

### VS constant registers (TRL-specific)

```
c0–c3:   World matrix (transposed)
c8–c11:  View matrix
c12–c15: Projection matrix
c48+:    Skinning bone matrices (3 regs/bone)
```

View and Projection are **separate** registers, not a fused ViewProj. See [[VS-Constant-Register-Layout]].

### Build & test

```bash
pip install -r requirements.txt
python verify_install.py

# Full stage-light release gate
python patches/TombRaiderLegend/run.py test --build --randomize

# Hash-only nightly screening flow
python patches/TombRaiderLegend/run.py test-hash --build

# Autonomous patch-and-test loop
python -m autopatch
```

Full instructions: [[Setup-Guide]].

---

## How to contribute knowledge

1. Read [[Current-Status]] first.
2. Read [[Dead-Ends]] before proposing experiments.
3. Run a build under the [[Setup-Guide]] workflow.
4. Each build produces a `TRL tests/build-NNN-*/SUMMARY.md` — that file is the canonical record. The wiki page [[Build-History-Index]] is regenerated from those summaries.
5. Update [[Current-Status]] and [[Changelog]] on the same session.
6. Add any new failing approach to [[Dead-Ends]].

See [[Contributing]] for full guidelines.

---

## Why this wiki exists

For three years this project has accumulated discoveries faster than it has consolidated them. Knowledge lived in `docs/research/`, `docs/reference/`, `docs/status/`, dozens of `TRL tests/build-NNN/SUMMARY.md` files, CLAUDE.md, the CHANGELOG, hourly reviews, hot-path audits, agent prompts, archived sessions, and a 250 000-word `Combined_Research_Docs.md`.

This wiki **is** the consolidated, navigable version of all of that. Test build evidence (screenshots, proxy logs, JSONL traces, source snapshots) stays in `TRL tests/` — only the **knowledge** has been lifted into the wiki, where it can be cross-referenced and built upon by other projects.
