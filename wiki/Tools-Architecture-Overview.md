# Tools Architecture Overview

> A high-level inventory of what's in the repository for other DX9 (and DX11) → RTX Remix porting efforts. The whole repo is structured around reusability: every subsystem can be lifted into a new port.

The [[Reverse-Engineering-Toolkit]] page covers the static-and-dynamic analysis combo in more detail. This page focuses on what each directory contains and what it offers other projects.

## Top-level inventory

```
TombRaiderLegendRTX-/
├── proxy/                       D3D9 FFP proxy DLL source (the deliverable)
├── patches/TombRaiderLegend/    Per-game workspace — proxy source, kb.h, test harness, nightly orchestrator
├── retools/                     Offline static analysis (22 tools, ~6,690 lines Python)
├── livetools/                   Live dynamic analysis (Frida-based)
├── graphics/directx/dx9/tracer/ Full-frame D3D9 API capture
├── autopatch/                   Autonomous hypothesis → patch → test loop
├── automation/                  Build artifact validation, archive helpers
├── gamepilot/                   Vision-driven game agent (Claude via CLI)
├── agent/                       Inter-process signal bus
├── rtx_remix_tools/             Reusable templates + 16 DX9 analysis scripts
├── tools/                       gamectl, record_menu_nav, bundled radare2
├── scripts/                     session_start, daily_review, community_sweep
├── tests/                       Pytest suite for retools
├── tests_trl/                   TRL-specific pytest cases
├── wiki/                        This wiki — full project knowledge base
├── TRL tests/                   Archive of every build's evidence
├── TRL traces/                  Full-frame D3D9 captures
└── rtx.conf                     Remix runtime config (master template)
```

## proxy/

The D3D9 FFP proxy — a no-CRT, single-DLL D3D9 interposer that sits between the game and RTX Remix.

| File | Lines | Role |
|------|-------|------|
| `d3d9_device.c` | 5,502 | Core proxy — intercepts ~15 of 119 device methods, FFP conversion, memory patches |
| `d3d9_main.c` | 374 | DLL entry, hand-rolled `WriteFile` logging, Remix chain-load |
| `d3d9_wrapper.c` | 218 | `IDirect3D9` wrapper — intercepts `CreateDevice` |
| `d3d9_skinning.h` | 457 | Optional skinned-mesh support (`ENABLE_SKINNING=0` by default) |
| `d3d9.def` | 2 | DLL export table |
| `build.bat` / `dobuild.bat` / `rebuild.bat` | — | MSVC x86 build scripts (auto-find VS via `vswhere`) |
| `proxy.ini` | 50 | Runtime config |
| `d3d9.dll` | ~50 KB | Compiled artifact |

The authoritative source mirror is `patches/TombRaiderLegend/proxy/`. The `proxy/` at the repo root is kept in sync.

**Reusability for other ports: very high.** This is the working reference implementation of every concept in the FFP template. The hand-rolled logging, no-CRT vtable replacement, FFP engage/disengage state machine, and matrix transposition logic all port directly.

## retools/

Offline static analysis. 22 modules. Works on any MSVC-compiled PE binary.

### Run inline (fast, <5s)
- `sigdb.py fingerprint` — compiler ID via Rich header + markers + imports
- `sigdb.py identify` — single-function signature lookup
- `context.py assemble` — full analysis context gathering
- `context.py postprocess` — decompiler output post-processing pipe
- `readmem.py` — typed read from PE
- `dataflow.py --constants` / `--slice` — forward propagation / backward slice
- `asi_patcher.py build` — build ASI patch DLL from JSON spec

### Delegate to static-analyzer subagent
- `decompiler.py --types kb.h` — Ghidra-quality decompilation
- `pyghidra_backend.py analyze` — full Ghidra analysis (~5–15 min)
- `bootstrap.py` — first-pass auto-seed of `kb.h` (~2–5 min)
- `sigdb.py scan` — bulk signature scan (~1–3 min)
- `xrefs.py`, `datarefs.py`, `structrefs.py`, `vtable.py`, `rtti.py`, `throwmap.py`, `dumpinfo.py`, `cfg.py`, `callgraph.py`, `funcinfo.py`, `disasm.py`, `search.py`

