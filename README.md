# Tomb Raider Legend — RTX Remix

> **Goal:** Make Tomb Raider Legend (2006, PC) render correctly under NVIDIA RTX Remix for full path-traced lighting with stable per-mesh material assignments.

---

## Quick Navigation

| What you need | Where to look |
|---------------|---------------|
| Project status + culling layer map | [TRL tests/WHITEBOARD.md](TRL%20tests/WHITEBOARD.md) |
| Build-by-build results (44 builds) | [TRL tests/TEST_STATUS.md](TRL%20tests/TEST_STATUS.md) |
| Test build archive | [TRL tests/](TRL%20tests/) |
| Proxy DLL source | [proxy/](proxy/) |
| Technical documentation | [docs/](docs/) |
| Static analysis tools | [retools/](retools/) |
| Live analysis tools (Frida) | [livetools/](livetools/) |
| Test automation | [automation/](automation/) |
| D3D9 frame tracer | [graphics/directx/dx9/tracer/](graphics/directx/dx9/tracer/) |

---

## The Problem & Solution

TRL renders exclusively via vertex shaders. RTX Remix requires the D3D9 Fixed-Function Pipeline (FFP) to identify geometry, assign asset hashes, and inject path-traced lighting — shader-based draws produce unstable hashes and wrong material assignments because Remix cannot decode shader constant semantics.

**Solution:** A custom `d3d9.dll` proxy that sits between TRL and RTX Remix. It intercepts D3D9 API calls, reverse-engineers the vertex shader constant layout, reconstructs world/view/projection matrices, NULLs the vertex shaders, calls `SetTransform` to feed those matrices through FFP, then chains to the real Remix DLL. Remix sees TRL as if it were a native FFP game.

The proxy also defeats TRL's aggressive frustum culling, which would hide geometry that Remix needs to hash and light correctly.

---

## Current Status

| Milestone | Status |
|-----------|--------|
| FFP proxy DLL — builds and chains to Remix | **Done** |
| Transform pipeline (View / Proj / World) | **Done** |
| Asset hash stability (static + moving camera) | **Done** |
| Automated test pipeline | **Done** |
| DirectInput scancode delivery | **Done** |
| Backface culling disabled | **Done** |
| Frustum / distance culling disabled | **Done** |
| Sector / portal visibility disabled | **Done** |
| Per-light culling gates disabled | **Done** |
| **Both stage lights stable at all positions** | **Failing** |

**Last confirmed PASS:** `build-019` — both stage lights visible, hashes stable (2026-03-25)

**Latest build:** `build-044` — all three render paths patched; terrain rendering path (`TerrainDrawable` at `0x40ACF0`) identified as prime suspect; anchor geometry still disappears at distance.

**Root cause (reframed build 038):** The "red light at distance" in builds 019–037 was the RTX fallback light. With a neutral fallback, both stage lights vanish when Lara walks away — the problem is **anchor geometry not being submitted**, not light culling. All 22 identified culling layers have been addressed; the unexplored `TerrainDrawable` path is the remaining candidate.

See [`TRL tests/WHITEBOARD.md`](TRL%20tests/WHITEBOARD.md) for the full culling layer map and build-by-build history.

---

## Repository Structure

