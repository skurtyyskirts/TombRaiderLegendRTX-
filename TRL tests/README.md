# TRL RTX Remix — Test Archive

This directory contains every test build from the project, committed in order. Each build has a `SUMMARY.md`, proxy source snapshot, and screenshots (where applicable).

**Live status docs:**
- [`docs/status/WHITEBOARD.md`](../docs/status/WHITEBOARD.md) — culling layer map (30 layers), build history narrative, decision tree, key addresses
- [`docs/status/TEST_STATUS.md`](../docs/status/TEST_STATUS.md) — build-by-build pass/fail table, what's done, what remains

---

## Current Status

**Failing — anchor geometry not submitted to renderer.** All known culling and visibility layers have been patched. All three light pipeline gates are re-enabled and confirmed safe (build 068). The anchor mesh geometry carrying Remix light hashes is not being submitted to `DrawIndexedPrimitive` at the camera positions tested.

| Goal | Status |
|------|--------|
| FFP proxy DLL chains to Remix | Done |
| Transform pipeline (View / Proj / World) | Done |
| Asset hash stability (static + moving camera) | Done |
| Automated two-phase test pipeline | Done |
| Backface / frustum / distance culling disabled | Done |
| Sector / portal visibility disabled | Done |
| Per-light culling gates disabled | Done |
| SHORT4 → FLOAT3 VB expansion + fingerprint cache | Done |
| FLOAT3 character draws (Lara visible) | Done |
| RenderQueue_FrustumCull (Layer 31) bypassed | Done |
| **Both stage lights stable at all positions** | **Failing** |

Last confirmed PASS: `build-019` (both lights visible, hashes stable).  
Latest build: `build-073` — `useVertexCapture=True` test; white dots visible (possible overexposed lights); fresh hash capture needed.

---

## Build Phases

### Pre-Archive — Early Development Session (2026-03-24)

> Exploratory captures before the formal build numbering was established. See [`session-000-early-dev/`](session-000-early-dev/).

---

### Phase 1 — Baseline & Hash Stability (Builds 001–002)

| Build | Result | Key Finding |
|-------|--------|-------------|
| [001](build-001-baseline-passthrough) | PASS | Hashes stable across frames and sessions with `indices,texcoords,geometrydescriptor` rule |
| [002](build-002-stable-hash) | PASS | Two-phase test confirmed; RTX path tracing works |

---

### Phase 2 — Anti-Culling Attempts (Builds 016–020)

> Builds 003–015 not preserved.

| Build | Result | Key Finding |
|-------|--------|-------------|
| [016](build-016-anti-culling-nop) | PASS* | 3-layer culling patches active; draw count 91.8K — movement was broken (false positive) |
| [017](build-017-fixed-culling-nops) | FAIL | First real movement test; lights disappear on D-strafe, hash colors shift |
| [018](build-018-scancode-fix-lights-partial-fail) | FAIL | Scancode flag fix — input now confirmed working; green vanishes after sustained strafe |
| [019](build-019-miracle-both-lights-stable-hashes) | PASS* | Both lights visible — later confirmed false positive (wrong screenshots evaluated) |
| [020](build-020-lights-partial-fail) | FAIL | Fixed screenshot selection; red light missing in 2/3 shots |

---

### Phase 3 — Light Culling Investigation (Builds 021–035)

| Build | Result | Key Finding |
|-------|--------|-------------|
| [021](build-021-false-positive-lara-didnt-move) | PASS* | False positive — Lara didn't move |
| [022](build-022-lara-left-stage-fail) | FAIL | Lara walked past stage area (D held too long) |
| [023](build-023-light-nop-partial-green-only) | FAIL | Light frustum NOPs in wrong source file — patches not compiled |
| [024](build-024-light-frustum-nop-shot1-pass-others-fail) | FAIL | Shot 1 both lights visible; shots 2–3 fail — zone hypothesis formed |
| [025](build-025-pending-flag-nop-same-result) | FAIL | Pending-render flag NOPs: no effect — bottleneck not in caller chain |
| [026](build-026-vis-state-nop-red-light-still-culled) | FAIL | LightVolume_UpdateVisibility NOPs: silent VirtualProtect failure |
| [027](build-027-lights-fade-at-distance-fail) | FAIL | Randomized movement confirms sector patch works; issue is light range |
| [028](build-028-sector-visibility-nop-geometry-up-lights-out-of-range) | FAIL | Sector visibility NOPed — 65× draw count increase; clean render dark |
| [029](build-029-ghidra-cull-globals-light-frustum-fail) | FAIL | All geometry culling defeated; light disappearance remains |
| [030](build-030-light-visibility-test-unpatched-fail) | FAIL | Root cause: `Light_VisibilityTest` at 0x60B050 unpatched |
| [031](build-031-light-vistest-patch-partial-pass) | FAIL | `Light_VisibilityTest` → always TRUE; lights at baseline but still disappear at distance |
| [032](build-032-light-config-flag-no-effect) | FAIL | Config flag stamp: no code xrefs, no effect |
| [033](build-033-pause-menu-macro-fail) | FAIL | Macro failure — pause menu blocked all screenshots |
| [035](build-035-sector-light-gate-nop-green-works) | FAIL | Green stable at all positions; red anchor meshes in zero-light sectors |

---

### Phase 4 — Geometry Submission Investigation (Builds 036–044)