The full tool catalog and decision guide lives in [[Reverse-Engineering-Toolkit]].

**Reusability: maximum.** Binary-agnostic. Drop-in for any MSVC PE.

## livetools/

Live dynamic analysis. Frida-based. Spawns a TCP daemon on `127.0.0.1:27042` that hosts the Frida session.

| Command | Purpose |
|---------|---------|
| `attach <process>` / `attach <path> --spawn` | Start session (spawn catches DLL init code) |
| `trace $VA --read` | Non-blocking N-hit log with register/memory reads |
| `steptrace $VA` | Instruction-level trace (Stalker) |
| `collect $VA $VA2 …` | Multi-address hit counting over duration |
| `bp add/del/list` / `watch` / `regs` / `stack` / `bt` | Breakpoints + inspection |
| `mem read / write / scan` / `disasm` | Memory ops |
| `dipcnt on/off/read` / `dipcnt callers` | D3D9 DrawIndexedPrimitive counter + caller histogram |
| `memwatch start/stop/read` | Memory write watchpoint with backtrace |
| `modules` | Loaded modules with base addresses |
| `analyze <jsonl>` | Offline aggregation of collected trace data |

The big internal modules:
- `__main__.py` (1,230 lines) — CLI router
- `server.py` (788 lines) — Frida daemon
- `agent.js` — in-process Frida JS — actual hook implementations
- `gamectl.py` (420 lines) — SendInput + AttachThreadInput focus + macro replay/recording

**Reusability: maximum.** The Frida agent is x86-generic; `gamectl.py` solves the universal "DX9 game ignores window messages" problem and is reused by `gamepilot`, `automation`, `tools/record_menu_nav.py`, and the autopatch macro replay.

## graphics/directx/dx9/tracer/

Codegen-driven full-frame D3D9 API tracer.

Build flow:
```
d3d9_methods.py  (Python codegen)
   ├─ generates d3d9_trace_hooks.inc (580 lines C)
   └─ feeds into src/ → MSVC build → d3d9.dll (160 KB)
       └─ deployed to game dir, triggered via filesystem trigger
       └─ writes JSONL of every device method call
       └─ analyze.py (1,927 lines) aggregates offline
```

Analysis options (under `python -m graphics.directx.dx9.tracer analyze <jsonl> [OPTIONS]`):
- `--summary` / `--draw-calls` / `--callers METHOD` / `--hotpaths`
- `--state-at SEQ` / `--state-snapshot DRAW#` — full device-state reconstruction
- `--render-loop` / `--render-passes` / `--rt-graph` — pass classification
- `--matrix-flow` / `--shader-map` (CTAB register names) / `--const-evolution vs:c0-c3`
- `--const-provenance` / `--const-provenance-draw N`
- `--classify-draws` / `--vtx-formats`
- `--redundant` / `--texture-freq` / `--transform-calls` / `--animate-constants`
- `--diff-draws A B` / `--diff-frames A B` (used by autopatch.diagnose for near-vs-far)
- `--pipeline-diagram` (auto-Mermaid)
- `--resolve-addrs BINARY` (resolves backtraces via retools)

**Reusability: maximum for DX9 work.** The codegen pattern is the cleanest way to add a Pix-style D3D9 capture to any port.

## autopatch/

Autonomous solver. Diagnoses why geometry disappears at distance, generates candidate patches, applies them at runtime via livetools, runs a 3-position movement test, evaluates pass/fail via pixel heuristics. Iterates up to 10 hypotheses.