```
.
├── proxy/                          # Current working proxy source + compiled DLL
│   ├── d3d9_device.c               # Core proxy: ~2100 lines, intercepts ~15 of 119 device methods
│   ├── d3d9_main.c                 # DLL entry, logging, chain-load to Remix
│   ├── d3d9_wrapper.c              # IDirect3D9 wrapper (create + relay)
│   ├── d3d9_skinning.h             # Optional skinning (ENABLE_SKINNING=0 by default)
│   ├── build.bat                   # MSVC x86 build script (uses vswhere to find VS)
│   ├── d3d9.def                    # DLL export table
│   ├── d3d9.dll                    # Compiled proxy binary (deployed to game dir)
│   └── proxy.ini                   # Runtime config: Remix chain-load, albedo stage
│
├── patches/TombRaiderLegend/       # Project workspace (git-ignored)
│   ├── proxy/                      # Authoritative proxy source (synced to proxy/)
│   ├── run.py                      # Test orchestrator (build → deploy → launch → macro → collect)
│   ├── kb.h                        # Knowledge base: discovered functions, globals, structs
│   ├── findings.md                 # Accumulated static analysis findings
│   ├── TRL_TEST_CYCLE.md           # Pass/fail criteria and common mistakes
│   └── AUTOMATION.md               # Test pipeline documentation
│
├── TRL tests/                      # Test build archive — every build committed and pushed
│   ├── WHITEBOARD.md               # Live project status: culling layer map, build history, decision tree
│   ├── TEST_STATUS.md              # Build-by-build results and what remains to be done
│   └── build-NNN-<description>/    # One folder per test run (SUMMARY.md + screenshots + proxy source)
│
├── docs/
│   ├── research/                   # Deep-dive technical reports and analysis
│   ├── reference/                  # Definitions, pipeline architecture, RTX config reference
│   ├── guides/                     # Project setup, Ghidra install, RTX Remix integration
│   └── archive/                    # Historical session handoffs and early artifacts
│
├── retools/                        # Static analysis toolkit (offline PE analysis)
├── livetools/                      # Live dynamic analysis toolkit (Frida-based)
├── graphics/directx/dx9/tracer/    # D3D9 frame capture and analysis tool
├── rtx_remix_tools/                # Reusable FFP proxy template for other RTX Remix ports
├── Tomb Raider Legend/             # Game directory (trl.exe, NvRemixLauncher32.exe, rtx.conf)
└── requirements.txt                # Python deps: frida, pefile, capstone, r2pipe, minidump
```

---

## How the Proxy Works

The proxy is a no-CRT `d3d9.dll` compiled with MSVC x86 that replaces the game's D3D9 entry point via COM vtable replacement.

| Method | What the proxy does |
|--------|---------------------|
| `SetVertexShader` | When shader is NULLed, triggers FFP mode for the upcoming draw. |
| `SetVertexShaderConstantF` | Captures VS constant registers into a per-draw constant bank. |
| `SetRenderState` | Intercepts `D3DRS_CULLMODE` — forces `D3DCULL_NONE`. |
| `DrawIndexedPrimitive` | Reconstructs World/View/Proj matrices from constant bank, calls `SetTransform`, NULLs shader, relays draw. |
| `Present` | Logs diagnostics every 120 frames (draw counts, vpValid, patch confirmations). |

**VS Constant Register Layout (TRL-specific):**

```c
#define VS_REG_WVP_START     0   // c0–c3:  combined World-View-Projection (4×4)
#define VS_REG_VIEW_START    8   // c8–c11: View matrix
#define VS_REG_PROJ_START   12   // c12–c15: Projection matrix
#define VS_REG_BONE_START   48   // c48+:   skinning matrices (3 regs/bone)
```

The proxy reads `View` and `Projection` directly from TRL's in-memory matrix globals (confirmed addresses), reconstructs `World` as `WVP × (VP)⁻¹`, and feeds all three to `SetTransform`.

**Anti-Culling Patches (applied at proxy startup via memory writes):**

| Address | What | Why |
|---------|------|-----|
| `0x407150` | Write `0xC3` (RET) | Bypasses the entire per-object frustum cull function |
| `0x4070F0–0x407xxx` | NOP branches (11 sites) | Disables scene-traversal cull jumps |
| `SetRenderState` hook | Force `D3DCULL_NONE` | Overrides per-pass cull mode globals |
| `0x46C194`, `0x46C19D` | NOP portal gates | Defeats sector/portal visibility — produced 65× draw count increase |
| `0x60B050` | `mov al,1; ret 4` | `Light_VisibilityTest` always returns TRUE |

---

## What PASS Means

The test scene is the level-opening area with two colored stage lights (one red, one green) as set dressing. They're the target geometry because:

- They're spatially separated — both appear together only when culling is fully defeated
- They have distinct, stable asset hashes in RTX Remix
- Their position in frame shifts left/right as Lara strafes past them

**All five criteria must be true for a PASS:**

1. Both the **red** and **green** stage lights visible in **all 3** clean render screenshots
2. The lights **shift position** across the 3 screenshots (left/right relative to Lara)
3. Hash debug shows **same color for same geometry** across all 3 positions (no hash flipping)
4. No crash, no proxy log errors
5. Proxy log shows `vpValid=1`, patches confirmed, draw counts ~91,800+

