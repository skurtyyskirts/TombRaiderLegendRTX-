# RETRACTION — build 085 was a false PASS

**Reported by user, 2026-05-19 ~11:00 CT (testing build 085 deployed bundle).**

The original `SUMMARY.md` in this folder claims "miracle — main-menu Lara
hashes stable across consecutive frames". That conclusion was wrong. Manual
in-game verification (debug view 0, normal rendering mode) showed Lara is
**invisible** at the main menu under the build 083+ widened gate.

## What actually happened

The cumulative diff applied through builds 083 → 085 routed the menu Lara
decl A (`POSITION FLOAT3 + COLOR + TEXCOORD0 FLOAT2`) onto the null-VS FFP
path. TRL's menu Lara is animated by her vertex shader — not by per-bone
FFP transforms — so stripping the VS produces degenerate geometry and she
drops off-screen / scales to zero.

Debug view 277 (`rtx.debugView.debugViewIdx = 277`) visualizes every mesh
Remix sees, regardless of whether the regular rasterizer renders it. The
cached bind-pose VB was still being submitted to Remix (with stable bytes,
so stable hash colors) — but the geometry never reached the user's screen
in normal rendering mode. The visible hash-color stability across the two
PASS screenshots in this folder was therefore a debug-view artifact, not
end-to-end correctness.

## What's kept, what's reverted

Reverted in build 086:
- `TRL_ForceSkinnedNullVS` gate restored to `posType==FLOAT3 &&
  tcType==FLOAT4`. Menu Lara stays on the shader route; cache is inactive
  at the menu (as the build 081 author intended).

Retained from builds 083/084:
- `Lara_LookupCacheSlot` keys on `(nv, pc, tex0)` only (build 084 change).
  Inert with the restored restrictive gate, but ready if gameplay-scene
  hash drift later proves it's needed.
- `useLaraCache` upper bound `nv <= 65535` (this build's change). Same
  rationale — inert in scenes where the gate doesn't fire, ready when it
  does.

The patches under `patches/TombRaiderLegend/proxy/` are now back to the
build-081 design semantics for the menu, with the relaxations queued for
in-level scenes that match Decl C.

## Test-methodology lesson

Hash-color stability in debug view 277 is necessary but not sufficient.
Any future PASS gate must additionally verify the mesh is visible in
debug view 0 (normal rendering). The `do_main_menu_hash_capture` driver
should be extended to capture both views before declaring PASS — same
pattern the in-level `do_test_hash_stability` already uses (its Phase 1 is
view 277 + Phase 2 is view 0).
