# Build 042 — Re-parent Lights to Largest Mesh (FAIL — Worse)

## Result

**FAIL** — All 3 clean shots show only fallback red. Moving lights to mesh_7DFF31ACB21B3988 made it worse — this mesh is NOT always drawn. Even the near-stage position that previously showed both lights now shows nothing.

## What Changed This Build

Modified `mod.usda` to parent all 3 lights to `mesh_7DFF31ACB21B3988` (the largest captured mesh, 88KB). This was based on the hypothesis that larger meshes = terrain = always drawn. The hypothesis was wrong — this mesh is apparently only drawn in specific sectors/conditions.

**Reverted immediately after test** — mod.usda restored to original 3-mesh anchor layout.

## Proxy Log Summary

Same as build-041. No proxy code changes.

## Open Hypotheses

1. The anchor mesh selection matters critically. The original 3 meshes work near stage because they exist in that sector's draw list. mesh_7DFF31ACB21B3988 apparently isn't in ANY of the test sectors.
2. Need to find a mesh that IS always drawn (like Lara's character mesh or the ground plane) to use as a universal anchor.
3. Alternatively, solve the engine-side problem by finding which function builds sector object lists.

## Next Build Plan

1. Wait for static-analyzer upstream caller analysis
2. Find Lara's character mesh hash by using the hash debug screenshots — her body should be a consistent hash across all positions
3. Try parenting lights to the ground mesh (the large tan/golden area in hash debug shots)