**False positive detection:** If both lights appear in all 3 shots but their frame position hasn't shifted, Lara didn't move — the macro failed input delivery. Affected builds: 016, 019, 021.

---

## Running Tests

```bash
# Full build + test (compile proxy → deploy → launch game → run macro → collect results)
python patches/TombRaiderLegend/run.py test --build --randomize

# Test only (skip build, use last compiled proxy)
python patches/TombRaiderLegend/run.py test --randomize

# Record a new test macro
python patches/TombRaiderLegend/run.py record
```

`run.py` runs the entire pipeline autonomously:

1. *(Optional)* Build proxy via `proxy/build.bat`
2. Deploy `d3d9.dll` + `proxy.ini` to `Tomb Raider Legend/`
3. Write TRL graphics registry settings (lowest quality, fullscreen)
4. Kill any running `trl.exe`
5. Launch via `NvRemixLauncher32.exe trl.exe` — no focus touching, 20-second wait
6. Dismiss setup dialog via Win32 automation
7. Replay `test_session` macro (menu nav → level load → A/D strafes with `]` screenshot triggers)
8. Wait up to 70 seconds for `ffp_proxy.log` (proxy has a 50-second startup delay)
9. Kill `trl.exe` and collect the 3 most recent screenshots from the NVIDIA capture folder

**Never trigger this manually.** When the user says "begin testing", the agent runs the full workflow from `.claude/rules/begin-testing.md`.

---

## Build Archive

Every test run produces a build folder in `TRL tests/`:

```
build-NNN-<description>/
├── SUMMARY.md                      # Result, What Changed, Proxy Log, Findings, Hypotheses, Next Plan
├── phase1-hash-debug-posN.png      # Hash debug view screenshots
├── phase2-clean-render-posN-*.png  # Clean render screenshots
├── ffp_proxy.log
└── proxy/                          # Proxy source files at time of test
```

Naming convention: `build-019-miracle-...` for PASS builds, `build-020-lights-partial-fail` for FAIL builds. Every build is pushed to `skurtyyskirts/TombRaiderLegendRTX-` immediately — no batching.

---

## Tools

### Static Analysis (`retools/`) — Offline PE Analysis

Run from repo root with `python -m retools.<tool>`. Always pass `--types patches/TombRaiderLegend/kb.h` to the decompiler.

| Tool | Purpose |
|------|---------|
| `decompiler.py` | Ghidra-quality C decompilation with KB type injection |
| `disasm.py` | Disassemble N instructions at a virtual address |
| `xrefs.py` | Find all callers/jumps to an address |
| `callgraph.py` | Caller/callee tree (multi-level, `--up`/`--down`) |
| `datarefs.py` | Find instructions that read/write a global address |
| `structrefs.py` | Find `[reg+offset]` accesses; reconstruct structs with `--aggregate` |
| `search.py` | String search, byte pattern search, import/export list, instruction search |
| `rtti.py` | MSVC RTTI: resolve C++ class name + inheritance from vtable |
| `bootstrap.py` | Seed KB: compiler ID, signatures, RTTI classes, propagated labels (2–5 min) |
| `sigdb.py` | Bulk signature scan, single function ID, compiler fingerprint |
| `context.py` | Assemble full analysis context for a function; postprocess decompiler output |
| `dumpinfo.py` | Minidump analysis: exception, threads, stack walk, memory scan |
| `throwmap.py` | Map MSVC `_CxxThrowException` call sites to error strings; match against dump |

**Delegation rule:** Never run more than one `retools` command inline — delegate to a `static-analyzer` subagent. Exceptions (fast, <5s): `sigdb identify`, `sigdb fingerprint`, `context assemble`, `context postprocess`, `readmem.py`.

### Live Analysis (`livetools/`) — Frida-Based

```bash
python -m livetools attach trl.exe
python -m livetools trace 0x407150 --count 20
python -m livetools mem write 0x00F2A0D4 01000000
python -m livetools dipcnt on
```

| Command | Purpose |
|---------|---------|
| `trace $VA` | Non-blocking: log hits with register/memory reads |
| `bp add $VA` + `watch` | Blocking breakpoint; inspect with `regs`/`stack`/`bt` |
| `mem read/write $VA` | Inspect or patch live process memory |
| `memwatch` | Write watchpoint: catch who writes to an address |
| `dipcnt on/read` | D3D9 DrawIndexedPrimitive counter |
| `modules` | List loaded modules + base addresses |

