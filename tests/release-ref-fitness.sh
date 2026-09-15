#!/usr/bin/env bash
# release-ref-fitness.sh — may THIS REF be released?
#
# WHY THIS EXISTS (REL-2, 2026-09-15)
# ----------------------------------
# `tests/release-honesty-gate.sh` answers a different question. Its default mode reads a
# build's REPORTED OPTIONS (WANT_VST3='ON', ...) and compares them with what the release
# notes claim; its `--tag-run` mode reads the Actions API's conclusions. Neither asks
# whether a *ref* is fit to release. This script is that question, as a command.
#
# The class of failure it closes. `.github/workflows/build.yml`'s `release-gate` job is
# `needs:` every build job and carries no `if: always()`, so on a red seven-platform
# matrix GitHub SKIPS it — the job records `skipped`, not `failed`, and a skipped
# conclusion is not a refusal that anything downstream can see. Measured on the product
# repo, build run 34870198514 @ f611c888b (`release/0.3.0`): 5 of 7 platform jobs red,
# and `release gate (green matrix required)` = `skipped`. It was also tag-only
# (`if: startsWith(github.ref, 'refs/tags/')`), so it could not run BEFORE a tag existed.
# A gate that only runs after the tag is cut cannot keep the tag off a red commit.
#
# WHAT IT MEASURES — two independent legs, both required when --require-ci is passed
# ---------------------------------------------------------------------------------
# LEG 1  ref content (local, no network, no build). The build-free commands that the
#        three workflows run on every push are run against a temporary detached worktree
#        of the ref, so the judgement is of the REF, never of the ambient tree:
#
#          checks.yml/scripted-checks   tests/scripted/verify
#                                       tests/test-package-upload-guard.sh
#          checks.yml/yamllint          yamllint over `git ls-files '*.yml'`
#          quality-gates.yml/static-gates  gates 3, 4, 6, 7, 8, 9, 11 of tests/run-all-gates.sh
#          quality-gates.yml/mcp-bridge-python-tests
#                                       the bridge's two no-build unittest modules, with the
#                                       job's own "0 tests is an error" count check - the one
#                                       leg that runs a real test suite without a build
#
#        and one leg this fork's release path needs and no upstream job runs:
#
#          staging-path                 every release-package upload step in
#                                       .github/workflows/ is reachable only on a tag ref,
#                                       i.e. the release job is the only sanctioned way to
#                                       stage artefacts (a dispatch on a branch must not
#                                       stage a release package, and neither must a push).
#
# LEG 2  the ref's own CI evidence (--require-ci). The Actions API is asked for the PUSH
#        runs of the judged commit, and the required JOB SET is asserted at job
#        granularity — the seven platform jobs of build.yml, the three jobs of checks.yml
#        and quality-gates.yml's `static gates (3, 4, 6, 7, 8, 9, 11)`. A missing job, a
#        job still in flight, or a job whose conclusion is anything but `success`
#        (failure, cancelled, skipped, neutral, timed_out) refuses the ref. The workflow
#        conclusion alone is deliberately not enough: a workflow can be `success` while
#        the job that matters was skipped or never requested.
#
# WHY A SKIP IS A REFUSAL, NOT A PASS
# -----------------------------------
# `tests/run-all-gates.sh` exits 3 on pass-with-skips because a skipped gate is not a
# pass. For a release the same rule is stronger: an unmeasured ref is an unreleasable
# ref. So this script's exit 3 is a REFUSAL too, and every caller must treat any
# non-zero status as "do not cut the tag". There is no flag that turns a skip into a
# pass.
#
# Usage:
#   bash tests/release-ref-fitness.sh                              # judge the current worktree's HEAD (leg 1)
#   bash tests/release-ref-fitness.sh --ref release/0.3.0
#   bash tests/release-ref-fitness.sh --ref v0.3.0-alpha --require-ci [--repo owner/name]
#   bash tests/release-ref-fitness.sh --only g9-fork-sources       # one leg (diagnosis/self-test)
#   bash tests/release-ref-fitness.sh --list-legs
#   bash tests/release-ref-fitness.sh --keep                       # keep the materialised worktree
#
# Exit status:
#   0  FIT      — every required leg ran and passed
#   1  REFUSED  — at least one leg is red
#   2  usage / environment error (no such ref, not a git worktree, bad option)
#   3  REFUSED  — no leg is red, but at least one could not run (tool absent, ref not
#                 materialisable). Incomplete is not green.
#
# Every non-zero status is a refusal: the release path fails on any of them.
#
# Env: GH_TOKEN / GITHUB_TOKEN (only for --require-ci); RELEASE_FITNESS_REPO overrides
#      --repo; RELEASE_FITNESS_WORKTREE_DIR overrides where the ref is materialised.

set -uo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/.." && pwd)"

REF=""
REQUIRE_CI=0
REPO="${RELEASE_FITNESS_REPO:-${GITHUB_REPOSITORY:-}}"
KEEP=0
ONLY=""
LIST_LEGS=0
WT_BASE="${RELEASE_FITNESS_WORKTREE_DIR:-${TMPDIR:-/tmp}}"

