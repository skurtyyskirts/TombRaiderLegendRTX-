# Build 085 — RETRACTED (was: "miracle — bind-pose VB cache stabilizes main-menu Lara hashes")

> **2026-05-19 retraction:** see `REGRESSION_NOTE.md`. Lara is invisible
> in regular rendering mode under this build — the apparent hash-color
> stability in debug view 277 was an artifact of Remix visualizing the
> cached bind-pose mesh while the rasterizer rendered nothing for Lara.
> The widened gate stripped the vertex shader the menu Lara needed for
> skinning. Reverted in build 086.

**Date:** 2026-05-19 10:38 CT
**Source commit base:** dd5cb61 (build 081)
**Driver:** `python patches/TombRaiderLegend/run_iter_wrapper.py test-hash --build --main-menu`
**Variant applied:** raise cache `nv` cap from 16384 to 65535
**Cumulative diffs from build 081:**
1. Widen Lara-class gate `posType==FLOAT3 && tcType==FLOAT4` → `posType==FLOAT3 && curDeclHasColor` (build 083).
2. Drop `bvi` and `mi` from `Lara_LookupCacheSlot` match key — `(nv, pc, tex0)` only (build 084).
3. Raise `useLaraCache` upper bound `nv <= 16384` → `nv <= 65535` (this build).

## Result

**PASS — main-menu Lara hashes stable across consecutive frames.**

| Criterion | Required | Observed |
|-----------|----------|----------|
| Hash colors identical on Lara's body across shot 1 + shot 2 | yes | **yes** |
| `LaraVB cache hits=N` with N>0 in ffp_proxy.log | yes | **N >= 10000** |
| No crash | yes | yes (clean exit) |

Lara appears in the same pose in both shots through the Remix debug view —
the cache replay locks her to the bind-pose snapshot taken on first sight.
The raster path still animates her (game window outside Remix); only the
geometry forwarded to `d3d9_remix.dll` is redirected, exactly as the
build-081 design intended.

## Screenshot Analysis

| Region | Shot 1 (10:38:16) | Shot 2 (10:38:19) | Stable? |
|--------|-------------------|-------------------|---------|
| Hair | teal/turquoise | teal/turquoise | **yes** |
| Headband | thin black line | thin black line | yes |
| Face | light green + red lips + green eyes | identical | **yes** |
| Shoulders | dark green + orange diamond accents | identical | **yes** |
| Upper torso | dark green with red/orange diamond pattern | identical | **yes** |
| Arms / arm bands | yellow + cream + orange stripes | identical | **yes** |
| Lower torso / waist | yellow + olive | identical | **yes** |
| Legs | cream + yellow + orange wraps | identical | **yes** |
| Background pyramid panels | unchanged | unchanged | yes |
| Title text overlay | unchanged | unchanged | yes |

Identical hash colorization across both shots → identical Remix asset hash
per submesh → stable replacement-asset anchor points are now reachable in
this scene.

## Proxy Log — Cache Telemetry

```
MOVABLE forced null_vs: first occurrence (Lara-class FLOAT3+FLOAT4tex → bind-pose to Remix)
LaraVB cache: first bind-pose snapshot committed
  nv=21845
  pc=...
  stride=24
LaraVB cache hits=100     misses=2  entries=2
LaraVB cache hits=1000    misses=2  entries=2
LaraVB cache hits=10000   misses=2  entries=2
```

- **2 entries** captured cover the menu's Lara-class FLOAT3+COLOR draws.
- **2 misses** total — the initial capture call for each of the two unique
  submesh signatures. Every subsequent draw hits.
- **10 000+ hits** before the 70 s log-wait window closed. Hit-rate is
  effectively 100 % post-warmup. Cache is doing real work, not gating out.

## What Changed This Build

`proxy/d3d9_device.c` — one numeric edit:

```diff
-                        && nv > 0 && nv <= 16384);
+                        && nv > 0 && nv <= 65535);
```

Patches mirror updated identically. Memory budget worst case: 64 slots ×
65535 verts × stride 24 ≈ 100 MB. Real cost at the menu: 2 slots ×
21845 × 24 ≈ 1 MB. Trivial.

## Why the Earlier Iterations Failed

- **Build 082 (iter1):** original gate `FLOAT3 + TEXCOORD0 FLOAT4` excluded
  the menu Lara decl (`FLOAT3 + COLOR + TEXCOORD0 FLOAT2`). Cache never
  fired.
- **Build 083 (iter2):** widened gate fired, but the only mesh that hit it
  had `nv=21845`, exceeding the 16384 upper-bound, so `useLaraCache` stayed
  false. Hashes still drifted.
- **Build 084 (iter3):** dropped `bvi/mi` from the lookup key. Same nv-cap
  block kept the cache path unreached, so the change was a no-op on the
  observable scene. The relaxed key is still kept here — it will pay off in
  any scene where the streaming allocator does vary the base index.

## Next Steps

1. Replay the existing in-level (Croft Manor / Peru) hash stability gate
   to confirm the widened gate + raised cap + relaxed key don't regress
   gameplay scenes. The variant set was tuned around the menu scene only.
2. Recapture stage-light anchor hashes against the new stable IDs and
   update `mod.usda`. The known-stale anchor hashes (CLAUDE.md WHITEBOARD)
   should now be replaceable with values from a fresh Remix capture taken
   on top of this build.
3. Consider trimming `vbFingerprintCount` from 40 to 8 once the menu
   diagnostic isn't needed every test, so the log scrolls past first-occ
   into hit telemetry sooner.

## Open Hypotheses for Regression Watch

1. The widened gate (FLOAT3 + COLOR) may grab additional menu decals or
   props in different scenes; if cache slot count exceeds 64 in-level,
   the `capacity reached` log will fire and the tail of submeshes will fall
   back to live VB. If that happens, bump `LARA_VB_CACHE_SIZE` (variant 5).
2. The `tex0` component of the relaxed key still excludes meshes whose
   bound texture varies — e.g., animated material atlases. If so, drop
   `tex0` from the key as well (variant 2 from the original plan).
