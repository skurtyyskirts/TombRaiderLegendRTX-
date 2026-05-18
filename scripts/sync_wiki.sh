#!/usr/bin/env bash
# Sync the in-repo wiki/ directory to the GitHub Wiki for this repo.
#
# Prerequisites:
#   1. The GitHub Wiki must be ENABLED on the repository:
#      https://github.com/skurtyyskirts/TombRaiderLegendRTX-/settings
#      → Features → check "Wikis"
#   2. The wiki must be initialized (visit the wiki page once and click
#      "Create the first page" if prompted, or create any page).
#   3. You must have push access to the repo.
#
# After running, the content shows up at:
#   https://github.com/skurtyyskirts/TombRaiderLegendRTX-/wiki

set -euo pipefail

REPO_DIR="$(cd "$(dirname "$0")/.." && pwd)"
WIKI_DIR="$REPO_DIR/wiki"
WIKI_REMOTE="https://github.com/skurtyyskirts/TombRaiderLegendRTX-.wiki.git"
WORK_DIR="$(mktemp -d)"

if [ ! -d "$WIKI_DIR" ]; then
    echo "ERROR: wiki/ directory not found at $WIKI_DIR" >&2
    exit 1
fi

echo "Cloning wiki repo to $WORK_DIR ..."
git clone "$WIKI_REMOTE" "$WORK_DIR/wiki" || {
    cat >&2 <<EOF
ERROR: Could not clone wiki repo.

If the error is "repository not found", the wiki is probably not enabled
on the repo yet. Enable it at:
  https://github.com/skurtyyskirts/TombRaiderLegendRTX-/settings
then create at least one initial page in the GitHub web UI, then re-run.
EOF
    exit 1
}

echo "Copying wiki/ content into wiki repo..."
# Remove everything except .git, then copy fresh content.
find "$WORK_DIR/wiki" -mindepth 1 -maxdepth 1 ! -name '.git' -exec rm -rf {} +
cp -r "$WIKI_DIR"/. "$WORK_DIR/wiki/"

cd "$WORK_DIR/wiki"
git add -A
if git diff --cached --quiet; then
    echo "No changes to push — wiki is already in sync."
    rm -rf "$WORK_DIR"
    exit 0
fi

COMMIT_MSG="sync wiki from main repo @ $(cd "$REPO_DIR" && git rev-parse --short HEAD)"
git commit -m "$COMMIT_MSG"
git push origin master || git push origin main

echo ""
echo "Wiki synced. View at:"
echo "  https://github.com/skurtyyskirts/TombRaiderLegendRTX-/wiki"

rm -rf "$WORK_DIR"