usage() {
	sed -n '2,72p' "$HERE/release-ref-fitness.sh" | sed -n 's/^# \{0,1\}//p' | sed '/^$/q'
}

while [ $# -gt 0 ]; do
	case "$1" in
		--ref|--repo|--only)
			if [ $# -lt 2 ]; then echo "release-ref-fitness: $1 needs a value" >&2; exit 2; fi ;;
	esac
	case "$1" in
		--ref)         REF="${2:?--ref needs a git ref}";       shift 2 ;;
		--repo)        REPO="${2:?--repo needs owner/name}";     shift 2 ;;
		--only)        ONLY="${2:?--only needs a leg name}";     shift 2 ;;
		--require-ci)  REQUIRE_CI=1;                             shift ;;
		--keep)        KEEP=1;                                   shift ;;
		--list-legs)   LIST_LEGS=1;                              shift ;;
		-h|--help)     usage; exit 0 ;;
		*) echo "release-ref-fitness: unknown argument '$1'" >&2; exit 2 ;;
	esac
done

# ---------------------------------------------------------------------------
# The leg table. One leg per line: <name>|<what it mirrors>|<command, run in the ref>.
# Editing this table is the only way to change what a release needs, and the
# self-test (tests/test-release-ref-fitness.sh) asserts the table still refuses a ref
# that carries a defect, so a leg cannot be dropped silently.
# ---------------------------------------------------------------------------
LEGS=(
	"scripted-verify|checks.yml/scripted-checks|tests/scripted/verify"
	"package-upload-guard|checks.yml/scripted-checks|bash tests/test-package-upload-guard.sh"
	"yamllint|checks.yml/yamllint|bash -c 'for i in \$(git ls-files \"*.yml\"); do yamllint \$i; done'"
	"g3-no-tautology|quality-gates.yml/static-gates|bash tests/no-tautology-gate.sh"
	"g4-complexity|quality-gates.yml/static-gates|bash tests/complexity-gate.sh --check"
	"g4-complexity-tools|quality-gates.yml/static-gates|bash tests/complexity-gate.sh --check --scope tools"
	"g6-upstream-regression|quality-gates.yml/static-gates|bash tests/no-upstream-regression-gate.sh"
	"g7-file-length|quality-gates.yml/static-gates|bash tests/file-length-gate.sh --check"
	"g7-file-length-tools|quality-gates.yml/static-gates|bash tests/file-length-gate.sh --check --scope tools"
	"g8-duplication|quality-gates.yml/static-gates|bash tests/duplication-gate.sh"
	"g8-duplication-tools|quality-gates.yml/static-gates|bash tests/duplication-gate.sh --scope tools"
	"g9-fork-sources|quality-gates.yml/static-gates|bash tests/fork-sources-gate.sh"
	"g11-evidence|quality-gates.yml/static-gates|bash tests/evidence-gate.sh"
	"mcp-bridge-python-tests|quality-gates.yml/mcp-bridge-python-tests|bash tests/release-mcp-bridge-leg.sh"
	"staging-path|(this fork: REL-2 staging sanction)|bash tests/release-staging-path-gate.sh"
)

# The staging-path leg is implemented here rather than as a second file: it is a
# property of the workflow files, and the release path is the only consumer.
staging_path_leg() {
	bash "$HERE/release-staging-path-gate.sh"
}

if [ "$LIST_LEGS" -eq 1 ]; then
	for row in "${LEGS[@]}"; do printf '%s\t%s\n' "${row%%|*}" "${row#*|}"; done
	exit 0
fi

if [ -n "$ONLY" ]; then
	found=0
	for row in "${LEGS[@]}"; do [ "${row%%|*}" = "$ONLY" ] && found=1; done
	if [ "$found" -eq 0 ]; then
		echo "release-ref-fitness: no leg named '$ONLY' — see --list-legs" >&2
		exit 2
	fi
fi

# ---------------------------------------------------------------------------
# Materialise the ref. The judgement is of the REF's content, so the legs run in a
# detached temporary worktree of it; running them in the ambient tree would judge
# whatever the caller happens to have checked out (and would measure a lane's
# uncommitted work as if it were the release).
# ---------------------------------------------------------------------------
if [ -z "$REF" ]; then REF="HEAD"; fi
if ! git -C "$ROOT" rev-parse --git-dir >/dev/null 2>&1; then
	echo "release-ref-fitness: $ROOT is not a git worktree — nothing can be judged" >&2
	exit 2
fi
SHA="$(git -C "$ROOT" rev-parse --verify --quiet "${REF}^{commit}" 2>/dev/null)" || {
	echo "release-ref-fitness: no such ref: $REF" >&2
	exit 2
}
SHORT="${SHA:0:9}"
KIND="$(git -C "$ROOT" cat-file -t "$REF" 2>/dev/null || echo commit)"

