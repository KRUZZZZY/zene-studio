#!/usr/bin/env bash
# test-release-ref-fitness.sh — red/green proof for the release ref-fitness oracle.
#
# WHY THIS EXISTS
# ---------------
# `tests/release-ref-fitness.sh` is the only thing that decides whether a ref may be cut
# a tag, and a check that has never been seen refusing a ref is a claim, not a gate. This
# harness drives that oracle BOTH ways against refs it builds itself, so the red case is a
# real ref with a real defect rather than a fixture describing one:
#
#   control 1  the oracle's leg table is intact          every leg of the three upstream
#                                                        jobs is still named
#   control 2  the CI leg's required job set is exact    7 platform jobs + checks' 3 +
#                                                        static-gates = 11 named jobs
#   control 3  a deliberately red ref is REFUSED          a scratch ref whose new test is
#                                                        registered in no scope manifest
#                                                        -> g9-fork-sources exit 1, and the
#                                                        oracle exits 1
#   control 4  the same leg PASSES on the ref it was cut from
#   control 5  a leg that cannot run is not a pass         a missing tool must not become
#                                                        exit 0
#   control 6  the staging-path policy gate goes red       a workflow COPY with the
#                                                        dispatch staging path restored is
#                                                        refused (the checker has been seen
#                                                        red, against a weakened copy - the
#                                                        same control shape as
#                                                        tests/test-package-upload-guard.sh)
#
# The scratch ref is a TEMPORARY BRANCH under rel2/selftest-* which this script deletes on
# exit, including on failure. Nothing is pushed; no other ref is touched.
#
# Usage: bash tests/test-release-ref-fitness.sh
# Exit codes: 0 = every control held, 1 = at least one did not, 2 = setup error.

set -uo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/.." && pwd)"
cd "$ROOT" || exit 2

command -v python3 >/dev/null 2>&1 || { echo "error: python3 required" >&2; exit 2; }
git rev-parse --git-dir >/dev/null 2>&1 || { echo "error: not a git worktree" >&2; exit 2; }

FAILED=0
check() { # check <sentence> <expected> <actual>
	if [ "$2" = "$3" ]; then
		printf '  PASS %-74s %s\n' "$1" "$3"
	else
		printf '  FAIL %-74s got %s, expected %s\n' "$1" "$3" "$2"
		FAILED=1
	fi
}

SELFTEST_BRANCH="rel2/selftest-$$"
BASE_SHA="$(git rev-parse HEAD)"
cleanup() {
	git worktree remove --force "$ROOT/.rel2-selftest-wt" >/dev/null 2>&1 || true
	git branch -D "$SELFTEST_BRANCH" >/dev/null 2>&1 || true
	rm -rf "$TMP"
}
TMP="$(mktemp -d)"
trap cleanup EXIT

# ---------------------------------------------------------------------------
echo "=== control 1: the leg table still covers the three upstream jobs ==="
LEGS="$(bash tests/release-ref-fitness.sh --list-legs)"
check "--list-legs exits 0" 0 "$?"
for leg in scripted-verify package-upload-guard yamllint \
           g3-no-tautology g4-complexity g4-complexity-tools g6-upstream-regression \
           g7-file-length g7-file-length-tools g8-duplication g8-duplication-tools \
           g9-fork-sources g11-evidence mcp-bridge-python-tests staging-path; do
	case "$LEGS" in
		*"$leg"*) : ;;
		*) printf '  FAIL leg %-66s missing from --list-legs\n' "$leg"; FAILED=1 ;;
	esac
done
printf '  PASS %-74s %s\n' "all 15 legs named" "$(printf '%s\n' "$LEGS" | grep -c .)"
echo

# ---------------------------------------------------------------------------
echo "=== control 2: the CI leg requires the exact upstream job set ==="
REQ="$(bash tests/release-ci-evidence-gate.sh --dump-required)"
for job in linux-x86_64 linux-arm64 macos-x86_64 macos-arm64 mingw64 msvc-x64 windows-arm64 \
           scripted-checks shellcheck yamllint; do
	case "$REQ" in
		*"$job"*) : ;;
		*) printf '  FAIL required job %-59s missing\n' "$job"; FAILED=1 ;;
	esac
done
case "$REQ" in
	*"static gates (3, 4, 6, 7, 8, 9, 11)"*) check "quality-gates' static-gates job is required by name" 0 0 ;;
	*) check "quality-gates' static-gates job is required by name" 0 1 ;;
esac
check "one required job per line: 11 required names" 11 "$(printf '%s\n' "$REQ" | grep -c .)"
echo

# ---------------------------------------------------------------------------
echo "=== control 3/4: a deliberately red ref is refused; its base is not ==="
# The defect is a NEW TEST FILE that no scope manifest registers. It is not a fixture
# describing a defect: the ref really carries the file, and Gate 9 really reads the ref.
git worktree add --detach "$ROOT/.rel2-selftest-wt" "$BASE_SHA" >/dev/null 2>&1 || {
	echo "error: cannot create the scratch worktree" >&2; exit 2; }
