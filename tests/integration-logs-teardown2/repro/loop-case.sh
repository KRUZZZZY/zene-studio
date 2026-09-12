#!/usr/bin/env bash
# Pre-fix reproduction instrument (scratch, not part of the tree's tests).
# Runs N copies of one case in parallel loops, each loop sequential, and reports
# every unpiped exit code. Usage: bash loop-case.sh <binary> <case> <loops> <runs-per-loop> <logfile>
set -u
BIN="$1"; CASE="$2"; LOOPS="$3"; RUNS="$4"; LOG="$5"
: > "$LOG"
loop() {
  local tag="$1" p=0 f=0
  for i in $(seq 1 "$RUNS"); do
    "$BIN" "$CASE" > /dev/null 2>&1
    if [ $? -eq 0 ]; then p=$((p+1)); else f=$((f+1)); fi
  done
  echo "loop$tag passed=$p failed=$f" >> "$LOG"
}
pids=()
for n in $(seq 1 "$LOOPS"); do
  loop "L$n" &
  pids+=($!)
done
for pid in "${pids[@]}"; do wait "$pid"; done
awk -F'[= ]' '/passed=/{p+=$3; f+=$5} END{printf "TOTAL passed=%d failed=%d abort_rate=%.1f%%\n", p, f, 100*f/(p+f)}' "$LOG" | tee -a "$LOG"
