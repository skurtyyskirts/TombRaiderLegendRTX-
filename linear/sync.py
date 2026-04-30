"""Sync CHANGELOG to Linear for TombRaiderLegendRTX.

Dedup strategy: query Linear directly before creating each issue.
This is CI-safe — no ephemeral local cursor that gets discarded between runs.
Syncs the most recent MAX_BUILDS build entries each run.
"""
import json
import os
import sys
from pathlib import Path

_root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
if _root not in sys.path:
    sys.path.insert(0, _root)

from linear.parse_changelog import parse_changelog  # noqa: E402

try:
    import requests
except ImportError:
    sys.exit("pip install requests")

API_KEY = os.environ.get("LINEAR_API_KEY", "")
if not API_KEY:
    sys.exit("Set LINEAR_API_KEY environment variable")

HEADERS = {"Authorization": API_KEY, "Content-Type": "application/json"}
GQL = "https://api.linear.app/graphql"
MAX_BUILDS = 10


def gql(query: str, variables: dict | None = None) -> dict:
    r = requests.post(GQL, json={"query": query, "variables": variables or {}},
                      headers=HEADERS, timeout=30)
    r.raise_for_status()
    payload = r.json()
    if "errors" in payload:
        raise RuntimeError(payload["errors"])
    return payload.get("data", {})


def issue_exists(team_id: str, title_prefix: str) -> bool:
    """True if a Linear issue title starts with title_prefix (exact token boundary).

    The prefix must include a trailing space so that e.g. 'Build 7 ' does not
    match 'Build 70 — ...' or 'Build 77 — ...'.
    """
    q = """
    query($f: IssueFilter!) {
      issues(filter: $f, first: 1) { nodes { id } }
    }"""
    data = gql(q, {"f": {
        "team": {"id": {"eq": team_id}},
        "title": {"startsWith": title_prefix},
    }})
    return bool(data.get("issues", {}).get("nodes"))


def create_issue(team_id: str, title: str, description: str, label_ids: list[str]) -> str:
    q = """
    mutation($input: IssueCreateInput!) {
      issueCreate(input: $input) { issue { identifier } }
    }"""
    data = gql(q, {"input": {"teamId": team_id, "title": title,
                             "description": description, "labelIds": label_ids}})
    return data["issueCreate"]["issue"]["identifier"]


def main() -> None:
    config_path = Path("linear/config.json")
    if not config_path.exists():
        sys.exit("Run linear/setup_linear.py first")
    config = json.loads(config_path.read_text())
    team_id = config["team_id"]

    builds = parse_changelog()
    if not builds:
        print("No builds found in changelog.")
        return

    recent = sorted(builds, key=lambda b: b["build"], reverse=True)[:MAX_BUILDS]
    created = skipped = 0
    for b in reversed(recent):
        # Trailing space ensures 'Build 7 ' won't match 'Build 70 — ...' or 'Build 77 — ...'
        prefix = f"Build {b['build']} "
        if issue_exists(team_id, prefix):
            skipped += 1
            continue
        title = f"Build {b['build']} — {b['result'].upper()}"
        body = "\n".join(b["lines"][:30])
        issue_id = create_issue(team_id, title, body, [])
        print(f"Created: {issue_id} ({title})")
        created += 1

    print(f"Sync complete: {created} created, {skipped} already existed.")


if __name__ == "__main__":
    mode = sys.argv[1] if len(sys.argv) > 1 else "sync"
    if mode in ("--status", "status"):
        config = json.loads(Path("linear/config.json").read_text())
        print("Team:", config["team_id"])
    elif mode == "research":
        print("Research mode: invoke the research-scanner Claude agent for research tasks.")
    else:
        main()
