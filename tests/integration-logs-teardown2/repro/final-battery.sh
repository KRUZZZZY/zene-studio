#!/usr/bin/env bash
# Final acceptance battery, re-run after the MidiAlsaSeq fix (product code changed, so
# every earlier measurement has to be repeated on the new binaries). Exit codes are
# captured unpiped everywhere. Usage: bash final-battery.sh <repo-root>
set -u
REPO="$1"
cd "$REPO" || exit 1
L="$REPO/tests/integration-logs-teardown2"
BIN="$REPO/build/tests/RenderJobQueueTest"

# --- 1. the case, 50 sequential runs -------------------------------------------------
: > "$L/post-fix2-case-50.log"
p=0; f=0
for i in $(seq 1 50); do
  "$BIN" poolModeStillRunsEveryJobExactlyOnce >/dev/null 2>&1
  ec=$?; echo "run$i EXIT=$ec" >> "$L/post-fix2-case-50.log"
  if [ $ec -eq 0 ]; then p=$((p+1)); else f=$((f+1)); fi
done
echo "case poolModeStillRunsEveryJobExactlyOnce: passed=$p failed=$f out of 50" | tee -a "$L/post-fix2-case-50.log"

# --- 2. the whole test binary, 40 sequential runs ------------------------------------
: > "$L/post-fix2-allcases-40.log"
p=0; f=0
for i in $(seq 1 40); do
  "$BIN" >/dev/null 2>&1
  ec=$?; echo "run$i EXIT=$ec (all 10 cases)" >> "$L/post-fix2-allcases-40.log"
  if [ $ec -eq 0 ]; then p=$((p+1)); else f=$((f+1)); fi
done
echo "whole RenderJobQueueTest binary: passed=$p failed=$f out of 40" | tee -a "$L/post-fix2-allcases-40.log"

# --- 3. ControlShutdown (the fourth site), 6 runs under load -------------------------
hogs=()
for _ in $(seq 1 40); do ( while :; do :; done ) & hogs+=($!); done
: > "$L/post-fix2-control-shutdown-6.log"
p=0; f=0
for i in $(seq 1 6); do
  python3 tests/control-shutdown.py "$REPO/build/zene" \
      "$REPO/tests/data/agent-control-fixture.mmp" > "$L/control-shutdown-postfix-run-$i.log" 2>&1
  ec=$?; echo "run$i EXIT=$ec" >> "$L/post-fix2-control-shutdown-6.log"
  if [ $ec -eq 0 ]; then p=$((p+1)); else f=$((f+1)); fi
done
echo "ControlShutdown under 40 busy loops: passed=$p failed=$f out of 6" | tee -a "$L/post-fix2-control-shutdown-6.log"
for pid in "${hogs[@]}"; do kill "$pid" 2>/dev/null; done
wait 2>/dev/null

# --- 4. the render, twice ------------------------------------------------------------
for n in 1 2; do
  bash tools/mmpz-git/render-recipe.sh data/projects/shorties/sv-DnB-Startup.mmpz \
      -o "$L/render-teardown2-postfix-$n.wav" > "$L/render-postfix-$n.log" 2>&1
  echo "render-postfix-$n EXIT=$?"
done
sha256sum "$L/render-teardown2-postfix-1.wav" "$L/render-teardown2-postfix-2.wav" | tee "$L/render-postfix-hashes.txt"

echo "=== final battery: DONE ==="