> Build 034 not preserved. Build 043 crashed and not preserved.

| Build | Result | Key Finding |
|-------|--------|-------------|
| [036](build-036-green-light-culled-sector-boundary) | FAIL | Green culled at sector boundary |
| [037](build-037-render-lights-gate-nop-green-still-missing) | FAIL | RenderLights gate + sector count clear NOPed; green still missing |
| [038](build-038-fallback-light-diagnostic-both-lights-gone) | FAIL | **Root cause reframe**: neutral fallback confirms both lights gone at distance — problem is geometry, not light culling |
| [039](build-039-remove-ret-more-geometry-green-at-distance) | FAIL | Removed RET at 0x407150; draw counts 93K→180K; green at extreme distance only |
| [040](build-040-11-cull-nops-lights-still-culled) | FAIL | 11 cull NOPs inside SceneTraversal_CullAndSubmit; ~190K draws — culling not in this function |
| [041](build-041-far-clip-stamp-same-pattern) | FAIL | Far clip stamped to 1e30f; same pattern as 039 |
| [042](build-042-reparent-lights-wrong-mesh-fail) | FAIL | Lights re-parented to largest mesh — worse; mesh not always drawn |
| [044](build-044-sector-proximity-nop-same-pattern) | FAIL | Camera-sector proximity filter NOPed; all 3 render paths patched; terrain path identified as prime suspect |

---

### Phase 5 — Proxy Improvements + Deep Culling (Builds 045–068)

> Builds 048–063 not preserved.

| Build | Result | Key Finding |
|-------|--------|-------------|
| [045](build-045-managed-pool-vb-flush-FAIL-lights-missing) | FAIL | D3DPOOL_MANAGED VBs + per-frame flush; SHORT4 hash debug stable; blank amber render (flush too aggressive) |
| [046](build-046-null-vs-all-fingerprint-FAIL-rendering-broken) | FAIL | Content fingerprint cache introduced; nulling VS for FLOAT3 draws breaks view-space geometry |
| [047](build-047-strip-positions-hash-FAIL-hash-collision) | FAIL | Removing `positions` from hash rule causes catastrophic collision — positions are required |
| 048–063 | — | Not preserved |
| [064](build-064-hash-stability-FAIL-lights-missing) | FAIL | Hash debug invalid (load timing); Phase 1 timing bug identified (15s insufficient) |
| [065](build-065-hash-stability-FAIL-lights-missing) | FAIL | Hash stable; ~650 draws per frame; anchor meshes absent — fourth render path suspected |
| [066](build-066-theory1-draw-cache-disabled-FAIL-lights-missing) | FAIL | Theory 1: disabling draw cache has no effect |
| [067](build-067-theory2-vp-inverse-no-threshold-FAIL-lights-missing) | FAIL | Theory 2: removing VP inverse epsilon has no effect |
| [068](build-068-theory3-light-patches-reenabled-FAIL-lights-missing) | FAIL | Theory 3: light patches re-enabled — **no crash**; all 20+ patches confirmed; lights still absent |

---

### Phase 6 — Advanced Culling + Config Experiments (Builds 069–073)

| Build | Result | Key Finding |
|-------|--------|-------------|
| [069](build-069-hash-stability-FAIL-lights-missing) | FAIL | dipcnt hook failed; proxy log shows ~75% of draws culled despite patches |
| [070](build-070-hash-stability-FAIL-lights-missing) | FAIL | Draw counts collapsed 93% over session; proxy uses NOP-jump strategy (not RET) at 0x407150 |
| [071](build-071-hash-stability-FAIL-lights-missing) | FAIL | Expanded mod.usda to 8 anchor hashes; no lights; Lara not visible (FLOAT3 unpatched) |
| [071b](build-071b-FLOAT3-FFP-lara-visible-FAIL-lights-missing) | FAIL | **Lara now visible** — FLOAT3 draw path fixed (null VS before draw); lights still absent |
| [072](build-072-frustumcull-bypass-FAIL-lights-missing) | FAIL | RenderQueue_FrustumCull bypass (0x40C430→0x40C390); draw counts +29% (2845→3657); no lights |
| [073](build-073-vertexcapture-true-FAIL-lights-missing) | FAIL | `useVertexCapture=True`; white dots in screenshots — possibly overexposed stage lights (HDR clipping) |

\* False positive — Lara didn't move or wrong screenshots evaluated.

---

## Build Folder Structure

```
build-NNN-<description>/
├── SUMMARY.md                     # Result, What Changed, Proxy Log, Findings, Next Plan
├── phase1-hash-debug-posN.png     # Hash debug screenshots (geometry colored by hash)
├── phase2-clean-render-posN.png   # Clean render screenshots (RTX path traced)
├── ffp_proxy.log                  # Proxy diagnostics (draw counts, vpValid, patches)
└── proxy/                         # Proxy source snapshot at time of test
```

Naming: `build-NNN-miracle-*` for PASS, `build-NNN-*-fail` for FAIL.

---

## Pass Criteria

All five must be true:

1. Both the **red** and **green** stage lights visible in **all 3** clean render screenshots
2. Lights **shift position** left/right across the 3 screenshots as Lara strafes
3. Hash debug shows **same color for same geometry** across all positions
4. No crash, no proxy errors
5. Proxy log shows `vpValid=1`, patches confirmed, draw counts ≥ 91,800
