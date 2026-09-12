#!/usr/bin/env bash
# Acceptance instrument: the whole suite N times, exit codes captured unpiped,
# two of the three runs under deliberate interference (sibling work on the box is
# what the release verification actually sees). Logs land in this directory.
#
# Usage: bash suite-runs.sh <repo-root> <logdir>
set -u
REPO="$1"; LOGDIR="$2"
BIN="$REPO/build/tests/RenderJobQueueTest"
mkdir -p "$LOGDIR"

run_suite() { # <tag> <logfile>
  local tag="$1" log="$2"
  local t0
  t0=$(date +%s)
  ctest --test-dir "$REPO/build/tests" -j2 --output-on-failure > "$log" 2>&1
  local ec=$?
  local t1
  t1=$(date +%s)
  {
    echo "=== ctest run $tag (unpiped) ==="
    echo "EXIT=$ec   wall=$((t1 - t0))s   $(grep -E '^[0-9]+% tests passed' "$log" | tail -1)"
    echo "aborts (QFATAL/QThread: Destroyed/SIGABRT): $(grep -cE 'QFATAL|QThread: Destroyed|Received signal 6' "$log")"
    echo "test lines: $(grep -cE '^ *[0-9]+/[0-9]+ Test' "$log")"
  } | tee -a "$LOGDIR/suite2-runs-summary.txt"
}

: > "$LOGDIR/suite2-runs-summary.txt"

run_suite "1-alone" "$LOGDIR/suite2-run-1-alone.log"

# interference A: four parallel loops of the very case under test
pids=()
for n in 1 2 3 4; do
  ( for _ in $(seq 1 60); do "$BIN" poolModeStillRunsEveryJobExactlyOnce >/dev/null 2>&1; done ) &
  pids+=($!)
done
run_suite "2-four-parallel-case-loops" "$LOGDIR/suite2-run-2-interference-loops.log"
for p in "${pids[@]}"; do kill "$p" 2>/dev/null; done

# interference B: an oversubscribed box (40 busy loops on 20 cores)
hogs=()
for _ in $(seq 1 40); do ( while :; do :; done ) & hogs+=($!); done
run_suite "3-forty-busy-loops" "$LOGDIR/suite2-run-3-interference-hogs.log"
for p in "${hogs[@]}"; do kill "$p" 2>/dev/null; done
wait 2>/dev/null

echo "=== suite-runs: DONE ===" | tee -a "$LOGDIR/suite2-runs-summary.txt"