| Module | Role |
|--------|------|
| `orchestrator.py` | Top-level loop |
| `diagnose.py` | Near/far frame capture via dx9 tracer + draw-call diff |
| `hypothesis.py` | Candidate generation — decompile callers, extract jumps, rank, filter tried-list |
| `patcher.py` | Runtime patch via `livetools mem write`; promote to proxy C source on success |
| `evaluator.py` | Screenshot pixel heuristics for red + green stage lights |
| `safety.py` | Sanity checks |
| `knowledge.py` | Persistent iteration history |
| `macros.json` | Movement macros |
| `knowledge.json` | 83 KB log of every address tried |

Run modes:
- `python -m autopatch` — full run
- `python -m autopatch --skip-diagnosis` — reuse cached diagnostic data
- `python -m autopatch --dry-run` — evaluator calibration only

**Reusability: high (as pattern).** The orchestrator is TRL-shaped (looks for red+green stage lights), but the four-phase pattern (differential capture → static-analysis hypothesis ranker → livetools patch → vision evaluator → C-source promotion) is fully portable. The 4 patch types (`nop_jump_6`, `nop_jump_2`, `ret_true`, `ret_true_stdcall`) are generic.

## automation/ and patches/TombRaiderLegend/

Build hygiene + per-game workspace.

**`automation/`** has:
- `build_validator.py` — md5sum + size verification against baseline (refuses LLM-generated metrics)
- `archive_utils.py` — standardized per-build archive (DLL, build.log, rtx.conf, **user.conf**, optional console.log / remix-dxvk.log / bridge.log / screenshots/)
- `macros.json` — `skip_cutscene` and `test_session` keystroke macros

**`patches/TombRaiderLegend/`** is the per-game workspace:
- `proxy/` — authoritative proxy source
- `kb.h` (994 lines) — knowledge base of TRL (DIP call sites, vtable offsets, struct layouts, function signatures)
- `run.py` — top-level test orchestrator (`record` / `test` / `test-hash` modes)
- `macros.json` — recorded test sessions
- `live_capture.py` — drives a ~2-minute dx9tracer capture (7,200 frames @ 60 fps)
- `deploy_build.py` — deploy a historical archived build to the game directory
- `launcher.py` — stable launch path
- `usd_analyze.py` — inspect mod.usda for anchor mesh-hash references
- `nightly/` (15 files) — autonomous-nightly subsystem
- `scripts/` — per-game wrapper copies of the dx scanners

The `automation/archive_utils.py` rule that always pulls `user.conf` (not just `rtx.conf`) is the reason [[Dead-Ends]] #14 will not happen again.

**Reusability: maximum.** The per-game-subdirectory pattern is the recommended layout for any port. Copy the template, edit `kb.h` and `VS_REG_*` defines, get a working baseline.

## rtx_remix_tools/

Reusable distribution layer. **Drop-this-folder starter kit for any new DX9 port.**

```
rtx_remix_tools/dx/
├── dx9_ffp_template/        Copy-this-folder DX9 FFP proxy starter kit
│   ├── proxy/               Template proxy source
│   ├── scripts/             Per-template script clones
│   └── kb.h                 Empty kb.h to seed
├── remix-comp-proxy/        Stripped per-game build skeleton (build.bat, d3d9.def)
└── scripts/                 The 16 DX analysis scanners — the primary reusable library
```

### The 16 DX9 analysis scripts

| Script | Finds |
|--------|-------|
| `find_d3d_calls.py` | D3D9/D3DX imports + every call site |
| `find_device_calls.py` | Device-vtable call patterns |
| `find_vs_constants.py` | `SetVertexShaderConstantF` sites |
| `find_ps_constants.py` | `SetPixelShaderConstantF/I/B` sites |
| `find_vtable_calls.py` | D3DX CTAB + D3D9 vtable indirect calls |
| `find_render_states.py` | `SetRenderState` with full enum decoding |
| `find_texture_ops.py` | Texture pipeline (stages, TSS ops, sampler) |
| `find_transforms.py` | `SetTransform` types |
| `find_surface_formats.py` | CreateTexture/RT/DS format extraction |
| `find_stateblocks.py` | State-block patterns |
| `decode_fvf.py` | FVF bitfield decode |
| `decode_vtx_decls.py` | Vertex declaration formats |
| `find_shader_bytecode.py` | Embedded shader bytecode extraction |
| `classify_draws.py` | Draw classification (FFP/shader/hybrid) |
| `find_matrix_registers.py` | Identify View/Proj/World matrix registers (CTAB + frequency) |
| `find_skinning.py` | Consolidated skinning analysis |
| `find_blend_states.py` | D3DRS_VERTEXBLEND + INDEXEDVERTEXBLENDENABLE |
| `scan_d3d_region.py` | D3D calls in code region |

