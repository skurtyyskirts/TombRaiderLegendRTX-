# Linear Integration — TombRaiderLegendRTX

This folder contains scripts that sync project state to Linear.

## Setup

1. Add `LINEAR_API_KEY` secret in GitHub repo Settings → Secrets and variables → Actions.
2. Add `LINEAR_ENABLED` variable (value: `true`) in the same location.
3. Run once to create the Linear project structure:
   ```bash
   export LINEAR_API_KEY="lin_api_xxxx"
   python linear/setup_linear.py
   ```
4. Commit the generated `linear/config.json`.

## Scripts

| Script | Purpose |
|---|---|
| `setup_linear.py` | One-time: creates team, projects, labels, milestones, pre-seeds issues |
| `sync.py` | Ongoing: reads CHANGELOG + WHITEBOARD → pushes builds/blockers/dead-ends to Linear |
| `parse_changelog.py` | Parses CHANGELOG.md into structured build records |

## Labels used

`proxy-code` `config` `static-analysis` `hash-stability` `culling` `sky-water` `upstream` `auto-idea` `dead-end` `blocker`

## Milestones

1. Infrastructure — proxy loads, game boots
2. Geometry Visible — meshes appear in Remix
3. Hash Stability — hashes stable across sessions
4. Visual Quality — sky, water, LOD correct
5. Stable Release