TRL is 32-bit without ASLR — static addresses from `retools` map directly to runtime addresses.

### D3D9 Frame Tracer (`graphics/directx/dx9/tracer/`)

Captures every D3D9 call for one or more frames with arguments, matrices, shader bytecodes, and backtraces.

```bash
python -m graphics.directx.dx9.tracer trigger --game-dir "Tomb Raider Legend/"
python -m graphics.directx.dx9.tracer analyze capture.jsonl --shader-map --render-passes --classify-draws
```

Key analysis flags: `--shader-map` (disassemble all shaders), `--const-provenance` (which `SetVSConstantF` call set each register), `--render-passes` (group draws by render target), `--classify-draws` (auto-tag draws), `--state-snapshot DRAW#` (full device state at a draw index).

---

## Key Addresses

| Address | Symbol | Notes |
|---------|--------|-------|
| `0x00407150` | `SceneTraversal_CullAndSubmit` | Patched to RET; NOP'd 11 exit branches |
| `0x0046B7D0` | `RenderSector` | Per-sector render; proximity filter NOPed at `0x46B85A` |
| `0x0046C180` | `RenderVisibleSectors` | Sector iteration; visibility gates NOPed at `0x46C194/19D` |
| `0x0040ACF0` | `TerrainDrawable` | **Prime suspect** — unexplored terrain render path |
| `0x0060C7D0` | `RenderLights_FrustumCull` | Light render dispatch |
| `0x0060B050` | `Light_VisibilityTest` | Patched → always TRUE |
| `0x00413950` | `cdcRender_SetWorldMatrix` | Sets world matrix on renderer |
| `0x00F2A0D4` | `g_cullMode_pass1` | Stamped to `D3DCULL_NONE` per scene |
| `0x010FC780` | `g_viewMatrix` | Live view matrix read by proxy |
| `0x01002530` | `g_projMatrix` | Live projection matrix read by proxy |
| `0x010FC910` | `g_farClipDistance` | Stamped to 1e30f per BeginScene |
| `0x01392E18` | `g_pEngineRoot` | Engine root object |

---

## Knowledge Base (`patches/TombRaiderLegend/kb.h`)

Accumulates all reverse engineering discoveries. Format:

```c
// Structs
struct TRLRenderer { int x; float y[16]; };

// Functions — @ address name(signature)
@ 0x00413950 void __cdecl cdcRender_SetWorldMatrix(float* pMatrix);
@ 0x0060C7D0 void __cdecl RenderLights_FrustumCull(void* pScene);

// Globals — $ address type name
$ 0x010FC780 float g_viewMatrix[16]
$ 0x01002530 float g_projMatrix[16]
$ 0x01392E18 void* g_pEngineRoot
```

Always pass `--types patches/TombRaiderLegend/kb.h` to the decompiler so discovered names propagate through decompilation output.

---

## Operating Conventions

### Delegation

Static analysis (`retools`) → `static-analyzer` subagent (background). Live tools → main agent directly. Never run more than one `retools` command inline.

### Backups

Before any proxy edit: create `patches/TombRaiderLegend/backups/YYYY-MM-DD_HHMM_<description>/` with all modified files. Create the backup **before** making changes.

### Never Do

- Change the test procedure (A/D hold times are the only tunable)
- Batch-push multiple test results without a code change between runs
- Launch the game without the 20-second pre-input wait
- Touch the game window focus after launch
- Ask the user to copy files, launch the game, or confirm anything in the test pipeline

### Git

Push to `skurtyyskirts/TombRaiderLegendRTX-`. Every build — pass or fail — gets committed and pushed immediately. PASS builds include "miracle" in the folder name.

---

## Quick Start for a New Session

1. Read `patches/TombRaiderLegend/kb.h` — accumulated address map and struct layouts
2. Read `patches/TombRaiderLegend/findings.md` — static analysis findings
3. Read `TRL tests/WHITEBOARD.md` — full project status, culling layer map, decision tree
4. Check the latest build folder in `TRL tests/` and its `SUMMARY.md`
5. Read `.claude/rules/begin-testing.md` before running any test
6. Check `.claude/rules/tool-catalog.md` before choosing an analysis tool

To run a test: say **"begin testing"** — the agent handles everything.
