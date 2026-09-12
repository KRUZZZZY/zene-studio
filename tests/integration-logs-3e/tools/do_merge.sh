#!/usr/bin/env bash
# Merge train 3E — one unit, in the worktree, with the sides saved and the
# resolutions asserted.  usage: do_merge.sh <N> <branch> <sha>
#
# Every unit of this train is a re-issue of work the release line already
# carries, so the resolution rule is: keep OURS, and then PROVE the merge
# carried no content by asserting the merged index is byte-identical to the
# pre-merge HEAD tree.  Nothing here is resolved by unioning lines across
# conflict markers; the manifests and the ledger are re-derived/verified by
# regen.py and precommit_check.py afterwards.
set -u
N="$1"; BRANCH="$2"; SHA="$3"
cd "$(dirname "$0")/../../.." || exit 2
L="tests/integration-logs-3e/merge${N}"
S="${L}/sides"
mkdir -p "$S"
: > "${L}/merge.log"

echo "=== merge ${N}: ${BRANCH} (${SHA}) onto $(git rev-parse --short HEAD) ===" | tee -a "${L}/merge.log"
git merge --no-commit --no-ff "${SHA}" >> "${L}/merge.log" 2>&1
MRC=$?
echo "MERGE_EXIT=${MRC}" | tee -a "${L}/merge.log"
echo "MERGE_EXIT=${MRC}" > "${L}/merge.exit"

mapfile -t CONFLICTED < <(git diff --name-only --diff-filter=U | LC_ALL=C sort)
echo "conflicted files: ${#CONFLICTED[@]}" | tee -a "${L}/merge.log"
printf '  %s\n' "${CONFLICTED[@]}" | tee -a "${L}/merge.log"

# --- save all three stages of every conflicted path (the sides) --------------
for f in "${CONFLICTED[@]}"; do
	m="$(echo "$f" | tr '/' '_')"
	git show ":1:${f}" > "${S}/${m}.base"   2>/dev/null || : > "${S}/${m}.base"
	git show ":2:${f}" > "${S}/${m}.ours"   2>/dev/null || : > "${S}/${m}.ours"
	git show ":3:${f}" > "${S}/${m}.theirs" 2>/dev/null || : > "${S}/${m}.theirs"
done

# --- resolve: ours for every conflicted path --------------------------------
for f in "${CONFLICTED[@]}"; do
	git checkout --ours -- "$f" || { echo "RESOLVE-FAILED ${f}"; exit 3; }
	git add -- "$f" || { echo "STAGE-FAILED ${f}"; exit 3; }
done

# --- assert: no markers anywhere in the index, no unstaged residue ----------
# NB: exclude this programme's own evidence dirs - three 3C resolver scripts legitimately
# carry conflict-marker text as string literals, and they are not this merge's markers.
MO="$(git grep -nE '^(<<<<<<< |>>>>>>> |=======$)' -- . ':(exclude)tests/integration-logs-3a' ':(exclude)tests/integration-logs-3b' ':(exclude)tests/integration-logs-3c' ':(exclude)tests/integration-logs-3d' ':(exclude)tests/integration-logs-3e' 2>/dev/null | head -5)"
if [[ -n "$MO" ]]; then echo "MARKERS-FOUND:"; echo "$MO"; exit 4; fi
if [[ -n "$(git status --porcelain | grep -vE '^\?\?' || true)" ]]; then
	echo "WORKTREE-NOT-CLEAN:"; git status --porcelain | grep -vE '^\?\?' | head -20; exit 4
fi

# --- THE assertion of this train: the merged tree is HEAD's tree ------------
CHANGED="$(git diff --cached --name-only HEAD | LC_ALL=C sort)"
if [[ -n "$CHANGED" ]]; then
	echo "MERGE-CARRIES-CONTENT (index differs from HEAD):"
	echo "$CHANGED" | sed 's/^/  /'
	echo "MERGE-CARRIES-CONTENT" > "${L}/carries-content.flag"
else
	echo "NO-CONTENT: the merged index is byte-identical to HEAD ($(git rev-parse --short HEAD)^{tree} $(git rev-parse HEAD^{tree} | cut -c1-12))"
fi | tee -a "${L}/merge.log"

echo "conflicted=${#CONFLICTED[@]} merge_exit=${MRC}" >> "${L}/merge.log"
