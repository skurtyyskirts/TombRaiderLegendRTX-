# Agent Fleet Index (TombRaiderLegendRTX)

Subagents under `.claude/agents/` are auto-discovered by Claude Code. Use `/agents` interactively to manage them.

## New in this PR

| Agent | Purpose | Model |
|-------|---------|-------|
| `agent-router` | Recommends which subagent fits a task | haiku |
| `pr-auto-merge-reviewer` | Decides if a PR is safe to auto-merge once green | sonnet |
| `dependency-auditor` | Vets Dependabot / manual dep bumps for breaking changes | sonnet |
| `repo-housekeeper` | Weekly hygiene punch-list (stale branches, large files) | sonnet |
| `feature-prioritizer` | Ranks open ideas/issues by impact-vs-cost | sonnet |

## Workflows added

| Workflow | Trigger | Effect |
|----------|---------|--------|
| `auto-merge-on-green.yml` | PR labelled `automerge` | GitHub native squash auto-merge after CI passes |
| `dependabot-auto-approve.yml` | PR from `dependabot[bot]` | Auto-approves & labels patch/minor bumps |
| `scheduled-housekeeping.yml` | Cron Mondays 04:17 UTC + manual | Opens housekeeping issue |
| `agent-fleet-validator.yml` | PR on `.claude/agents/**` | Lints frontmatter; skips legacy plain-markdown agents |

## Enabling auto-merge (one-time)

1. Settings → General → Pull Requests → **Allow auto-merge** ✓
2. Settings → Branches → protect `main` → require at least one passing check
3. `gh label create automerge -c "#0e8a16" -d "Eligible for native auto-merge"`
4. `gh label create housekeeping -c "#fbca04" -d "Repo hygiene report"`
5. Apply `automerge` to any PR and let CI go green.

## Terminal recipe for building more subagents

```bash
claude              # start interactive session
> /agents           # opens the subagent manager UI
> Create new agent  # follow prompts (name, description, tools, model)
```

That writes a markdown file to `.claude/agents/<name>.md`. Commit it; the agent-fleet-validator workflow lints it on PR.
