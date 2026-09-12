#!/usr/bin/env bash
# no-upstream-regression-gate.sh — Gate 6: no UNDECLARED divergence in upstream code.
#
# Rule. Behavioural changes to code INHERITED from upstream `origin/master` are allowed
# only when they are declared, with a reason, in tests/upstream-modifications.txt. That
# file is this repo's divergence ledger: it exists so a product can evolve a DAW (you
# cannot implement plugin delay compensation without changing the mixer) without silently
# forking upstream. Anything changed in inherited code that is NOT in the ledger is a
# violation. Fixes to fork-NEW code (tests/fork-sources.txt) need no entry, but must ship
# a regression test in the same change.
#
# Scope. The gate examines <base>..HEAD where <base> is tests/gate-base.txt: the commit at
# which this gate suite was ported in. Commits before it predate the ledger and are out of
# scope — widen the base deliberately, with a reason, if you need older history covered.
#
# Mechanical rule, for every file changed in <base>..HEAD:
#   - tests/**            -> allowed (that is the point of the gate)
#   - build/config files  -> allowed, non-behavioural (CMakeLists.txt, .gitignore, *.cmake)
#   - CI config           -> allowed, non-runtime (.github/**)
#   - *.md                -> allowed (documentation)
#   - fork-NEW sources    -> allowed (listed in tests/fork-sources.txt)
#   - tools/**            -> allowed BY CONSTRUCTION, whatever the file is. tools/ does not
#     exist at the fork point, so every path under it — source, fixture, JSON, .gitignore —
#     is fork-authored and cannot be a divergence of inherited code. Verified for this
#     commit with both of these, and they must print nothing:
#       git ls-tree -r --name-only 4e677cb6c6ab -- tools      # the upstream base commit
#       git ls-tree -r --name-only origin/master -- tools      # upstream master today
#     (4e677cb6c6ab is the upstream commit this fork is based on, named in the header of
#     tests/fork-sources.txt; the manifest header of tests/tools-sources.txt records the
#     same two commands and the same result.)
#     tests/tools-sources.txt remains the SCOPE manifest for gates 4, 7 and 8 (the file
#     length/CCN/duplication ratchets measure tools/ with `--scope tools`), and Gate 9
#     still requires every tools/ source to be named there or in another scope list. It is
#     NOT a declaration this gate may demand, and demanding it here was a defect, because
#     that manifest's documented candidate command admits only SOURCE extensions:
#       git diff --name-only --diff-filter=A 4e677cb6c6ab HEAD -- tools \
#         | grep -E '\.(py|sh|cpp|c|h|hpp|cc|cxx)$' | LC_ALL=C sort
#     so a non-source file under tools/ (a fixture, a JSON snapshot, a .gitignore) can
#     never be listed in it, could never be classified as allowed here, and was reported
#     as "VIOLATION: undeclared change to upstream-inherited code" — a false statement
#     about a file upstream has never had. Three such files had already been reported
#     (tools/mcp-zene-control/{.gitignore,tests/data/agent-control-fixture.mmp,
#     zene_control/commands_snapshot.json}); the class of defect, not those three paths,
#     is what this rule fixes.
#   - any other production source -> allowed ONLY if listed in
#     tests/upstream-modifications.txt with a non-empty reason
#
# Usage: bash tests/no-upstream-regression-gate.sh [base]

set -uo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/.." && pwd)"
cd "$ROOT" || exit 2

BASE="${1:-}"
if [[ -z "$BASE" ]]; then
	[[ -f tests/gate-base.txt ]] || { echo "no gate base recorded (tests/gate-base.txt)" >&2; exit 2; }
	BASE="$(grep -vE '^\s*(#|$)' tests/gate-base.txt | head -1)"
fi
git rev-parse --verify --quiet "$BASE^{commit}" >/dev/null \
	|| { echo "gate base '$BASE' is not a commit" >&2; exit 2; }

mapfile -t CHANGED < <(git diff --name-only "$BASE"..HEAD | sort -u)
[[ ${#CHANGED[@]} -eq 0 ]] && { echo "PASS: no changes since $BASE"; exit 0; }

FORK_NEW="$(grep -vE '^\s*(#|$)' tests/fork-sources.txt | sort -u)"
# tools/ needs no manifest read: a tools/ path is allowed by construction (see the header).
# tests/tools-sources.txt is the SCOPE manifest for gates 4, 7 and 8, not a declaration
# this gate may demand — its candidate command admits source extensions only, so a fixture,
# a JSON snapshot or a .gitignore under tools/ could never appear in it.

# --- ledger: every entry needs a non-empty reason ---------------------------
declare -A ALLOW REASON
if [[ -f tests/upstream-modifications.txt ]]; then
	while IFS=$'\t' read -r path why; do
		[[ "$path" =~ ^[[:space:]]*# ]] && continue
		[[ -z "${path// /}" ]] && continue
		if [[ -z "${why// /}" ]]; then
			echo "ledger error: tests/upstream-modifications.txt entry '$path' has no reason" >&2
			echo "every declared divergence must say why (this gate refuses to honour a blank one)" >&2
			exit 2
		fi
		ALLOW["$path"]=1
		REASON["$path"]="$why"
	done < tests/upstream-modifications.txt
fi

violations=0
declared=0
printf '%-56s %s\n' "changed file" "verdict"
for f in "${CHANGED[@]}"; do
	verdict=""
	case "$f" in
		tests/*) verdict="tests (allowed)" ;;
		CMakeLists.txt|.gitignore|*.cmake|*/CMakeLists.txt) verdict="build/config (allowed)" ;;
		.github/*) verdict="CI config (allowed, non-runtime)" ;;
		*.md) verdict="docs (allowed)" ;;
		tools/*) verdict="fork tooling (allowed by construction: tools/ does not exist upstream)" ;;
		*)
			if grep -qxF "$f" <<< "$FORK_NEW"; then
				verdict="fork-NEW (allowed)"
			elif [[ -n "${ALLOW[$f]:-}" ]]; then
				verdict="declared divergence -> ${REASON[$f]}"
				declared=$((declared + 1))
			else
				verdict="VIOLATION: undeclared change to upstream-inherited code"
				violations=1
			fi
			;;
	esac
	printf '%-56s %s\n' "$f" "$verdict"
done

echo
if [[ "$violations" -eq 1 ]]; then
	echo "FAIL: Gate 6 violation(s) above — either revert the change, or declare it in"
	echo "      tests/upstream-modifications.txt with a reason and ship a regression test."
	exit 1
fi
# Two different numbers, so the summary says which is which: $declared counts the
# CHANGED PATHS this run classified as declared, while the ledger holds one entry
# per declared path whether or not it changed since $BASE (deleted paths and
# renames keep their entry). Printing the first as "files in the ledger" made the
# two look like a contradiction.
ledger_entries=0
if [[ -f tests/upstream-modifications.txt ]]; then
	ledger_entries="$(grep -vcE '^[[:space:]]*(#|$)' tests/upstream-modifications.txt)"
fi
ledger_noun="entries"
[[ "$ledger_entries" -eq 1 ]] && ledger_noun="entry"
echo "PASS: every change to upstream-inherited code since $BASE is declared"
echo "      ($declared changed path(s) declared; the ledger holds $ledger_entries $ledger_noun)"
