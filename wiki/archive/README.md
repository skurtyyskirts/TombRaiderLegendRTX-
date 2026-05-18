# Archive

> Historical documents preserved for archaeology. **All authoritative content has been moved to active wiki pages.** If anything in here conflicts with an active wiki page, the active page is correct.

Most of the wiki's content lives in the `wiki/` root. This subdirectory holds documents that were superseded by later work but are kept for historical reference — early session handoffs, the original Compass AI analysis, prompts that were later replaced by skills, and the giant `Combined-Research-Docs.md` concatenation from the first research push.

## Contents

| File | Notes | Status |
|------|-------|--------|
| `Combined-Research-Docs.md` | 252,000-word concatenation of all early research notes | Superseded by structured wiki pages |
| `Workspace-Analysis.md` | 2026-03-19 initial workspace technical analysis | Superseded by [[../FFP-Proxy-Pipeline]] + [[../Tools-Architecture-Overview]] |
| `Compass-Analysis.md` | Output from Compass AI assistant — high-level architecture | Superseded by [[../Deep-Analysis-Report]] |
| `Compass-Artifact-Shader-D3D9-Compatibility.md` | Compass research artifact on TRL shader-based D3D9 compatibility | Superseded |
| `Compass-Artifact-D3D9-Accuracy-Audit.md` | Compass audit of TRL research docs — identifies 3 errors stemming from VS/PS shader prefix confusion | Useful for cautionary lessons; superseded for current state |
| `Original-Session-Handoff.md` | First formal session handoff document — game stack, attempted approaches, what worked, what failed, dead ends | Superseded by [[../Build-History-Index]] + [[../Dead-Ends]] |
| `Camera-Pivot-Plan.md` | Plan/todo file for the "trl camera pivot" investigation | All but final validation complete; absorbed into proxy |
| `Deep-Research-Queries.md` | Saved Claude.ai deep-research queries (hash stability, terrain rendering, anti-culling) | Historical only |
| `FirstVibeCode-Rules.md` | RE workspace rules for the game directory | Superseded by `.claude/` config |
| `Session-Prompts.md` | Ready-to-paste Claude Code session starter prompts | Superseded by current skills |
| `Early-DX9-FFP-Port-Skill.md` | Early DX9 FFP port skill prompt | Superseded by `.claude/rules/dx9-ffp-port.md` |
| `Thinking-Agent-Prompt.md` | Extended-thinking-agent system prompt | Superseded by skill system |
| `Early-TRL-Skill.md` | Early skill file for TRL RTX Remix work | Superseded |
| `New-Project-Scaffold.md` | Scaffolding slash-command for `patches/<project>/` setup | Superseded |
| `VibReverse-Claude-Draft.md` | Earlier CLAUDE.md draft — engineering standards, code comments rules | Superseded by current CLAUDE.md + `.claude/CLAUDE.md` |

## Why this archive exists

Some context for what happened during the project's documentation phase is only visible in these files. They are not maintained — only kept readable. The active wiki pages contain the corrected, consolidated knowledge.

When researching a historical question like *"why did we initially think the red light at distance was the real stage light?"* the answer often lives in the Original-Session-Handoff or Combined-Research-Docs, even though the actual reframe is documented in [[../Build-016-to-044-Anti-Culling]] (build 038).
