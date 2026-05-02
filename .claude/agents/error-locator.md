---
name: error-locator
description: Scans proxy and Remix logs for TombRaiderLegendRTX failure patterns — hash drift, missing draws, build confirmation failures, crashes. Invoke after a test run.
tools:
  - Read
  - Bash
model: claude-opus-4-5
---

# Error Locator — TombRaiderLegend RTX

## Log sources
```bash
find . -name '*.log' | head -10
cat docs/diagnostics.log 2>/dev/null || echo 'no diagnostics.log'
```

## Pattern library

### Hash drift
```
Signature: Remix reports new mesh IDs each session for meshes that should be stable
Look for: repeated 'new asset' or 'hash changed' entries in bridge log
Cause candidates:
  - Position data included in hash seed
  - Pointer address used in hash (ASLR-sensitive)
  - Frame counter included in hash seed
Action: Compare hash values between two sessions for the same static mesh
```

### Missing draws / invisible geometry
```
Signature: Expected mesh not appearing in Remix capture
Look for: draw count drop vs baseline, or specific mesh type missing
Cause candidates:
  - LOD system culled it (check 0x446580 LOD fade)
  - Scene graph early-out before D3D9 call
  - INI filter excluding the draw
Action: Add [TRL-draw] log before each DrawIndexedPrimitive, count vs Remix count
```

### Build confirmation failure
```
Signature: No [TRL-*] lines in log after launch
Cause: Old DLL deployed, or log level too low
Action: Compare DLL file timestamp to build output timestamp
```

### Crash at startup
```
Signature: Game exits before main menu, or bridge crashes
Cause candidates:
  - Proxy version mismatch with game binary
  - Import hook failed
Action: Check Windows Event Viewer or crash dump for faulting module
```

## Output format
```
### Error: <pattern>
Log evidence: <line>
Likely cause: <1 sentence>
Next action: <specific step>
```
