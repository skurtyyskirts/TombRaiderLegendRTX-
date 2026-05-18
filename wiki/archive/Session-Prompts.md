# Claude Code Session Prompts — Ready to Paste

## Session: TerrainDrawable Investigation
```
Read CLAUDE.md and CHANGELOG.md.

Decompile TerrainDrawable at 0x40ACF0 via GhidraMCP. Identify all parameters, what struct is passed in, and find any distance/frustum/visibility check. Trace all callers. Cross-reference with the 22-layer culling map. Document findings in docs/TERRAIN_ANALYSIS.md and update CHANGELOG.md.

Do not patch anything — analysis only.
```

## Session: dx9tracer Comparison
```
Read CLAUDE.md and CHANGELOG.md.

Use the dx9 tracer (graphics/directx/dx9/tracer/) to capture two full frames:
1. Standing next to the stage (near position)
2. Maximum distance from stage (far position)

Diff the two captures: which draw calls are present in near but absent in far? Do any of them match the anchor mesh hashes? Document in docs/TRACER_DIFF.md and update CHANGELOG.md.
```

## Session: Lara Mesh Hash Workaround
```
Read CLAUDE.md and CHANGELOG.md.

Find Lara's character mesh hash from hash debug screenshots in TRL tests/. Her body mesh is always drawn regardless of position. If we anchor stage lights to her mesh hash, they will always be visible. Identify the hash, test re-parenting in rtx.conf or Remix Toolkit, document results in CHANGELOG.md.
```