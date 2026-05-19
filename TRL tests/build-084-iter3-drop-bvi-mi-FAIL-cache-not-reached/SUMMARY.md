# Build 084 — iter3: drop bvi/mi from cache key — FAIL (cache path never reached)

**Date:** 2026-05-19 10:32 CT
**Variant:** drop `bvi` and `mi` from `Lara_LookupCacheSlot` match key. Match
on `(nv, pc, tex0)` only. Capture still records bvi/mi for diagnostics.

## Result

**FAIL — cache-not-reached**

Same screenshot drift as build 083. Proxy log is byte-for-byte the same size
(12933) and the same telemetry coverage — no `first bind-pose snapshot
committed`, no hit counter line, no `capacity reached`. The drop-bvi/mi
change was completely unreachable.

## Why The Change Couldn't Take Effect

Every gate-firing draw at the main menu shares one vertex buffer:

```
DECL seen=0x019B2B40 (posType=2 FLOAT3 + COLOR + TEXCOORD0 FLOAT2)
VBfp vb=0x019A0688  drawIdx=0..39  nv=21845  stride=24  csum=0x519E4D0B
```

`nv = 21845` exceeds the hard-coded cap in `useLaraCache`:

```c
int useLaraCache = (forceSkinnedNullVS
    && self->laraClassBindPoseCacheEnabled
    && self->streamVB[0]
    && self->streamStride[0] > 0
    && nv > 0 && nv <= 16384);
```

With `nv > 16384` the condition is false, so `Lara_LookupCacheSlot` and
`Lara_CaptureCacheSlot` are never called — and any change to the lookup key
becomes a no-op. The draw falls through to the live-VB null_vs path, Remix
snapshots fresh CPU-skinned vertices every frame, and the asset hash drifts.

Only one FLOAT3+COLOR decl is observed at the menu. Lara appears to be drawn
as a single high-poly super-mesh rather than multiple submeshes — at least in
this scene. Smaller submeshes don't show up to exercise the cache.

## Next Build Plan (Build 085 — iteration 4)

**Raise the cache nv cap from 16384 to 65535** (the D3D9 16-bit index
limit). 21845 fits. Memory budget: one 21845-vertex slot at stride=24 is
~524 KB; 64-slot cache worst case ~33 MB, acceptable.

Expected on PASS:
- `LaraVB cache: first bind-pose snapshot committed` log line appears.
- `LaraVB cache hits=100` after a couple of seconds at the menu.
- Lara hash colors identical between shot 1 and shot 2 (apart from the
  background, which is on the same null_vs path).

## Open Hypotheses

1. If raising the cap reveals the capture succeeds but hits stay zero, the
   `(nv, pc, tex0)` key may still be unstable across frames — likely `tex0`
   changes for the menu's animated/scrolled material. Fall back to keying
   on `(nv, pc)` only (variant 2 generalized).
2. The 21845-vertex VB content shows constant `csum=0x519E4D0B` across all
   40 fingerprint draws, which means the menu *background* (not Lara) is
   the static FLOAT3+COLOR mesh. Lara herself may be on a different decl
   (SHORT4 + skinning?) that the gate doesn't catch. If so, iter4 will
   stabilize the background but leave Lara drifting. Investigate by also
   logging the decl per draw, not just per first-occurrence.
