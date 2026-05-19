# Build 083 — variant 7 (widen gate to FLOAT3+COLOR) — FAIL (cache always misses)

**Date:** 2026-05-19 10:22 CT
**Source commit:** dd5cb61 + variant 7 widen-gate edit
**Variant:** widen `TRL_ForceSkinnedNullVS` gate from `posType==FLOAT3 && tcType==FLOAT4` to `posType==FLOAT3 && curDeclHasColor`

## Result

**FAIL — cache-misses-no-stable-hash**

Gate now fires (`MOVABLE forced null_vs: first occurrence` line present in
log). Cache replay logic runs. But Lara's body hash colors still drift
between shot 1 (10:22:31) and shot 2 (10:22:34). Cache hits counter never
reaches the 100-threshold (no `LaraVB cache hits=N` line). No crash.

## What Changed This Build

Single edit in `proxy/d3d9_device.c:1199`:

```diff
-    int isLaraClass = (self->curDeclPosType == D3DDECLTYPE_FLOAT3
-                       && self->curDeclTexcoordType == D3DDECLTYPE_FLOAT4);
+    int isLaraClass = (self->curDeclPosType == D3DDECLTYPE_FLOAT3
+                       && self->curDeclHasColor);
```

Synced root `proxy/d3d9_device.c` → `patches/TombRaiderLegend/proxy/d3d9_device.c`.

## Screenshot Analysis

| Region | Shot 1 (10:22:31) | Shot 2 (10:22:34) | Stable? |
|--------|-------------------|-------------------|---------|
| Hair top | pink/red | red | drifts |
| Face | cream/pink | bright green | **NO** |
| Shirt/torso | dark teal | bright blue | **NO** |
| Arm | white/cream | green | **NO** |
| Legs | yellow + orange | bright green | **NO** |

Lara is in a slightly different idle-animation frame in each shot. All
submeshes shift colors — none are stably anchored.

## Proxy Log Summary

```
LaraClassBindPoseCache: 1
SkinnedFloat3Route: null_vs
MOVABLE forced null_vs: first occurrence (Lara-class FLOAT3+FLOAT4tex → bind-pose to Remix)
VBfp vb=0x018F10F0  drawIdx=0..N  nv=21845  stride=24  csum=0x519E4D0B
```

- Gate fires for the menu's first FLOAT3+COLOR draw.
- Per-draw fingerprint logger filled with 40 entries, **all identical**:
  same `vb`, same `nv=21845`, same csum, same first-vertex position.
- `nv=21845` exceeds the cache size cap (`nv <= 16384`), so this large mesh
  routes through null_vs **without** entering the cache lookup/capture path.
- No `LaraVB cache hits=` telemetry line. Counter never reached 100. Either
  no draws hit the cache, OR every draw is a fresh capture that never
  generates a hit on a subsequent frame.

## Root Cause (Hypothesis)

The cache key is `(nv, pc, tex0, bvi, mi)`. TRL CPU-skins Lara into a
shared streaming vertex buffer, and the engine very likely writes each
submesh at a **different `bvi` (base vertex index) per frame** — the
streaming allocator advances. With `bvi` in the cache key, every draw
looks like a new submesh, so:

1. Lookup misses → `Lara_CaptureCacheSlot` runs.
2. 64-slot cache fills within a few frames.
3. Subsequent frames also miss and the cache-full log line gates further
   capture, but no hits accumulate against the captured entries because
   `bvi` of the requested draw never matches.

The cache effectively snapshots a *one-shot bind-pose* for the first 64
unique `(nv, pc, tex0, bvi, mi)` tuples seen and then never replays any
of them again. The Remix view sees fresh, live, skinned vertices on every
subsequent frame — exactly the original drift problem.

## Next Build Plan (Build 084 — iteration 3)

**Drop `bvi` and `mi` from the cache key.** New key: `(nv, pc, tex0)`.
This treats every draw of the same submesh signature as a cache hit
regardless of where the streaming allocator placed it this frame. Capture
on first sight by snapshotting `nv * stride` bytes starting at the
current frame's `bvi`. Replay binds the captured VB at offset 0 with
`bvi = -mi`, same as today.

Expected on PASS:
- `LaraVB cache hits=100` shortly after main menu settles.
- Lara hash colors identical between shot 1 and shot 2.
- The pose itself may visually freeze in the Remix view (acceptable per
  the build-081 commit message; raster path still animates).

## Open Hypotheses

1. If iter3 still fails, mi might encode submesh identity in TRL's
   non-instancing scheme; check whether `mi` (min vertex index) actually
   varies per-frame or per-submesh. If per-submesh, keep `mi` in the key
   but drop `bvi`.
2. `tex0` might also vary if the menu uses a paletted/animated texture.
   Variant 2 from the plan (drop `tex0`) is the next fallback.
3. The `nv=21845` mesh is probably the menu background pyramid, not Lara.
   Confirm by adding `numElems` to the cache key, or by reading the
   active world matrix at the gate site and skipping non-character world
   transforms.
