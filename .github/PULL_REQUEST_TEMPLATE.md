## Summary

<!-- What does this PR change and why? -->

## Type

- [ ] Proxy DLL change (`proxy/` or `patches/TombRaiderLegend/proxy/`)
- [ ] Tooling (`retools/`, `livetools/`, `graphics/`, `autopatch/`, `automation/`)
- [ ] Documentation (`docs/`, `CHANGELOG.md`, `README.md`)
- [ ] Configuration (`rtx.conf`, CI workflow, `requirements.txt`)
- [ ] Other:

## Proxy Changes (if applicable)

| Patch address | Before | After | Effect |
|---------------|--------|-------|--------|
| `0x______` | | | |

## Test Results

<!-- Run `python patches/TombRaiderLegend/run.py test --build --randomize` and paste the result -->

- **Build number:** NNN
- **Result:** PASS / FAIL / CRASH
- **Draw calls/scene:**
- **Patch integrity:** all confirmed / issues:

## Checklist

- [ ] `patches/TombRaiderLegend/proxy/` is the source edited (not the root `proxy/` template)
- [ ] A timestamped backup was created in `patches/TombRaiderLegend/backups/` before editing
- [ ] A `TRL tests/build-NNN-<description>/` folder was committed with `SUMMARY.md` and screenshots
- [ ] `docs/status/WHITEBOARD.md` updated if a culling layer was added, removed, or changed status
- [ ] `CHANGELOG.md` updated with findings, patches, and dead ends from this session
- [ ] `user.conf` has `rtx.enableReplacementAssets=True` (verify before any mod content test)
- [ ] No game-specific data (function maps, address databases) committed to tracked core tooling

## Dead Ends / What Didn't Work

<!-- Save future contributors time — list approaches tried that failed and why -->

## References

<!-- WHITEBOARD.md section, CHANGELOG entry, related issues -->
