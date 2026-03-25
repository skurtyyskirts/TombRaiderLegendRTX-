# Build 002 — Stable Hash Test

**Date:** 2026-03-24
**Result:** PASS

## Configuration
- Asset hash rule: `indices,texcoords,geometrydescriptor`
- Debug view: 277 (Geometry/Asset Hash)
- RTX Remix: remix-main+08e8f1b7
- GPU: NVIDIA GeForce RTX 5090 (driver 595.79.0)
- Resolution: 1024x768 fullscreen

## Two-Phase Test
- **Phase 1 (hash debug):** 4 screenshots captured at debug view 277
- **Phase 2 (clean render):** 4 screenshots captured with debug view off

## Findings
- Asset hashes are **stable frame-to-frame** at the same camera position (screenshots 1 & 2 are identical colors)
- Asset hashes are **stable across camera movement** (screenshot 3 shows same colors on same geometry from different angle)
- Asset hashes are **stable on new geometry** entering view (screenshot 4)
- Clean render shows proper RTX path tracing with no visual glitches
- No crashes during either phase
- Generation hash still flickers (expected — includes positions which change with camera)

## Screenshots
| File | Phase | Description |
|------|-------|-------------|
| 01-hash-view-1.png | Hash | Standing position, debug view 277 |
| 02-hash-view-2.png | Hash | Same position ~5s later — colors identical |
| 03-hash-view-3.png | Hash | Slight camera movement — same geometry keeps same color |
| 04-hash-view-4.png | Hash | Different area — new geometry with stable hashes |
| 05-clean-view-1.png | Clean | RTX path-traced render, same scene |
| 06-clean-view-2.png | Clean | Same position ~5s later |
| 07-clean-view-3.png | Clean | Camera movement |
| 08-clean-view-4.png | Clean | Different area |
