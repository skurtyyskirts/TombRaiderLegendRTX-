---
description: Canonical game directory and deploy rule — every build, edit, and change must be deployed here before every launch so testing always exercises the latest artifacts
---

# Game Deploy Target

## The Rule

**Every change, edit, or build of any artifact that the game loads at runtime MUST be deployed to the canonical game directory before every game launch.** Testing must always exercise the latest build — never a stale copy.

## Canonical Game Directory

```
C:\Users\skurtyy\Documents\GitHub\AlmightyBackups\NightRaven1\Vibe-Reverse-Engineering-Claude\Tomb Raider Legend
```

This is the single source of truth for the active game install. Confirmed by `ffp_proxy.log` mtime and the path baked into `run.py`, `launcher.py`, `deploy_build.py`, and every test harness in the repo.

Set `TRL_GAME_DIR` to this path before running any test script.

## What Must Be Deployed

On every launch, after any modification, copy the latest version of all of the following from the repo to the game directory:

| Artifact | Repo source | Game-dir destination |
|----------|-------------|----------------------|
| FFP proxy DLL | `proxy/build/d3d9.dll` | `<game>\d3d9.dll` |
| Proxy INI | `proxy/proxy.ini` (or `patches/TombRaiderLegend/proxy.ini`) | `<game>\proxy.ini` |
| Remix DLL chain | `<repo>\d3d9_remix.dll`, `dxwrapper.dll`, `NvRemixLauncher32.exe` | `<game>\` (same names) |
| `rtx.conf` | `rtx.conf` or `patches/TombRaiderLegend/rtx.conf` | `<game>\rtx.conf` |
| `mod.usda` and replacement assets | `rtx-remix/mods/...` | `<game>\rtx-remix\mods\...` |
| `user.conf` overrides | repo copy if maintained | `<game>\user.conf` (must NOT contain `rtx.enableReplacementAssets=False` — see build 075) |
| Any patched binaries | wherever modified | corresponding game-dir path |

If a file exists in the repo and the game reads it at runtime, it gets deployed. No exceptions.

## How to Apply

- All test scripts (`run.py test`, `run.py test-hash`, `python -m autopatch`, `launcher.py`) already deploy before launch — DO NOT bypass them by launching the game manually with stale files.
- When building manually outside the test harness, run `python patches/TombRaiderLegend/deploy_build.py` (or the equivalent copy step) before any launch.
- When editing the proxy source, the proxy INI, `rtx.conf`, or `mod.usda` in the repo, the next launch MUST deploy that change. If you launch the game and your edit isn't reflected, the deploy step was skipped — STOP and re-deploy. Do not "test anyway" against the old copy.
- A failed deploy (file in use, permission denied, antivirus quarantine) is a launch-blocker. Fix the deploy first; do not proceed to launch with mixed-version artifacts.
- Mixed-version artifacts (new DLL with old INI, new `rtx.conf` with old `mod.usda`, etc.) invalidate the test. If in doubt, re-deploy everything in the table above.

## Why

- Builds 016–074 silently ran with a stale `user.conf` containing `rtx.enableReplacementAssets=False`, invalidating every mod content test for 58 consecutive builds (see CLAUDE.md "Dead Ends" #14, build 075). The lesson: any drift between repo state and game-dir state corrupts the test result without obvious symptoms.
- Test archives in `TRL tests/build-NNN-.../` are only trustworthy if the game-dir state at launch matches the snapshot in that folder. Always-deploy enforces that match.
- The proxy DLL, `rtx.conf`, and `mod.usda` are tightly coupled — partial deploys produce confusing failure modes (hash mismatches, missing draws, replacement asset no-ops) that look like new bugs but are actually deploy-skew artifacts.

## Verification

After deploy, before claiming a test result:

1. `ffp_proxy.log` in the game directory should have an mtime newer than the proxy build timestamp
2. The proxy's build banner (logged on `BeginScene` first frame) should match the version you just built
3. `mod.usda` and any replacement assets should match the repo copy byte-for-byte if they were modified

If any of those checks fail, the deploy was incomplete — re-deploy, re-launch.
