# Agent Fleet Index

Subagents under `.claude/agents/` are picked up automatically by the Claude Code harness when you run `claude` from this repo, or manage them interactively via `/agents`.

## New in this PR

| Agent | Purpose | Model |
|-------|---------|-------|
| `agent-router` | Recommends which subagent fits a given task | haiku |
| `pr-auto-merge-reviewer` | Decides if a PR is safe to auto-merge once green | sonnet |
| `dependency-auditor` | Vets Dependabot / manual dep bumps for breaking changes | sonnet |
| `repo-housekeeper` | Weekly hygiene punch-list (stale branches, large files) | sonnet |
| `feature-prioritizer` | Ranks open ideas/issues by impact-vs-cost | sonnet |

## Workflows added

| Workflow | Trigger | Effect |
|----------|---------|--------|
| `auto-merge-on-green.yml` | PR labelled `automerge` | Enables GitHub native squash auto-merge after CI passes |
| `dependabot-auto-approve.yml` | PR from `dependabot[bot]` | Auto-approves patch/minor bumps and labels them `automerge` |
| `scheduled-housekeeping.yml` | Cron Mondays 04:17 UTC + manual | Opens an issue with stale-branch / large-file / artifact scan |
| `agent-fleet-validator.yml` | PR touching `.claude/agents/**` | Lints YAML frontmatter on every agent file |

## Enabling auto-merge in this repo (one-time)

1. Settings → General → Pull Requests → **Allow auto-merge** ✓
2. Settings → Branches → protect `main` → require at least one passing check
3. Create the label: `gh label create automerge -c "#0e8a16" -d "Eligible for native auto-merge"`
4. Create the label: `gh label create housekeeping -c "#fbca04" -d "Repo hygiene report"`
5. Apply `automerge` to any PR and let CI go green.

## Terminal recipe for building more subagents

```bash
claude              # start interactive session in this repo
> /agents           # opens the built-in subagent manager UI
> Create new agent  # follow prompts (name, description, tools, model)
```

That writes a markdown file to `.claude/agents/<name>.md` with YAML frontmatter. Commit it. The `agent-fleet-validator` workflow lints it on PR.

For non-interactive: drop a file at `.claude/agents/<name>.md` matching the schema in any existing agent here.
