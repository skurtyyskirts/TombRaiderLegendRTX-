# Reverse Engineering Toolkit

> The static + dynamic + capture combination used to take a closed-source 2006 D3D9 game from "unknown engine architecture" to "32 culling layers patched, full hash recovery, working RTX path tracing." Reusable for any MSVC-compiled DX9 game targeted at RTX Remix.

This page is the **how to use the tools** guide. For inventory of what's in the repo see [[Tools-Architecture-Overview]].

## The three pillars

```
┌─────────────────────────┐  ┌─────────────────────────┐  ┌─────────────────────────┐
│   retools/  (static)    │  │  livetools/  (dynamic)  │  │  dx9 tracer  (capture)  │
│                         │  │                         │  │                         │
│ • Decompile             │  │ • Attach to running     │  │ • Full-frame D3D9 API   │
│ • Xrefs                 │  │   process via Frida     │  │ • All 119 device methods│
│ • RTTI                  │  │ • Trace, breakpoint     │  │ • JSONL with backtraces │
│ • Throwmap              │  │ • mem read/write        │  │ • Shader disasm         │
│ • Bootstrap kb.h        │  │ • dipcnt, memwatch      │  │ • State reconstruction  │
│                         │  │ • Frida JS agent        │  │                         │
└────────────┬────────────┘  └────────────┬────────────┘  └────────────┬────────────┘
             │                            │                            │
             │  finds clues (addresses)   │  confirms & patches live   │  proves frame-level behavior
             │                            │                            │
             └────────────────────────────┴────────────────────────────┘
                                          │
                                          ▼
                                  ┌──────────────┐
                                  │   proxy/     │
                                  │  (the DLL)   │
                                  └──────────────┘
```

The rule: **static analysis finds clues; live tools confirm and act on them.** Don't waste cycles on offline analysis when a 30-second `livetools trace` would answer the question.

## Static analysis (`retools/`)

### Setup

```bash
# One-time, ~600MB Ghidra download for the pyghidra backend
python verify_install.py --setup
```

### When to use it

| Question | Tool |
|---|---|
| "What compiler built this binary?" | `python -m retools.sigdb fingerprint trl.exe` |
| "Is this function a known library?" | `python -m retools.sigdb identify trl.exe 0x401200` |
| "Read a typed value from the PE" | `python -m retools.readmem trl.exe 0xEFDD64 float` |
| "Full context before reasoning about a function" | `python -m retools.context assemble trl.exe 0x401500 --project TombRaiderLegend` |
| "Decompile this function" | (delegate to static-analyzer subagent) — `decompiler.py trl.exe 0x401000 --types patches/TombRaiderLegend/kb.h` |
| "Who calls this function?" | (delegate) — `xrefs.py trl.exe 0x401000 -t call` or `callgraph.py --up` |
| "Where is this global read?" | (delegate) — `datarefs.py trl.exe 0x7A0000 --imm` |
| "What does this struct look like?" | (delegate) — `structrefs.py --aggregate --fn 0x401000 --base esi` |
| "What C++ class is this vtable?" | (delegate) — `rtti.py trl.exe vtable 0x6A0000` |
| "What's a crash dump telling me?" | (delegate) — `dumpinfo.py crash.dmp diagnose --binary d3d9.dll` |
| "Bootstrap a new binary" | (delegate, 2–5 min) — `bootstrap.py game.exe --project NewGame` |

### Knowledge-base (`kb.h`) convention

Every project has a `patches/<project>/kb.h` that accumulates discoveries:

```c
// Function: @ 0xADDR signature
@ 0x407150 void __cdecl SceneTraversal_CullAndSubmit(void *ctx);

// Global: $ 0xADDR name type
$ 0x01392E18 EngineRoot* g_pEngineRoot;
$ 0xEFDD64 float g_frustumThreshold;

// Struct
struct Sector {
    int gate1;    // +0x84
    int gate2;    // +0x94
    int light_count;  // +0x1B0
    Light** light_list;  // +0x1B8
    // ...
};

// Enums for magic constants
enum LightType {
    LIGHT_OMNI = 0,
    LIGHT_SPOT = 1,
    LIGHT_AMBIENT = 2,
};
```

Every `decompiler.py` invocation must pass `--types kb.h`. This makes subsequent decompilations replace `FUN_00407150` with `SceneTraversal_CullAndSubmit`, dereference `0xEFDD64` as `g_frustumThreshold`, and so on. Discoveries compound.

