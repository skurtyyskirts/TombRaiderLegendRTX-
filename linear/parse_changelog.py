"""Parse CHANGELOG.md into structured build records for Linear sync.

Supports TRL headings:
  ## [2026-04-13] BUILDS-076-077 -- title  (range: build_num = 77)
  ## [2026-04-13] BUILDS-077 -- title      (single)

Non-build ## headings (TERRAIN-ANALYSIS, BOOTSTRAP, Dead Ends, etc.) terminate
the current record so their content is never absorbed into a build entry.

Result classification:
  - Per-build result lines ('- Build 077: FIXED') are matched to their build
    number; lines for a DIFFERENT build in the range are skipped so they
    cannot contaminate this record's result.
  - 'fail' is sticky for general body text, but a definitive per-build result
    line for THIS build always wins.
  - Negated crash phrases ('no crash', 'crash-free') are NOT treated as failures.
"""
import re
from pathlib import Path
from typing import Any

_BUILD_RE = re.compile(
    r"^##\s+\[[^\]]+\]\s+BUILDS?-(\d+)(?:-(\d+))?",
    re.IGNORECASE,
)
# Matches lines like "- Build 077: FIXED" or "Build 076: FAIL-lights"
_PER_BUILD_RE = re.compile(
    r"build\s+(\d+)\s*:\s*(fail|fixed|pass|broken|working)",
    re.IGNORECASE,
)

_PASS_WORDS = ("pass", "working", "fixed", "confirmed", "stable")
_FAIL_EXACT = ("fail", "broken", "regression", "black screen")
_NEGATED_CRASH = ("no crash", "crash-free", "crash guard", "crash protection", "crash-proof")


def _classify_line(low: str, current_result: str, build_num: int) -> str:
    # Per-build result line takes priority over general classification
    m = _PER_BUILD_RE.search(low)
    if m:
        line_build = int(m.group(1))
        if line_build != build_num:
            # This result is for a different build in the range — skip it
            return current_result
        # Definitive result for our specific build
        status = m.group(2).lower()
        return "pass" if status in ("fixed", "working", "pass") else "fail"

    # General classification with sticky-fail
    if current_result != "fail" and any(w in low for w in _PASS_WORDS):
        current_result = "pass"
    if any(w in low for w in _FAIL_EXACT):
        current_result = "fail"
    if "crash" in low and not any(neg in low for neg in _NEGATED_CRASH):
        current_result = "fail"
    return current_result


def parse_changelog(path: str = "CHANGELOG.md") -> list[dict[str, Any]]:
    text = Path(path).read_text(encoding="utf-8", errors="replace")
    builds: list[dict[str, Any]] = []
    current: dict[str, Any] | None = None

    for line in text.splitlines():
        m = _BUILD_RE.match(line)
        if m:
            if current:
                builds.append(current)
            build_num = int(m.group(2) if m.group(2) else m.group(1))
            current = {
                "build": build_num,
                "lines": [],
                "result": "unknown",
                "dead_ends": [],
                "blockers": [],
            }
            continue

        # Non-build ## heading terminates the current record
        if line.startswith("## ") and current is not None:
            builds.append(current)
            current = None
            continue

        if current is None:
            continue

        current["lines"].append(line)
        low = line.lower()
        current["result"] = _classify_line(low, current["result"], current["build"])
        if "dead end" in low or "dead-end" in low:
            current["dead_ends"].append(line.strip())
        if "blocker" in low:
            current["blockers"].append(line.strip())

    if current:
        builds.append(current)
    return builds


if __name__ == "__main__":
    import json
    builds = parse_changelog()
    print(json.dumps([{"build": b["build"], "result": b["result"]} for b in builds], indent=2))
