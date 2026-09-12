#!/usr/bin/env bash
# gdb wrapper for the undo crash probe (this lane's copy of the bridge lane's
# wrapper, writing its log next to this script).
#
# The harness starts an instance with `Popen([binary, --config, ...])`; pointing
# ZENE_CONTROL_BINARY at this script makes that instance run under gdb so the
# SIGSEGV, its backtrace, and the crash-state locals are captured.
#
#   ZENE_CONTROL_BINARY=$PWD/tests/integration-logs-3f-undo/gdb-wrapper.sh \
#     python3 tests/integration-logs-3f-undo/capture-undo.py
#
# Set ZENE_CONTROL_REAL_BINARY to the built product binary; default is build/zene.
set -uo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
REAL="${ZENE_CONTROL_REAL_BINARY:-$ROOT/build/zene}"
LOG="${ZENE_GDB_LOG:-$HERE/gdb-undo-bt.log}"
exec gdb -batch -nx \
	-ex "set pagination off" \
	-ex "set confirm off" \
	-ex "run" \
	-ex "echo \n===== SIGNAL / STOP REASON =====\n" \
	-ex "info program" \
	-ex "echo \n===== CRASH FRAME =====\n" \
	-ex "frame 0" \
	-ex "info line" \
	-ex "info locals" \
	-ex "echo \n--- the loop counter and the count it is bounded by ---\n" \
	-ex "p i" \
	-ex "p curPattern" \
	-ex "p numOfPatterns()" \
	-ex "echo \n===== BACKTRACE (all threads, 16 frames) =====\n" \
	-ex "thread apply all bt 16" \
	-ex "quit" \
	--args "$REAL" "$@" > "$LOG" 2>&1
