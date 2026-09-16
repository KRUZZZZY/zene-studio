#!/usr/bin/env bash
# release-ci-evidence-gate.sh — does THIS COMMIT's own CI evidence cover the release?
# REL-2 (2026-09-15).
#
# WHY THIS EXISTS
# ---------------
# `release-honesty-gate.sh --tag-run` asks a workflow-level question: is there a completed
# push run of build.yml / checks.yml / quality-gates.yml for this commit that is not red?
# A workflow conclusion is not coverage. Measured on the product repo, build run
# 34870198514 @ f611c888b (`release/0.3.0`):
#
#   mingw64       success        linux-x86_64  failure       macos-x86_64  failure
#   windows-arm64 success        linux-arm64   failure       macos-arm64   failure
#                                msvc-x64      failure       release gate  skipped
#
# 5 of the 7 platform jobs are red and the release gate recorded `skipped`. A check that
# reads "did some job finish" answers a different question from "did the seven-platform
# matrix pass", and the release path needs the second one.
#
# WHAT IT ASSERTS, at JOB granularity
# -----------------------------------
# For the commit under judgement, and for the three workflows that run on every push:
#
#   1. the workflow has at least one COMPLETED push run for this commit (none = refuse);
#   2. no completed push run is red (a conclusion other than `success` — including
#      `cancelled` and `skipped`, which are not results a release may be cut from);
#   3. every REQUIRED JOB below is present and concluded `success` in at least one of
#      those runs.
#
# The required set is the thing this gate exists to keep complete, so it is declared here
# and a rename is a RED this gate reports rather than a silent narrowing:
#
#   build.yml           the seven platform jobs (linux-x86_64, linux-arm64, macos-x86_64,
#                       macos-arm64, mingw64, msvc-x64, windows-arm64)
#   checks.yml          scripted-checks, shellcheck, yamllint
#   quality-gates.yml   static gates (3, 4, 6, 7, 8, 9, 11)
#
# Usage:
#   bash tests/release-ci-evidence-gate.sh --sha <commit> [--repo owner/name]
#   bash tests/release-ci-evidence-gate.sh --runs-tsv FILE      # replay a recorded capture
#   bash tests/release-ci-evidence-gate.sh --dump-required
#
#   --runs-tsv FILE reads rows `<workflow-basename>\t<job name>\t<status>\t<conclusion>`
#   instead of calling the API, plus one `<workflow>\t<run>\t<status>\t<conclusion>` row
#   per run. This is the offline/rehearsal form: the self-test drives it with fixtures, and
#   a recorded live capture (gh api, saved verbatim) can be replayed by anyone. It never
#   invents a row.
#
# Exit codes: 0 = covered, 1 = refused (missing/red/in-flight), 2 = usage error,
#             3 = the API or its client is unavailable (unmeasured is a refusal).
#
# Env: GH_TOKEN / GITHUB_TOKEN for the API; RELEASE_CI_REPO overrides the repository.

set -uo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/.." && pwd)"

SHA=""
REPO="${RELEASE_CI_REPO:-${GITHUB_REPOSITORY:-}}"
RUNS_TSV=""
DUMP=0

usage() {
	sed -n '2,48p' "$HERE/release-ci-evidence-gate.sh" | sed -n 's/^# \{0,1\}//p' | sed '/^$/q'
}

while [ $# -gt 0 ]; do
	case "$1" in
		--sha|--repo|--runs-tsv)
			if [ $# -lt 2 ]; then echo "release-ci-evidence-gate: $1 needs a value" >&2; exit 2; fi ;;
	esac
	case "$1" in
		--sha)        SHA="${2:?--sha needs a commit}";    shift 2 ;;
		--repo)       REPO="${2:?--repo needs owner/name}"; shift 2 ;;
		--runs-tsv)   RUNS_TSV="${2:?--runs-tsv needs a file}"; shift 2 ;;
		--dump-required) DUMP=1; shift ;;
		-h|--help)    usage; exit 0 ;;
		*) echo "release-ci-evidence-gate: unknown argument '$1'" >&2; exit 2 ;;
	esac
done

# One required JOB per line: <workflow file><TAB><job display name>. ONE PER LINE, not a
# space-separated list per workflow, because a job display name can itself contain spaces
# and commas (`static gates (3, 4, 6, 7, 8, 9, 11)`) and a word-split would turn it into
# nine pseudo-jobs that are then all reported missing.
REQUIRED="build.yml	linux-x86_64
build.yml	linux-arm64
build.yml	macos-x86_64
build.yml	macos-arm64
build.yml	mingw64
build.yml	msvc-x64
build.yml	windows-arm64
checks.yml	scripted-checks
checks.yml	shellcheck
checks.yml	yamllint
quality-gates.yml	static gates (3, 4, 6, 7, 8, 9, 11)"

if [ "$DUMP" -eq 1 ]; then
	printf '%s\n' "$REQUIRED"
	exit 0
fi

[ -n "$SHA" ] || { echo "release-ci-evidence-gate: --sha <commit> is required (or --runs-tsv FILE)" >&2; exit 2; }

# ---- obtain the rows: live API, or a supplied capture ----------------------
FETCHED=""
if [ -n "$RUNS_TSV" ]; then
	[ -f "$RUNS_TSV" ] || { echo "release-ci-evidence-gate: no such capture: $RUNS_TSV" >&2; exit 2; }
	ROWS="$(cat "$RUNS_TSV")"
	SRC="capture: $RUNS_TSV"
