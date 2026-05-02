import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from patches.TombRaiderLegend.nightly.publication import build_draft_pr_payload, format_rolling_branch, format_run_branch, parse_github_remote


def test_publication_helpers_build_expected_payloads() -> None:
    owner, repo = parse_github_remote("git@github.com:example/reverse-engineering.git")
    run_branch = format_run_branch("nightly/trl", "2026-04-14T03:04:05", "run-20260414-030405")
    rolling_branch = format_rolling_branch("nightly/trl")
    payload = build_draft_pr_payload(
        title="TRL RTX Remix Nightly Solver",
        body="nightly body",
        head_branch=rolling_branch,
        base_branch="main",
    )

    assert (owner, repo) == ("example", "reverse-engineering")
    assert run_branch == "nightly/trl-20260414-030405-run-20260414-030405"
    assert rolling_branch == "nightly/trl-rolling"
    assert payload["draft"] is True
    assert payload["head"] == rolling_branch
