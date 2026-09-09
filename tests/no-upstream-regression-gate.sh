#!/usr/bin/env bash
# no-upstream-regression-gate.sh — Gate 6 for the LMMS standards fork.
#
# Rule (QA-GATES.md Gate 6): behavioural changes to code INHERITED from upstream
# `origin/master` are forbidden in this fork; fixes to fork-NEW code are allowed and
# must ship a regression test in the same change.
#
# The fork's feature branches deliberately modify upstream files (that is their
# purpose), so this gate is scoped to the STANDARDS workstream: the commits made on
# top of the integration base recorded in tests/gate-base.txt.
#
# Mechanical rule, for every file changed in <base>..HEAD:
#   - tests/**            -> allowed (that is the point of the gate)
#   - build/config files  -> allowed, non-behavioural (CMakeLists.txt, .gitignore, *.cmake)
#   - CI config           -> allowed, non-runtime (.github/**)
#   - fork-NEW sources    -> allowed (listed in tests/fork-sources.txt)
#   - any other production source -> VIOLATION unless listed in
#     tests/upstream-modifications.txt with a reason (compile-only fixes).
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
ALLOW="$(grep -vE '^\s*(#|$)' tests/upstream-modifications.txt 2>/dev/null | cut -f1 | sort -u || true)"

violations=0
printf '%-56s %s\n' "changed file" "verdict"
for f in "${CHANGED[@]}"; do
	verdict=""
	case "$f" in
		tests/*) verdict="tests (allowed)" ;;
		CMakeLists.txt|.gitignore|*.cmake|*/CMakeLists.txt) verdict="build/config (allowed)" ;;
		.github/*) verdict="CI config (allowed, non-runtime)" ;;
		*.md) verdict="docs (allowed)" ;;
		*)
			if grep -qxF "$f" <<< "$FORK_NEW"; then verdict="fork-NEW (allowed)"
			elif [[ -n "$ALLOW" ]] && grep -qxF "$f" <<< "$ALLOW"; then verdict="allowlisted upstream fix"
			else verdict="VIOLATION: upstream-inherited production file changed"; violations=1
			fi
			;;
	esac
	printf '%-56s %s\n' "$f" "$verdict"
done

echo
if [[ "$violations" -eq 1 ]]; then
	echo "FAIL: Gate 6 violation(s) above — fork-NEW fixes need a regression test; upstream behaviour must not change"
	exit 1
fi
echo "PASS: no upstream-inherited production file was behaviourally changed since $BASE"
