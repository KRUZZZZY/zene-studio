#!/usr/bin/env bash
# Pre/post-fix reproduction instrument (scratch, not part of the tree's tests).
#
# The abort needs the two threads that increment the test's shared job counter to
# be PREEMPTED between the load and the store of `++m_total`. That needs an
# oversubscribed box, which is what the release verification has (sibling lanes
# compile on it). This script creates that condition explicitly: HOGS busy loops
# on NPROC cores, then RUNS sequential invocations of one case, each with its
# unpiped exit code counted.
#
# Usage: bash repro-under-load.sh <binary> <case> <hogs> <runs> <logfile>
set -u
BIN="$1"; CASE="$2"; HOGS="$3"; RUNS="$4"; LOG="$5"
: > "$LOG"

hogs=()
for _ in $(seq 1 "$HOGS"); do
  ( while :; do :; done ) &
  hogs+=($!)
done
echo "hogs=$HOGS a=$(nproc) load_before=$(cut -d' ' -f1 /proc/loadavg)" >> "$LOG"

pass=0; fail=0
for i in $(seq 1 "$RUNS"); do
  "$BIN" "$CASE" > /dev/null 2>&1
  ec=$?
  echo "run$i EXIT=$ec" >> "$LOG"
  if [ $ec -eq 0 ]; then pass=$((pass+1)); else fail=$((fail+1)); fi
done

for pid in "${hogs[@]}"; do kill "$pid" 2>/dev/null; done
wait 2>/dev/null
printf 'TOTAL passed=%d failed=%d out of %d (abort rate %.1f%%)\n' \
  "$pass" "$fail" "$RUNS" "$(awk -v f=$fail -v n=$RUNS 'BEGIN{print 100*f/n}')" | tee -a "$LOG"