### Dual-backend decompilation

For complex exploratory tasks (mapping a subsystem), spawn **two parallel static-analyzer agents**:

1. **r2ghidra** (`--backend pdg --types kb.h`) — better `__thiscall` recovery, low-level D3D
2. **pyghidra** (`pyghidra_backend.py decompile`) — better library call resolution, type propagation

They write to `findings_r2.md` and `findings.md` respectively. Merging both gives a complete picture. For single-function decompilation, `--backend auto` is fine.

## Dynamic analysis (`livetools/`)

### Setup

```bash
python -m livetools attach trl.exe     # process name
python -m livetools attach 12345       # or pid
python -m livetools attach trl.exe --spawn  # spawn suspended, instrument, resume (catches init code)
```

The TCP daemon binds `127.0.0.1:27042`. All commands talk to it.

### When to use it

| Question | Command |
|---|---|
| "Is this function reached?" | `livetools trace 0x60C7D0 --count 20` |
| "What are the actual register values?" | `livetools trace 0x401000 --read "eax; [esp+4]:4:uint32"` |
| "Step through instructions" | `livetools steptrace 0x401000` |
| "Set a breakpoint" | `livetools bp add 0x401000` then `livetools watch` then `livetools regs` |
| "Read live memory" | `livetools mem read 0xEFDD64 4 --as float32` |
| "Write live memory (test patch)" | `livetools mem write 0x401234 909090` |
| "How many DIP calls per frame?" | `livetools dipcnt on` then `livetools dipcnt read` |
| "Where do DIP calls come from?" | `livetools dipcnt callers 1000` |
| "Who writes to this address?" | `livetools memwatch start 0x7A0000 4` then `livetools memwatch read` |
| "What modules are loaded and where?" | `livetools modules --filter d3d9` |

### Static-to-runtime address mapping (ASLR)

x86 games without ASLR: PE preferred base == runtime base. `retools` addresses work directly in `livetools`.

DLLs and ASLR-enabled executables: `runtime_addr = runtime_base + (static_addr - preferred_base)`. Get `runtime_base` from `livetools modules --filter <name>` and `preferred_base` from the PE.

For TRL: `trl.exe` has no ASLR, so all addresses match. `d3d9_remix.dll` does have ASLR.

### Hooking game CALLs vs DLL entries

To trace a D3D9 method, hook the `call [reg+offset]` instruction **in the game's .exe** (found via `xrefs.py` or `vtable.py calls`), NOT the function entry inside the DLL. The game's call site has arguments in known stack positions. The DLL entry may be wrapped by other proxies and is shared across all callers.

## Frame capture (`graphics/directx/dx9/tracer/`)

### Setup

```bash
# Regenerate hooks from method definitions
python -m graphics.directx.dx9.tracer codegen -o d3d9_trace_hooks.inc

# Build the tracer proxy DLL
cd graphics/directx/dx9/tracer/src && build.bat

# Deploy: tracer DLL + proxy.ini → game directory
# (during diagnostics — swap with the FFP proxy)

# Trigger a capture (3s countdown)
python -m graphics.directx.dx9.tracer trigger --game-dir "<GAME_DIR>" --frames 2 --delay 0 --wait
```

`proxy.ini` settings: `CaptureFrames=N`, `CaptureInit=1` (capture boot-time calls like shader creation), `Chain.DLL=<wrapper.dll>` (chain to another wrapper, or empty for system d3d9).

### Analysis (`python -m graphics.directx.dx9.tracer analyze <jsonl> [OPTIONS]`)

