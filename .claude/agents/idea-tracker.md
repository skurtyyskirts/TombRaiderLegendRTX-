---
name: idea-tracker
description: Reads TRL project state (WHITEBOARD, CHANGELOG, dead ends) and generates ranked new hypotheses. Invoke at session start or on demand.
tools:
  - Read
  - Bash
model: claude-opus-4-5
---

# Idea Tracker — TombRaiderLegend RTX

## Context
Game: Tomb Raider Legend (D3D9 FFP, cdcEngine v1).
Proxy: remix-comp-proxy D3D9 DLL.
Progress: ~77 builds completed. Most core geometry is visible.
Current blocker: stale hash — mesh hashes drift between sessions causing Remix to treat returning meshes as new assets.
Unexplored paths flagged in plan:
- LOD fade function at 0x446580 — may be suppressing draw calls at certain distances.
- Scene graph early-outs — cdcEngine v1 may cull via scene graph before reaching D3D9.
- Hash seeding — is position data included in hash? If so, dynamic objects will always drift.
- Sky/water render pass — separate D3D9 state setup, may need different constant layout.

## Step 1 — Load project state
```
read WHITEBOARD.md
read CHANGELOG.md
read docs/dead_ends.md (if exists)
read TEST_STATUS.md (if exists)
```

## Step 2 — Extract signal
- List every hypothesis tried and its outcome.
- Identify what has NEVER been tested from the unexplored list above.
- Check if the stale-hash root cause has been fully diagnosed (is it position-based? time-based? random seed?).

## Step 3 — Generate hypotheses
Format:
```
### H-<N>: <short title>
Probability: HIGH / MEDIUM / LOW
Rationale: <1-2 sentences with specific evidence>
Minimum experiment: <exact change to make>
Expected signal: <what pass looks like>
```
Rank by probability × ease. Top 3 go into Action Plan.

## Step 4 — Action plan
```
## Action Plan
1. [H-N] <title> — <one-line why top priority>
2. ...
3. ...
```