WT="$WT_BASE/rel2-fitness-$SHORT-$$"
ADDED=0
cleanup() {
	if [ "$ADDED" -eq 1 ]; then
		if [ "$KEEP" -eq 1 ]; then
			echo "release-ref-fitness: kept worktree at $WT (--keep)"
		else
			git -C "$ROOT" worktree remove --force "$WT" >/dev/null 2>&1
		fi
	fi
}
trap cleanup EXIT

echo "=== release ref fitness: may this ref be released? ==="
echo "repo      : $ROOT"
echo "ref       : $REF ($KIND)"
echo "commit    : $SHA"
echo "worktree  : $WT (detached; removed on exit unless --keep)"
echo

if ! git -C "$ROOT" worktree add --detach "$WT" "$SHA" >/dev/null 2>&1; then
	echo "release-ref-fitness: cannot materialise $REF at $SHA in $WT" >&2
	echo "  (a release judgement that cannot be measured is a refusal, not a pass)" >&2
	exit 3
fi
ADDED=1

RED=0
INCOMPLETE=0
RAN=0
declare -a RED_LEGS=() INCOMPLETE_LEGS=()
printf '%-24s %-34s %s\n' "leg" "mirrors" "verdict"

# A leg whose tool is absent from the PATH cannot run: that is INCOMPLETE (exit 3),
# never a pass. `command -v bash` is not checked — the caller is bash.
leg_tool() { # leg_tool <command-string> -> first word, unless it is bash -c
	:
}

for row in "${LEGS[@]}"; do
	name="${row%%|*}"; rest="${row#*|}"; mirrors="${rest%%|*}"; cmd="${rest#*|}"
	[ -n "$ONLY" ] && [ "$name" != "$ONLY" ] && continue
	log="$(mktemp)"
	if [ "$name" = "staging-path" ]; then
		( cd "$WT" && staging_path_leg ) >"$log" 2>&1
		rc=$?
	else
		( cd "$WT" && bash -c "$cmd" ) >"$log" 2>&1
		rc=$?
	fi
	if [ "$rc" -eq 0 ]; then
		printf '%-24s %-34s %s\n' "$name" "$mirrors" "PASS"
	elif [ "$rc" -eq 127 ] || grep -qi 'command not found\|not found in PATH' "$log"; then
		printf '%-24s %-34s %s\n' "$name" "$mirrors" "INCOMPLETE (exit $rc)"
		INCOMPLETE=$((INCOMPLETE + 1)); INCOMPLETE_LEGS+=("$name|$log")
	else
		printf '%-24s %-34s %s\n' "$name" "$mirrors" "RED (exit $rc)"
		RED=$((RED + 1)); RED_LEGS+=("$name|$log")
	fi
	RAN=$((RAN + 1))
	if [ -n "$ONLY" ]; then
		echo "--- $name output ---"
		cat "$log"
		echo "--- end $name output ---"
	fi
done

if [ "$RAN" -eq 0 ]; then
	echo "release-ref-fitness: no leg was selected — that is an error, not a pass" >&2
	exit 2
fi

echo
if [ "$RED" -gt 0 ]; then
	echo "Failing legs (the cause is the first refusal line in each log):"
	for e in "${RED_LEGS[@]}"; do
		printf '  [RED] %s\n' "${e%%|*}"
		grep -m3 -E '^(FAIL|REGRESSION|Error|error:|release-staging-path)' "${e#*|}" | sed 's/^/        /' || true
	done
	echo
fi
if [ "$INCOMPLETE" -gt 0 ]; then
	echo "Legs that could not run (a skip is not a pass):"
	for e in "${INCOMPLETE_LEGS[@]}"; do printf '  [INCOMPLETE] %s\n' "${e%%|*}"; done
	echo
fi

# ---- leg 2: the ref's own CI evidence --------------------------------------
if [ "$REQUIRE_CI" -eq 1 ]; then
	bash "$HERE/release-ci-evidence-gate.sh" --sha "$SHA" ${REPO:+--repo "$REPO"}
	ci_rc=$?
	case "$ci_rc" in
		0) printf '%-24s %-34s %s\n' "ci-evidence" "build+checks+quality-gates" "PASS" ;;
		1) printf '%-24s %-34s %s\n' "ci-evidence" "build+checks+quality-gates" "RED (exit 1)"
		   RED=$((RED + 1)) ;;
		*) printf '%-24s %-34s %s\n' "ci-evidence" "build+checks+quality-gates" "INCOMPLETE (exit $ci_rc)"
		   INCOMPLETE=$((INCOMPLETE + 1)) ;;
	esac
fi

echo
if [ "$RED" -gt 0 ]; then
	echo "RESULT: REFUSED — $RED leg(s) red on $SHORT. Do not cut a tag from this ref."
	exit 1
fi
if [ "$INCOMPLETE" -gt 0 ]; then
	echo "RESULT: REFUSED (incomplete) — $INCOMPLETE leg(s) did not run on $SHORT."
	echo "An unmeasured ref is not a fit ref; see tests/QA-GATES.md."
	exit 3
fi
echo "RESULT: FIT — every required leg ran and passed on $SHORT ($REF)."
exit 0
