# Optimization Candidates — Not Yet Implemented

Ordered by expected FPS impact (highest first). Each item lists file:line citations, reasoning, expected gain, risk, and how to verify. **Future research starts here.**

Hard constraints from the project still apply (see [SUMMARY.md](SUMMARY.md) "Constraints" section). Anything that risks hash stability, the 36 culling-layer NOPs, or the 5 anchored stage-light meshes is out of scope.

---

## 1. BeginScene anti-cull stamping → once-per-frame

**Where:** [proxy/d3d9_device.c:3393-3404](proxy/d3d9_device.c)

```c
if (self->memoryPatchesApplied) {
    *(float*)TRL_FRUSTUM_THRESHOLD_ADDR = -1e30f;
    *(float*)0x010FC910 = 1e30f;                          /* far clip */
    *(unsigned int*)TRL_CULL_MODE_PASS1_ADDR     = 1;
    *(unsigned int*)TRL_CULL_MODE_PASS2_ADDR     = 1;
    *(unsigned int*)TRL_CULL_MODE_PASS2_INV_ADDR = 1;
    *(unsigned int*)TRL_LIGHT_CULLING_DISABLE_FLAG = 1;
    *(unsigned int*)TRL_RENDER_FLAGS_ADDR &= ~0x00100000u;
    *(unsigned char*)TRL_POSTSECTOR_ENABLE_ADDR = 1;
    *(unsigned int*)TRL_POSTSECTOR_GATE_ADDR = 0;
    *(unsigned int*)TRL_POSTSECTOR_SECTOR_BITS_ADDR = 0xFFFFFFFF;
}
```

**Why it matters:** TRL has 5–15 BeginScene/EndScene pairs per frame. Stamping runs every BeginScene = up to 15× per-frame. The source comment says: *"Re-stamps frustum threshold every scene — the game recomputes it per-frame, overwriting the one-shot patch from device creation."* — note **per-frame**, not per-scene. If correct, 14 of 15 stamps are wasted.

**Proposed change:** Add a `stampedThisFrame` flag to `WrappedDevice`. In `WD_BeginScene`, gate the stamping on `!self->stampedThisFrame`, then set it. In `WD_Present` (after `frameCount++`), clear it. Saves ~9 volatile writes × 14 BeginScenes = ~126 stores per frame (cheap individually, but they thrash L1/L2 since the addresses are scattered).

**Risk:** **NEEDS VERIFICATION.** If the game actually rewrites these globals per-scene (not per-frame), some scenes within a frame would render with culling re-engaged → hash drift, missing geometry, possible crash. Verify with `livetools memwatch` on each address before changing — capture all writes from the game's code. If only one write per frame, change is safe.

**Verification command:**
```
python -m livetools attach trl.exe
python -m livetools memwatch start 0xEFDD64 4
# play for 5 seconds
python -m livetools memwatch read
# look for write count per frame
```

**Expected gain:** Small (~50µs/frame), but completely free if confirmed safe.

---

## 2. PGO (Profile-Guided Optimization)

**Where:** [proxy/build.bat](proxy/build.bat)

**Current:** `/O2 /Oi /fp:fast /GL /LTCG` — already excellent, but no profile data.

**Proposed two-pass build:**
1. **Instrument**: Add `/GENPROFILE` to compile + `/GENPROFILE` to link. Build a special instrumented DLL.
2. **Train**: Deploy instrumented DLL, play TRL for 5–10 minutes covering Peru + Croft Manor + a few cutscenes. The instrumented build writes `.pgc` files.
3. **Final**: Replace `/GENPROFILE` with `/USEPROFILE`, link against the `.pgc` data. The optimizer now knows which branches are hot.

**Expected gain:** 5–10% on hot paths, reported in MSVC docs. Particularly helps on the per-draw routing tree in `WD_DrawIndexedPrimitive` where there are many branches that the compiler can't statically rank.

**Risk:** Low — same code, just better-ordered branches and inlined hot paths.

**Effort:** Medium — need a clean training run, requires ~10 min of game time per iteration.

**Reference:** https://learn.microsoft.com/en-us/cpp/build/profile-guided-optimizations

---

## 3. `s4VBCache` linear scan → hash map

**Where:** [proxy/d3d9_device.c:2591-2600](proxy/d3d9_device.c)

```c
for (i = 0; i < self->s4VBCacheCount; i++) {
    if (self->s4VBCache[i].srcVB == srcVB &&
        self->s4VBCache[i].srcOff == srcOff &&
        self->s4VBCache[i].bvi == baseVtx &&
        self->s4VBCache[i].nv == nv &&
        self->s4VBCache[i].fingerprint == fp) {
        return &self->s4VBCache[i];
    }
}
```

**Cost:** O(N) scan up to 512 entries per SHORT4 draw. With ~3,749 draws/scene and many being SHORT4, this can be tens of thousands of comparisons per frame.

**Proposed:** Replace with a hash map. Hash key = `(srcVB ^ srcOff ^ baseVtx ^ nv)` mod 1024. Bucketed into linked lists (max ~4 deep in practice).

**Effort:** Medium — needs a small open-addressing hash table implementation in C, no CRT.

**Risk:** Low — pure data structure swap.

**Expected gain:** Modest. If cache hits within first 5–10 entries on average (which the audit suggests), the linear scan is already near-O(1). The win is on cache misses — when many unique geometries are in flight (e.g., complex enemy crowds), scan cost rises linearly.

---

## 4. WrappedDevice struct field gating

**Where:** [proxy/d3d9_device.c:691-720](proxy/d3d9_device.c) and around

