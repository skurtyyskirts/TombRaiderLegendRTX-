# TRL RTX Remix — D3D9 Frame Captures

Full-frame D3D9 API traces captured with the [`graphics/directx/dx9/tracer/`](../graphics/directx/dx9/tracer/) proxy. Each capture records every D3D9 call for one or more frames, including arguments, matrices, shader bytecodes, vertex declarations, and backtraces.

---

## Captures

### `dx9tracer-captures/`

| File | Description |
|------|-------------|
| `trl_capture_fixed_geo.jsonl` | Frame captured with sector visibility patches active (geometry fully submitting, ~190K draw calls) |
| `trl_capture_fixed_geo_proxy.log` | Proxy log for the fixed-geometry session |
| `trl_capture_broken_geo.jsonl` | Frame captured without sector patches (baseline, ~91K draw calls) |
| `trl_capture_broken_geo_proxy.log` | Proxy log for the broken-geometry session |

These captures predate the terrain rendering investigation. A near-vs-far frame diff (anchor geometry present vs. absent) has not yet been captured — that is the [next planned step](../TRL%20tests/WHITEBOARD.md).

---

## Analyzing a Capture

```bash
# Overview: call counts per frame and method
python -m graphics.directx.dx9.tracer analyze <file.jsonl> --summary

# Group draw calls by render target
python -m graphics.directx.dx9.tracer analyze <file.jsonl> --render-passes

# Disassemble all shaders with constant register map
python -m graphics.directx.dx9.tracer analyze <file.jsonl> --shader-map

# Auto-tag draws (alpha, ztest, fullscreen-quad, etc.)
python -m graphics.directx.dx9.tracer analyze <file.jsonl> --classify-draws

# Diff two captures to find missing draw calls
python -m graphics.directx.dx9.tracer analyze <file.jsonl> --diff-frames 0 1

# Full state snapshot at a specific draw index
python -m graphics.directx.dx9.tracer analyze <file.jsonl> --state-snapshot 4200

# Resolve backtrace addresses to function names
python -m graphics.directx.dx9.tracer analyze <file.jsonl> --resolve-addrs "Tomb Raider Legend/trl.exe"
```

See the [tool catalog](../.claude/rules/tool-catalog.md) for the full list of analysis flags.
