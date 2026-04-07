# Build 067 — Theory 2: Remove VP Inverse Cache Threshold

## Result

**FAIL-lights-missing**

## What Changed This Build

- **Removed VP inverse epsilon threshold** — the `mat4_changed()` function previously used a 1e-4 tolerance to skip VP inverse recalculation when the matrix changed by less than that amount. The threshold was removed so VP inverse is always recalculated.

## Hypothesis

The 1e-4 epsilon in `mat4_changed()` means small camera movements don't trigger VP inverse recalculation, leaving stale world matrices that cause hash drift. Removing the threshold ensures world matrices always reflect the current camera position.

## Result

No change. Hash debug identical to baseline. Clean render still shows no stage lights.

## Conclusion

The VP inverse cache threshold is not contributing to hash instability. VP changes on camera pan are large enough to always exceed the threshold — recalculation was already happening every frame in practice.

## Proxy Log Summary

All 20+ patches confirmed active. No crashes.

## Open Hypotheses

- Root cause is upstream of all current patches — anchor geometry never submitted at tested positions
- Three theories tested together (builds 066-068); see build-068 SUMMARY for full analysis

## Next Steps

See build-068 (Theory 3 — re-enable all light patches).