| Option | Purpose |
|--------|---------|
| `--summary` | Overview: calls per frame/method, backtrace completeness |
| `--draw-calls` | Every draw call with state deltas |
| `--callers METHOD` | Caller histogram for a method |
| `--hotpaths` | Frequency-sorted call paths from backtraces |
| `--state-at SEQ` | Reconstruct full device state at a sequence number |
| `--render-loop` | Detect render loop entry from backtraces |
| `--render-passes` | Group draws by render target, classify pass types |
| `--matrix-flow` | Track matrix uploads per SetTransform / SetVertexShaderConstantF |
| `--shader-map` | Disassemble all shaders (CTAB names, register map) |
| `--const-provenance` | Compact: per draw, which seq# set each named constant |
| `--const-provenance-draw N` | Detailed: all register values and sources for draw N |
| `--classify-draws` | Auto-tag draws (alpha, ztest, fog, fullscreen-quad) |
| `--vtx-formats` | Group draws by vertex declaration |
| `--redundant` | Find redundant state-set calls |
| `--texture-freq` | Texture binding frequency |
| `--rt-graph` | Render target dependency (mermaid) |
| `--diff-draws A B` | State diff between two draw calls |
| `--diff-frames A B` | Compare two captured frames (used by autopatch.diagnose) |
| `--const-evolution vs:c4-c6` | Track register changes across draws |
| `--state-snapshot DRAW#` | Complete state dump at a draw index |
| `--transform-calls` | SetTransform/SetViewport timing relative to draws |
| `--animate-constants` | Cross-frame constant register tracking |
| `--pipeline-diagram` | Auto mermaid render pipeline diagram |
| `--resolve-addrs BINARY` | Resolve backtrace addresses to function names via retools |
| `--filter EXPR` | Filter by field (e.g. `frame==0`, `slot==83`) |
| `--export-csv FILE` | Export raw records to CSV |

### When tracer beats live tools

- "Which draws disappear at distance?" — `--diff-frames 0 1` between near-stage and far-stage captures
- "What VS constants change frame-to-frame?" — `--animate-constants`
- "What's the render target dependency graph?" — `--rt-graph`
- "Which textures are bound to which stage at draw N?" — `--state-snapshot N`

## The static + dynamic + tracer workflow

The full investigation pattern, demonstrated by the project's most successful builds:

1. **Question:** "Why does the green stage light vanish at distance?"
2. **Tracer captures:** Near-stage and far-stage 2-frame captures with the FFP proxy active.
3. **`--diff-frames 0 1`:** Identifies the draw calls present near but absent far. Lists their caller addresses.
4. **Static analyzer (parallel):**
   - Decompile each caller's function with `decompiler.py --types kb.h`
   - Extract conditional jumps and their targets
   - Rank by proximity to the missing-draw site and type
5. **Live tools:**
   - `livetools trace` each candidate function with `--read` to see register values when it's reached
   - `livetools memwatch` on suspected gate fields
   - `livetools mem write` to NOP the candidate jump and verify the draw returns
6. **Promotion:** Hardcode the patch into `proxy/d3d9_device.c`'s `TRL_ApplyMemoryPatches()`. Rebuild. Verify the proxy log shows `[PATCH OK]` for the address.
7. **Capture again:** Run the test pipeline (`run.py test --build`); both lights visible in screenshots is PASS.

This is the workflow autopatch automates (with vision-based screenshot evaluation instead of static screenshot review). The autopatch's 4 patch types (`nop_jump_6`, `nop_jump_2`, `ret_true`, `ret_true_stdcall`) cover ~95% of the patches in the [[36-Layer-Culling-Map]].

## Dx9-specific analysis scripts (`rtx_remix_tools/dx/scripts/`)

Fast first-pass tools for D3D9 questions. **Run these before delegating to a static-analyzer agent.**

| Script | Use case |
|--------|---------|
| `find_d3d_calls.py` | "What D3D9/D3DX functions does this binary import and where?" |
| `find_vs_constants.py` | "Which call sites upload VS constants and to which registers?" |
| `find_render_states.py` | "What `SetRenderState` calls does this binary make? Decoded by enum." |
| `find_transforms.py` | "Does this binary use `SetTransform`?" (no → FFP-incompatible without proxy) |
| `find_matrix_registers.py` | "Which VS register holds View, Proj, World?" — CTAB + frequency analysis |
| `find_skinning.py` | "Does this binary do software skinning? Where?" |
| `classify_draws.py` | "FFP, shader, or hybrid?" — Tells you whether a proxy is needed at all |
| `decode_vtx_decls.py` | "What vertex layouts does this binary use?" |

Running all 16 against a fresh binary takes ~5 minutes total and produces enough data to design the proxy's `GAME-SPECIFIC` `VS_REG_*` defines.

## See also

- [[Tools-Architecture-Overview]] — directory inventory
- [[Setup-Guide]] — environment setup
- [[Stable-Hashes-Technical-Analysis]] — example of toolkit-driven investigation
- [[FFP-Proxy-Pipeline]] — the proxy that consumes the toolkit's output
- [[Glossary]] — terminology used across the toolkit