else
	if ! command -v gh >/dev/null 2>&1; then
		echo "release-ci-evidence-gate: 'gh' is not on PATH — the ref is unmeasured, which is a refusal" >&2
		exit 3
	fi
	if [ -z "$REPO" ]; then
		url="$(git -C "$ROOT" remote get-url product 2>/dev/null || true)"
		case "$url" in
			https://github.com/*/*) REPO="${url#https://github.com/}"; REPO="${REPO%.git}" ;;
			git@github.com:*/*)     REPO="${url#git@github.com:}";     REPO="${REPO%.git}" ;;
			*) echo "release-ci-evidence-gate: cannot determine the repository — pass --repo owner/name" >&2; exit 2 ;;
		esac
	fi
	echo "=== release CI evidence: this commit's own push runs, at job granularity ==="
	echo "repo   : $REPO"
	echo "commit : $SHA"
	echo
	RUN_IDS="$(gh api "repos/$REPO/actions/runs?head_sha=$SHA&per_page=100" --paginate \
		-q '.workflow_runs[] | select(.event=="push") | select(.path|test("/(build|checks|quality-gates)\\.yml$")) | [.path,.id,.status,.conclusion] | @tsv' 2>&1)" || {
		echo "release-ci-evidence-gate: the Actions API call failed — unmeasured, refused:" >&2
		printf '%s\n' "$RUN_IDS" >&2
		exit 3
	}
	if [ -z "$RUN_IDS" ]; then
		ROWS=""
	else
		ROWS=""
		while IFS=$'\t' read -r path id status conclusion; do
			[ -n "${id:-}" ] || continue
			base="${path##*/}"
			# A run with no jobs yet contributes no rows; its absence is caught below.
			jobs="$(gh api "repos/$REPO/actions/runs/$id/jobs?per_page=100" --paginate \
				-q --arg wf "$base" '.jobs[] | [$wf, .name, .status, (.conclusion // "-")] | @tsv' 2>/dev/null || true)"
			if [ -n "$jobs" ]; then
				ROWS="$ROWS$jobs"$'\n'
			fi
			# The run's own conclusion is a row of its own (job-less runs still count as
			# a result, and a skipped/ cancelled run must be visible as such).
			ROWS="$ROWS$base"$'\t'"<run>"$'\t'"$status"$'\t'"${conclusion:--}"$'\n'
		done <<< "$RUN_IDS"
	fi
	SRC="Actions API"
fi

echo "source : $SRC"
echo

# ---- assert ---------------------------------------------------------------
FAILED=0
printf '  %-18s %-30s %s\n' "workflow" "required job" "verdict"

while IFS=$'	' read -r wf job; do
	[ -n "${wf:-}" ] || continue
	verdict="MISSING — this commit has no such job in any push run of $wf"
	had_run=0
	while IFS=$'	' read -r rwf rjob rstatus rconcl; do
		[ -n "${rwf:-}" ] || continue
		[ "$rwf" = "$wf" ] || continue
		# `<run>` rows are the workflow's own conclusion, not a job's; they are asserted
		# by the whole-run loop below and must not overwrite a job-level verdict here (a
		# red run would otherwise mask the jobs inside it that did succeed).
		[ "$rjob" = "<run>" ] && continue
		had_run=1
		if [ "$rjob" = "$job" ]; then
			if [ "$rstatus" != "completed" ]; then
				verdict="IN FLIGHT ($rstatus) — not a result; the release waits for it"
			elif [ "$rconcl" = "success" ]; then
				verdict="ok (success)"
			else
				verdict="RED ($rconcl)"
			fi
		fi
	done <<< "$ROWS"
	if [ "$had_run" -eq 0 ]; then
		verdict="MISSING — no push run of $wf exists for this commit"
	fi
	printf '  %-18s %-30s %s\n' "$wf" "$job" "$verdict"
	[ "$verdict" = "ok (success)" ] || FAILED=$((FAILED + 1))
done <<< "$REQUIRED"

# A completed push run that is red anywhere refuses the commit even if every required job
# is green in another run — the same rule release-honesty-gate.sh --tag-run applies, kept
# here at job granularity rather than relaxed.
while IFS=$'\t' read -r rwf rjob rstatus rconcl; do
	[ -n "${rwf:-}" ] || continue
	[ "$rjob" = "<run>" ] || continue
	if [ "$rstatus" = "completed" ] && [ "$rconcl" != "success" ]; then
		printf '  %-18s %-30s %s\n' "$rwf" "<whole run>" "RED (this commit has a completed $rwf push run concluding $rconcl)"
		FAILED=$((FAILED + 1))
	fi
done <<< "$ROWS"

echo
if [ "$FAILED" -gt 0 ]; then
	echo "RESULT: REFUSED — $FAILED gap(s) in this commit's own CI coverage."
	echo "A tag is cut from a commit whose own seven-platform matrix, checks and static"
	echo "gates all concluded success. Re-run them on this commit first."
	exit 1
fi
echo "RESULT: PASS — every required job of build.yml, checks.yml and quality-gates.yml"
echo "        concluded success on this commit's own push runs."
exit 0