cat > "$ROOT/.rel2-selftest-wt/tests/src/core/Rel2FitnessProbeTest.cpp" <<'CPP'
// DELIBERATELY RED REF — a local scratch-branch fixture created by
// tests/test-release-ref-fitness.sh. It exists to be refused: it is a test file that no
// scope manifest registers (Gate 9), so the release oracle must not judge this ref fit.
// It is never meant to be merged and the branch that carries it is deleted on exit.
#include "../core/TestSupport.h"

namespace lmms::test
{

class Rel2FitnessProbeTest : public TestSupport
{
};

} // namespace lmms::test
CPP
(
	cd "$ROOT/.rel2-selftest-wt" || exit 2
	git add tests/src/core/Rel2FitnessProbeTest.cpp >/dev/null 2>&1
	git -c user.name=rel2 -c user.email=rel2@localhost commit -q -m "test(rel2): deliberately red ref for the fitness self-test" >/dev/null 2>&1
	git branch -f "$SELFTEST_BRANCH" HEAD >/dev/null 2>&1
)
RED_SHA="$(git rev-parse "$SELFTEST_BRANCH" 2>/dev/null || true)"
if [ -z "$RED_SHA" ] || [ "$RED_SHA" = "$BASE_SHA" ]; then
	echo "error: the deliberate red ref was not created" >&2
	exit 2
fi

bash tests/release-ref-fitness.sh --ref "$SELFTEST_BRANCH" --only g9-fork-sources > "$TMP/red.log" 2>&1
check "the red ref is REFUSED by g9-fork-sources (exit 1)" 1 "$?"
bash tests/release-ref-fitness.sh --ref "$SELFTEST_BRANCH" > "$TMP/red-full.log" 2>&1
check "the red ref is REFUSED by the full leg set (exit 1)" 1 "$?"
bash tests/release-ref-fitness.sh --ref "$BASE_SHA" --only g9-fork-sources > "$TMP/green.log" 2>&1
check "the same leg PASSES on the base the red ref was cut from (exit 0)" 0 "$?"
echo "  (red ref: $SELFTEST_BRANCH = ${RED_SHA:0:9}, base: ${BASE_SHA:0:9})"
echo

# ---------------------------------------------------------------------------
echo "=== control 5: a leg that cannot run is a refusal, never a pass ==="
# lizard is Gate 4's tool. With it off the PATH the leg cannot be measured; the oracle must
# not read that as green. Exit 1 (the leg went red on the missing binary) and exit 3 (the
# oracle classified the leg incomplete) are both refusals; 0 is not acceptable.
PATH="/usr/bin:/bin" bash tests/release-ref-fitness.sh --ref "$BASE_SHA" --only g4-complexity > "$TMP/notool.log" 2>&1
NO_TOOL_RC=$?
if [ "$NO_TOOL_RC" -ne 0 ]; then
	check "a leg whose tool is absent refuses (not exit 0)" "refused" "refused"
	printf '  .... %-74s (exit %s; the oracle does not distinguish a red leg from an unmeasurable one here - both refuse)\n' "measured status" "$NO_TOOL_RC"
else
	check "a leg whose tool is absent refuses (not exit 0)" "refused" "PASSED (exit 0)"
fi
grep -m1 -E '^(FAIL|RESULT)' "$TMP/notool.log" | sed 's/^/        /' || true
echo

# ---------------------------------------------------------------------------
echo "=== control 6: the staging-path policy gate has been seen red ==="
mkdir -p "$TMP/weakened/.github/workflows"
cp .github/workflows/build.yml .github/workflows/release.yml "$TMP/weakened/.github/workflows/"
bash tests/release-staging-path-gate.sh > "$TMP/policy-green.log" 2>&1
check "the real workflow shape PASSES the policy gate (exit 0)" 0 "$?"
# Restore the dispatch staging path the release path removed. The checker must refuse it.
sed -i "s|if: startsWith(github.ref, 'refs/tags/')$|if: startsWith(github.ref, 'refs/tags/') \|\| github.event_name == 'workflow_dispatch'|" \
	"$TMP/weakened/.github/workflows/build.yml"
RELEASE_STAGING_ROOT="$TMP/weakened" bash tests/release-staging-path-gate.sh > "$TMP/policy-red.log" 2>&1
check "a workflow copy with the dispatch staging path restored is REFUSED (exit 1)" 1 "$?"
grep -m1 -A1 'FAIL A1' "$TMP/policy-red.log" | sed 's/^/        /' || true
echo

if [ "$FAILED" -eq 1 ]; then
	echo "RESULT: FAIL — at least one control above did not hold."
	exit 1
fi
echo "RESULT: PASS — the oracle refuses a deliberately red ref, passes its base, refuses an"
echo "        unmeasurable leg, and the staging-path policy gate has been seen red."
exit 0
