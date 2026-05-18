"""GitHub publication payloads and lightweight REST helpers."""
from __future__ import annotations

import json
import os
import subprocess
import urllib.error
import urllib.parse
import urllib.request
from datetime import datetime
from typing import Any

from config import REPO_ROOT

def parse_github_remote(remote_url: str) -> tuple[str, str]:
        url = remote_url.strip()
        if url.endswith(".git"):
                    url = url[:-4]
                if url.startswith("git@github.com:"):
                            owner_repo = url.split(":", 1)[1]
else:
        parsed = urllib.parse.urlparse(url)
        if parsed.hostname not in ("github.com", "www.github.com"):
                        raise ValueError(f"Unsupported GitHub remote: {remote_url}")
                    owner_repo = parsed.path.lstrip("/")
    owner, repo = owner_repo.split("/", 1)
    return owner, repo

def discover_origin_remote() -> tuple[str, str]:
        result = subprocess.run(
            ["git", "remote", "get-url", "origin"],
            cwd=str(REPO_ROOT),
            capture_output=True,
            text=True,
            check=True,
)
    return parse_github_remote(result.stdout.strip())

def format_run_branch(prefix: str, started_at: str, run_id: str) -> str:
        stamp = datetime.fromisoformat(started_at).strftime("%Y%m%d-%H%M%S")
    return f"{prefix}-{stamp}-{run_id}"

def format_rolling_branch(prefix: str) -> str:
        return f"{prefix}-rolling"

def build_draft_pr_payload(
        *,
        title: str,
        body: str,
        head_branch: str,
        base_branch: str,
) -> dict[str, Any]:
        return {
            "title": title,
            "body": body,
            "head": head_branch,
            "base": base_branch,
            "draft": True,
}

def build_nightly_comment(run_id: str, summary_markdown: str, leaderboard: list[dict[str, Any]]) -> str:
        lines = [
            f"## Nightly Run `{run_id}`",
            "",
            summary_markdown.strip(),
            "",
            "### Leaderboard",
]
    for entry in leaderboard:
                lines.append(
                                f"- `{entry['candidate_id']}`: {entry['verdict']} "
                                f"(hard={entry['hard_gate_pass']}, sky={entry['sky_pass']}, water={entry['water_pass']}, "
                                f"screen={entry.get('screen_pass', False)}, release={entry.get('release_pass', False)})"
                )
            return "\n".join(lines).strip() + "\n"

def resolve_github_token() -> str | None:
        """Return the GitHub token used for nightly publication.
            Prefer the standard GITHUB_TOKEN env var so the nightly path can reuse the
                user's existing GitHub CLI/API token. TRL_GITHUB_TOKEN remains as a
                    backwards-compatible fallback.
                        """
    return os.environ.get("GITHUB_TOKEN") or os.environ.get("TRL_GITHUB_TOKEN")

def github_request(method: str, url: str, payload: dict[str, Any] | None = None) -> dict[str, Any]:
        token = resolve_github_token()
    if not token:
                raise RuntimeError("GITHUB_TOKEN is not set")

    data = None
    if payload is not None:
                data = json.dumps(payload).encode("utf-8")
            request = urllib.request.Request(
                        url,
                        data=data,
                        method=method,
                        headers={
                                        "Authorization": f"Bearer {token}",
                                        "Accept": "application/vnd.github+json",
                                        "Content-Type": "application/json",
                                        "User-Agent": "trl-nightly-solver",
                        },
            )
    try:
                with urllib.request.urlopen(request, timeout=30) as response:
                                raw = response.read()
    except urllib.error.HTTPError as exc:
        body = exc.read().decode("utf-8", errors="replace")
        raise RuntimeError(f"GitHub API {exc.code}: {body}") from exc
    if not raw:
                return {}
            return json.loads(raw.decode("utf-8"))
