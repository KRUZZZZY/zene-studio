#!/usr/bin/env bash
# Reproduce the ControlShutdown abort (the fourth site of the class: QThread
# destroyed while running, on the product's shutdown path with an audio device
# that cannot open) and capture the offending thread's stack with the
# abort-trace shim. Bounded, and it reports every attempt's exit code.
#
# Usage: bash repro-control-shutdown-abort.sh <repo-root> <runs>
set -u
REPO="$1"; RUNS="${2:-8}"
LOGDIR="$REPO/tests/integration-logs-teardown2/repro"
cd "$REPO" || exit 1

gcc -shared -fPIC -O1 -g -o "$LOGDIR/abort-trace.so" "$LOGDIR/abort-trace.c" -ldl || exit 1

hogs=()
for _ in $(seq 1 40); do ( while :; do :; done ) & hogs+=($!); done

: > "$LOGDIR/control-shutdown-abort.log"
for i in $(seq 1 "$RUNS"); do
  : > /tmp/abort-trace-run.txt
  LD_PRELOAD="$LOGDIR/abort-trace.so" ABORT_TRACE_FILE=/tmp/abort-trace-run.txt \
    python3 tests/control-shutdown.py "$REPO/build/zene" \
      "$REPO/tests/data/agent-control-fixture.mmp" \
      > "$LOGDIR/control-shutdown-run-$i.log" 2>&1
  ec=$?
  echo "run$i EXIT=$ec" >> "$LOGDIR/control-shutdown-abort.log"
  if [ -s /tmp/abort-trace-run.txt ]; then
    echo "--- abort trace from run$i ---" >> "$LOGDIR/control-shutdown-abort.log"
    cat /tmp/abort-trace-run.txt >> "$LOGDIR/control-shutdown-abort.log"
    cp "$LOGDIR/control-shutdown-run-$i.log" "$LOGDIR/control-shutdown-failing-run.log"
    break
  fi
done

for p in "${hogs[@]}"; do kill "$p" 2>/dev/null; done
wait 2>/dev/null
rm -f /tmp/abort-trace-run.txt
echo "done; see $LOGDIR/control-shutdown-abort.log"