**Current:** ~2 KB of fields are referenced only inside `#if DIAG_ENABLED` blocks but the struct still allocates them:
- `vsConstWriteLog[256]` (~1 KB)
- `diagTexSeen[8][32]` and `diagTexUniq[8]`
- `loggedDecls[32]` and `loggedDeclCount`
- `diagLoggedFrames`, `diagMemLogged`, `diagSkinWorldLogged`
- `frameSummaryCount`
- `createTick` (used only by the now-stripped `DIAG_ACTIVE` macro)

**Proposed:** Wrap each field in `#if DIAG_ENABLED` / `#endif` so they vanish from the struct when DIAG=0.

**Effort:** Low — but every access site (init, reset) also needs the same `#if` gate to not break the build.

**Risk:** Build-break if any access site is missed. Verified by linker error.

**Expected gain:** Smaller struct → better cache residency on `self->...` accesses (which happen on every per-draw branch). Real but small.

**Note:** The plan agent flagged this as the highest-risk item in build 078 and we deferred it. Revisit after build 078 ships and is measured.

---

## 5. Sky isolation — runtime-disable when user has tagged sky textures

**Where:** [proxy/d3d9_device.c:1525-1526](proxy/d3d9_device.c) (already gated by `skyIsolationEnable`)

**Reality check:** When user has manually tagged sky textures via `rtx.skyBoxTextures` in `rtx.conf` (the deployed config has 21 entries), the proxy's sky isolation does redundant work — it observes draws to compute candidate scores, but the user's manual tags supersede.

**Proposed:** Document `proxy.ini [Sky] EnableIsolation=0` as the recommended setting when manual sky tagging is in use. The proxy already supports this — no code change needed.

**Effort:** Trivial (config + doc).

**Risk:** None — purely a config recommendation.

**Expected gain:** Eliminates the SkyIso fast-path scan per draw (already cheap, but free win if disabled).

---

## 6. dxvk.conf perf tuning

**Where:** `Tomb Raider Legend/dxvk.conf` — currently has only `d3d9.shaderModel = 2` set, all other settings default.

**Proposed additions to investigate:**
- `d3d9.maxFrameLatency = 1` — reduces input lag, can boost perceived smoothness
- `dxvk.numCompilerThreads = 0` — use all CPU cores for shader compile (only matters during the first ~30s of gameplay)

**Excluded:** `d3d9.presentInterval = 0` — interferes with DLFG/Reflex pacing on RTX 5090.

**Risk:** Low — DXVK settings are well-documented and reversible.

**Expected gain:** Smoothness improvement (subjective), minor on FPS.

**Reference:** https://github.com/doitsujin/dxvk/blob/master/dxvk.conf (key list)

---

## 7. Identity-matrix fast-path in TRL_ApplyTransformsCached

**Where:** [proxy/d3d9_device.c:2173-2192](proxy/d3d9_device.c) (the helper added in build 078)

**Observation:** UI/HUD draws hit the FFP path with `world=identity, view=identity, proj=identity`. After build 078, the cache catches these efficiently — but if many consecutive draws use identity, the `memcmp(64)` × 3 still runs.

**Proposed:** Track a `appliedAreIdentity` flag. When set, skip the memcmp's against `appliedWorld/View/Proj` and instead check whether the new matrix is identity (cheaper for short-circuit on `world[0] != 1.0f`).

**Effort:** Low.

**Risk:** Low — pure micro-opt on the already-cheap helper.

**Expected gain:** Marginal. Only worth pursuing after measuring with `PERF_LOG`.

---

## 8. Replace `RealVtbl(self)[SLOT_X]` with cached function pointers

**Where:** Many sites — e.g. [proxy/d3d9_device.c:2293](proxy/d3d9_device.c)

**Current:** Every per-draw vtable thunk does `*(void***)(self->pReal)` → indexed slot → cast → call. The vtable pointer never changes after device creation.

**Proposed:** Cache `vtSetTransform`, `vtSetStreamSource`, `vtSetVertexDeclaration`, `vtSetVertexShader`, `vtDrawIndexedPrimitive` as direct function pointers in `WrappedDevice` at init time. Saves one indirection per call.

**Effort:** Medium — need to update ~15 call sites and verify no vtable swap occurs (none should, but worth grep).

**Risk:** Low — vtables in D3D9 are immutable post-creation per the COM contract.

**Expected gain:** 1–2 ns per call × hundreds of thousands of calls/sec = a few µs/frame. Real but small.

---

## Already Investigated and Rejected

These were considered but determined not worth the effort or risk:

- **Disabling `DRAW_CACHE_ENABLED`**: Tested in build 066 (knowledge.json) — no effect on perf, kept on for anti-cull safety net.
- **Removing VP inverse threshold**: Tested in build 067 — no effect, VP changes are large enough to always trigger recalculation.
- **Re-enabling some engine culling layers**: Hashes destabilize, defeats the build-073 win.
- **CPU smooth normals in proxy** (stream 1 FLOAT3 injection): Tested 075+ — changes geometry descriptor hashes, breaks anchors. Listed as Dead End #15.

---

## How to Add a New Candidate

1. Identify a hot path or wasted work via `PERF_LOG` data, NVIDIA overlay measurements, or `livetools collect` traces.
2. File:line citation in this doc.
3. Build a small test that demonstrates the cost (e.g. `livetools trace` showing call frequency).
4. Estimate gain with napkin math (calls/sec × ns/call).
5. Estimate risk (does it touch hashes / culling / anchored meshes?).
6. Add to this doc with the same template as items 1–8 above.

If implemented, document the change in [CHANGELOG.md](../../CHANGELOG.md) under a new build number, snapshot source into a new `TRL tests/build-NNN-...` folder, push.