**Reusability: maximum.** The 16 scanners are the single highest-value piece for new ports.

## gamepilot/

Vision-driven game agent. Captures via Win32 GDI (fast) or NVIDIA Shadowplay (`]` key, slow but full-resolution). Routes vision through Claude CLI (`claude -p --bare --model sonnet`) for state classification — no API billing.

States: `SETUP_DIALOG / MAIN_MENU / LOADING / GAMEPLAY / PAUSE_MENU / REMIX_MENU / CRASHED / UNKNOWN`.

Actions dispatched through `livetools.gamectl`.

**Reusability: high.** For any visual-output Win32 game where you want LLM-driven testing.

## tools/, scripts/, agent/

- `tools/gamectl.py` — extended SendInput automation
- `tools/record_menu_nav.py` — launches TRL, records menu nav keys with F12 stop
- `tools/radare2-6.1.0-w64/` — bundled radare2 (used by retools r2ghidra backend)
- `scripts/session_start.py` — generates session brief
- `scripts/daily_review.py` — Anthropic API blind-spot review against project state
- `scripts/community_sweep.py` — Claude with web search scans NVIDIA Game Works / dxvk-remix / etc.
- `agent/signal_bus.py` — POSIX-signal IPC between orchestrators (Linux/Mac only)

## tests/ and tests_trl/

- `tests/` (16 files) — Pytest suite for retools, uses `kernel32.dll` / `ntdll.dll` as sample PEs
- `tests_trl/` (9 files) — TRL-specific: release-gate evaluator, nightly dry-run, anchor refresh, proxy-log parsing, scoring, publication, USD analysis, water-rendering hypotheses

## rtx.conf and proxy.ini

The runtime configuration surface. See [[rtx-conf-Reference]] for line-by-line documentation of the canonical `rtx.conf`. `proxy.ini` highlights:

- `[Remix] Enabled=1`, `DLLName=d3d9_remix.dll` — chain-load Remix
- `[Chain] PreloadDLL=` — optional side-effect injection
- `[FFP] AlbedoStage=0`, `Float3RoutingMode=auto`, `NormalizeSkinnedDecl=1`
- `[Sky] EnableIsolation=1` plus `CandidateMinVerts=12000`, `CandidateMinPrims=30`, `WarmupScenes=300`

## Recommended starter kit for a new DX9 → Remix port

1. Copy `rtx_remix_tools/dx/dx9_ffp_template/` → `patches/<NewGame>/`.
2. Run all `rtx_remix_tools/dx/scripts/find_*` against the game binary to seed the kb.
3. Use `retools.bootstrap` to seed `kb.h` (RTTI + CRT + signatures, ~2–5 min).
4. Build the empty proxy, deploy with Remix disabled (`[Remix] Enabled=0`), check `ffp_proxy.log` for the actual VS constants the game writes.
5. Update `VS_REG_*` defines in `d3d9_device.c`, rebuild.
6. Use the dx9 tracer for full-frame captures to identify problem draw calls.
7. Use `livetools` to verify static findings live and to apply test patches.
8. Once stable, model your test harness on `patches/TombRaiderLegend/run.py` and (optionally) clone the autopatch + nightly subsystems with game-specific evaluators.

## See also

- [[Reverse-Engineering-Toolkit]] — static + dynamic + tracer combo deep dive
- [[Setup-Guide]] — installation, build, deploy, launch
- [[FFP-Proxy-Pipeline]] — what the proxy actually does
- [[Modding-Tools-Catalog]] — third-party tools that target TRL
