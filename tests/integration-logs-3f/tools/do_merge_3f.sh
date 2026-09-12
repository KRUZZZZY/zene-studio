#!/usr/bin/env bash
# Merge train 3F — one unit, in the worktree, with the three index stages saved.
# usage: do_merge_3f.sh <N> <branch> <sha>
#
# Unlike 3E's driver this one does NOT resolve by keeping ours: 3F's unit 1 is
# genuinely additive, so every resolution is a deliberate union (an append-only
# ledger, or a generated manifest re-derived from its own command).  This script
# ends with the merge still in progress and the sides saved; the resolution is
# applied by the caller, one class at a time, and recorded in merge.log.
set -u
N="$1"; BRANCH="$2"; SHA="$3"
cd "$(dirname "$0")/../../.." || exit 2
L="tests/integration-logs-3f/merge${N}"
S="${L}/sides"
mkdir -p "$S"
: > "${L}/merge.log"

echo "=== merge ${N}: ${BRANCH} (${SHA}) onto $(git rev-parse --short HEAD) ===" | tee -a "${L}/merge.log"
git merge --no-commit --no-ff "${SHA}" >> "${L}/merge.log" 2>&1
MRC=$?
echo "MERGE_EXIT=${MRC}" | tee -a "${L}/merge.log"
echo "${MRC}" > "${L}/merge.exit"

mapfile -t CONFLICTED < <(git diff --name-only --diff-filter=U | LC_ALL=C sort)
echo "conflicted files: ${#CONFLICTED[@]}" | tee -a "${L}/merge.log"
printf '  %s\n' "${CONFLICTED[@]}" | tee -a "${L}/merge.log"

for f in "${CONFLICTED[@]}"; do
	m="$(echo "$f" | tr '/' '_')"
	git show ":1:${f}" > "${S}/${m}.base"   2>/dev/null || : > "${S}/${m}.base"
	git show ":2:${f}" > "${S}/${m}.ours"   2>/dev/null || : > "${S}/${m}.ours"
	git show ":3:${f}" > "${S}/${m}.theirs" 2>/dev/null || : > "${S}/${m}.theirs"
done

echo "--- what the merged index brings vs HEAD (name-status):" | tee -a "${L}/merge.log"
git diff --cached --name-status HEAD > "${L}/index-vs-head.txt" 2>&1
wc -l < "${L}/index-vs-head.txt" | sed 's/^/  paths: /' | tee -a "${L}/merge.log"
echo "--- sides saved in ${S}:" | tee -a "${L}/merge.log"
ls -1 "${S}" | sed 's/^/  /' | tee -a "${L}/merge.log"
