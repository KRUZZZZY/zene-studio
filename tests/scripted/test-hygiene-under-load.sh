#!/usr/bin/env bash
#
# test-hygiene-under-load.sh - run the ctest suite repeatedly and in parallel,
# the way the release verification meets it (sibling lanes compiling on the same
# box), and report every run's exit code and test counts.
#
# Why this exists. The suite used to abort in teardown -
#   QFATAL : <AnyEngineTest>::cleanupTestCase() QThread: Destroyed while thread is
#            still running     (SIGABRT, exit 134)
# - on whichever test happened to be running when a worker thread outlived its
# QThread object. A single green run proved nothing about that: it needed load.
# This script *is* the load condition in miniature (N ctest invocations in
# parallel, each -jJ), so a regression is a measured abort rate, not a story.
#
# Usage:
#   bash tests/scripted/test-hygiene-under-load.sh [rounds] [parallel] [j]
#     rounds    number of back-to-back rounds            (default 6)
#     parallel  ctest invocations per round, concurrent  (default 3)
#     j         ctest -j passed to each invocation       (default 2)
#
# Logs are written into the repository (logs/test-hygiene/<timestamp>/) - never
# into /tmp, which a disk reclaim has already destroyed once in this program.
#
# Exit codes: 0 = every run passed with 0 tests failed and no abort signature;
#             1 = at least one run failed or aborted; 2 = setup error.

set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
BUILD_TESTS="${BUILD_TESTS:-$ROOT/build/tests}"
ROUNDS="${1:-6}"
PARALLEL="${2:-3}"
CTEST_J="${3:-2}"

[[ -d "$BUILD_TESTS" ]] || { echo "error: no ctest directory at $BUILD_TESTS" >&2; exit 2; }
[[ -x "$BUILD_TESTS/ctest" || -n "$(command -v ctest)" ]] || { echo "error: ctest not found" >&2; exit 2; }

STAMP="$(date +%Y%m%d-%H%M%S)"
OUT="$ROOT/logs/test-hygiene/$STAMP"
mkdir -p "$OUT"
SUMMARY="$OUT/summary.tsv"
: > "$SUMMARY"
printf 'round\trun\texit\ttests_passed\ttests_failed\tout_of\tabort\n' >> "$SUMMARY"

echo "test-hygiene under load: rounds=$ROUNDS parallel=$PARALLEL ctest -j$CTEST_J"
echo "build/tests : $BUILD_TESTS"
echo "logs        : $OUT"

for r in $(seq 1 "$ROUNDS"); do
	pids=()
	for i in $(seq 1 "$PARALLEL"); do
		log="$OUT/r${r}-${i}.log"
		(
			cd "$BUILD_TESTS" || exit 2
			ctest -j"$CTEST_J" --output-on-failure > "$log" 2>&1
			rc=$?
			passed="$(grep -oE '[0-9]+% tests passed, [0-9]+ tests failed out of [0-9]+' "$log" | head -1)"
			p="$(sed -nE 's/.*, ([0-9]+) tests failed out of ([0-9]+).*/\1\t\2/p' <<< "$passed" | head -1)"
			nf="$(cut -f1 <<< "$p")"; no="$(cut -f2 <<< "$p")"
			[[ -n "$nf" && -n "$no" ]] || { nf=-1; no=-1; }
			np=$((no - nf))
			abort="no"
			grep -qE 'QThread: Destroyed while thread is still running|Received signal (6|11)|QFATAL' "$log" && abort="yes"
			printf '%s\t%s\t%s\t%s\t%s\t%s\t%s\n' "$r" "$i" "$rc" "$np" "$nf" "$no" "$abort" >> "$SUMMARY"
		) &
		pids+=($!)
	done
	for p in "${pids[@]}"; do wait "$p"; done
	echo "round $r done"
done

echo
echo "=== per-run results (round / run / exit / passed / failed / out of / abort) ==="
column -t "$SUMMARY"
echo
data="$(tail -n +2 "$SUMMARY")"
runs="$(grep -c "" <<< "$data")"
zero="$(grep -c $'\t0\t' <<< "$data")"
aborts="$(grep -c $'\tyes$' <<< "$data")"
bad_exit="$(grep -vc $'\t0\t' <<< "$data")"
echo "runs=$runs  runs_with_0_failed=$zero  runs_with_an_abort_signature=$aborts  runs_with_nonzero_exit=$bad_exit"

rc=0
[[ "$aborts" -eq 0 ]] || rc=1
[[ "$bad_exit" -eq 0 ]] || rc=1
[[ "$zero" -eq "$runs" ]] || rc=1
echo "verdict: $([[ $rc -eq 0 ]] && echo 'PASS - no abort, no failed run' || echo 'FAIL - see the table above')"
exit $rc
