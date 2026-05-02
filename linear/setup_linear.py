"""One-time setup: writes linear/config.json for TombRaiderLegendRTX.

Usage:
    export LINEAR_API_KEY="lin_api_xxxx"
    python linear/setup_linear.py --team-id <team-id>

Run without --team-id to list all available teams and their IDs.
"""
import argparse
import json
import os
import sys
from pathlib import Path

try:
    import requests
except ImportError:
    sys.exit("pip install requests")

API_KEY = os.environ.get("LINEAR_API_KEY", "")
if not API_KEY:
    sys.exit("Set LINEAR_API_KEY environment variable")

HEADERS = {"Authorization": API_KEY, "Content-Type": "application/json"}
GQL = "https://api.linear.app/graphql"


def gql(query: str, variables: dict | None = None) -> dict:
    r = requests.post(GQL, json={"query": query, "variables": variables or {}},
                      headers=HEADERS, timeout=30)
    r.raise_for_status()
    data = r.json()
    if "errors" in data:
        raise RuntimeError(data["errors"])
    return data["data"]


def main() -> None:
    parser = argparse.ArgumentParser(description="Set up Linear config for TombRaiderLegendRTX")
    parser.add_argument("--team-id", help="Exact Linear team ID to use")
    args = parser.parse_args()

    me = gql("{ viewer { id name } }")
    print(f"Authenticated as: {me['viewer']['name']}")

    teams_data = gql("{ teams { nodes { id name } } }")
    teams = teams_data["teams"]["nodes"]

    if not args.team_id:
        print("\nAvailable teams (pass --team-id to select one):")
        for t in teams:
            print(f"  {t['id']}  {t['name']}")
        sys.exit(0)

    team = next((t for t in teams if t["id"] == args.team_id), None)
    if not team:
        print(f"Team ID '{args.team_id}' not found. Available teams:")
        for t in teams:
            print(f"  {t['id']}  {t['name']}")
        sys.exit(1)

    print(f"Using team: {team['name']} ({team['id']})")
    config = {"team_id": team["id"], "team_name": team["name"]}
    Path("linear/config.json").write_text(json.dumps(config, indent=2))
    print("linear/config.json written. Run linear/sync.py to push first sync.")


if __name__ == "__main__":
    main()
