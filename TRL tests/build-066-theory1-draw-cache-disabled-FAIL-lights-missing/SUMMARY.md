# Build 066 — Theory 1: Disable Draw Cache

## Result

**FAIL-lights-missing**

## What Changed This Build

- **`DRAW_CACHE_ENABLED 0`** — disabled the 4096-entry draw cache that replays previously submitted draws.

## Hypothesis

The draw cache replays draws with stale COM resource pointers (VB/IB/texture freed by game between frames), causing textures to disappear or incorrect geometry to render. Disabling it forces every draw to come from the live game submission path.

## Result

No change. Same brown/tan buildings, no stage lights. Hash debug stable.

## Conclusion

The draw cache is not causing texture absence or geometry disappearance. The cache only replays 3 draws and the stale pointer concern was unfounded — the game does not free these resources between frames.

## Proxy Log Summary

All 20+ patches confirmed active. No crashes.

## Open Hypotheses

- Root cause is upstream of all current patches — geometry never submitted at tested positions
- Three theories tested together (builds 066-068); see build-068 SUMMARY for full analysis

## Next Steps

See build-067 (Theory 2) and build-068 (Theory 3).
