#!/usr/bin/env bash
# Proves a change does not alter the H7 firmware artifacts, so F4/dos work
# can never silently change the H7 build. Compares HEAD against a base ref:
#   $1  base ref (default: merge-base with origin/master)
# CI passes the PR base (pull_request) or the previous tip (push), because
# long-lived F4 branches intentionally change shared code vs upstream master.
# Exits nonzero on any difference.
set -euo pipefail

cd "$(dirname "$0")/.."

BASE_REF="${1:-}"
if [ -z "$BASE_REF" ]; then
  git fetch -q origin master
  BASE_REF=$(git merge-base HEAD origin/master)
fi

ARTIFACTS="board/obj/panda_h7.bin.signed board/obj/body_h7.bin.signed board/obj/panda_jungle_h7.bin.signed board/obj/bootstub.panda_h7.bin board/obj/bootstub.body_h7.bin board/obj/bootstub.panda_jungle_h7.bin"

hash_tree() {
  scons -Q -j"$(nproc)" >/dev/null
  # Scrub the baked-in git version before hashing: it is metadata, not code,
  # and the base tree may not have any version-pinning hook. Fixed-length
  # replacement keeps every other byte aligned.
  for f in $ARTIFACTS; do
    LC_ALL=C sed 's/DEV-[^-]\{1,10\}-\(DEBUG\|RELEASE\)/DEV-XXXXXXXX-\1/g' "$f" | sha256sum | awk '{print $1}'
  done
}

CUR=$(mktemp)
BASE=$(mktemp)
WT=$(mktemp -d)
trap 'git worktree remove -f "$WT" 2>/dev/null || true; rm -f "$CUR" "$BASE"' EXIT

hash_tree > "$CUR"
git worktree add -q --detach "$WT" "$BASE_REF"
( cd "$WT" && hash_tree ) > "$BASE"

if diff -u "$BASE" "$CUR"; then
  echo "H7 binary parity vs $BASE_REF: PASS"
else
  echo "H7 binary parity vs $BASE_REF: FAIL — H7 artifacts changed"
  exit 1
fi
