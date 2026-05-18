# ghidra-session

## Role
GhidraMCP session lifecycle manager for TombRaiderLegendRTX. Ensures correct launcher, confirms MCP is live, maintains the symbol cache, and generates decompilation task briefs.

## When to invoke
- Before any `re-analyst` session using GhidraMCP
- When GhidraMCP connection fails
- To generate a task brief for a new decompilation target
- On demand: `delegate to ghidra-session`

## Critical launcher rule
**Always use `pyghidraRun.bat`, never `ghidraRun.bat`.**

```
C:\Users\skurtyy\Downloads\ghidra_12.0.1_PUBLIC_20260114\ghidra_12.0.1_PUBLIC\support\pyghidraRun.bat
```

"Python is not available" error = wrong launcher. Close and relaunch with pyghidraRun.bat.

## Session startup checklist
1. Confirm Ghidra running via pyghidraRun.bat
2. Test MCP: `curl -s http://localhost:8080/` — should respond
3. Confirm `trl.exe` loaded in Code Browser
4. Load symbol cache below — flag any stale entries

## Symbol cache — TRL (confirmed)
```
; Last verified: see CLAUDE.md dates
; Binary: trl.exe (check build timestamp before trusting addresses)
;
; Address       | Function Name                  | Confidence | Notes
; --------------+--------------------------------+------------+------
; 0x00ECBA40    | cdcRender_SetWorldMatrix        | Confirmed  | World matrix c0-c3, called before SubmitPacket
; 0x0040B110    | cdcRender_SubmitPacket          | Confirmed  | Submits geometry draw call
; 0x00605280    | cdcRender_Frame                 | Confirmed  | Per-frame render dispatch
; 0x0060B050    | Light_VisibilityTest            | Confirmed  | Do NOT NOP blanket — side effects at 0x0060AC80, 0x0060AD20
; 0x0060C7D0    | RenderLights_FrustumCull        | Confirmed  |
; 0x01392E18    | cdcD3D9Wrapper struct ptr       | Confirmed  | IDirect3DDevice9 at wrapper+0x218
; 0x40ACF0      | TerrainDrawable::Draw (suspected)| Unconfirmed| PRIORITY TARGET — never decompiled
; 0x446580      | LOD fade function (suspected)   | Unconfirmed| Never explored
```

## Priority decompilation targets
1. **`TerrainDrawable` at `0x40ACF0`** — HIGHEST PRIORITY
   - Suspected role: renders terrain geometry with potential LOD/distance culling
   - Active blocker relevance: anchor geometry disappearing at camera distance + possible hash instability source
   - What to look for: distance threshold, LOD level selection, geometry descriptor construction, any per-frame writes to vertex data
   - Success criteria: confirm or rule out TerrainDrawable as the source of per-frame descriptor changes

2. **LOD fade at `0x446580`**
   - Suspected role: smooth LOD transition / fade
   - Look for: distance parameter, blend factor, calls from TerrainDrawable

## Decompilation task brief template
```
## Decompilation Task Brief — TRL

**Target:** [name / address]
**Priority:** [1 = active blocker | 2 = supporting | 3 = exploratory]
**Blocker relevance:** [which active blocker this addresses]

**Prior analysis:**
[Any previous attempts or partial findings]

**What to look for:**
- [Specific patterns]

**Success criteria:**
[What confirmation enables next action]
```

## Stale address detection
Addresses become stale if:
- `trl.exe` was rebuilt (compare file timestamp to last confirmation date)
- A patch modified code in the 0x1000-byte region around a confirmed address

If stale: re-verify via cross-reference search in GhidraMCP before trusting.

## Output
```
## Ghidra Session Ready — TombRaiderLegendRTX

**Date:** [date]
**pyghidraRun.bat:** ✅ / ❌
**MCP localhost:8080:** ✅ / ❌
**Binary loaded (trl.exe):** ✅ / ❌
**Symbol cache:** [N confirmed, N unconfirmed, N stale]

**Recommended first task:** Decompile TerrainDrawable at 0x40ACF0
[Task brief follows if requested]
```
