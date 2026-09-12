#!/usr/bin/env bash
# gdb wrapper for the release-configuration undo repro. The harness starts an
# instance with `Popen([binary, --config, ...])`, so pointing ZENE_CONTROL_BINARY
# at this script makes the instance run under gdb and captures the signal, the
# crash frame and every thread's backtrace.
#
#   ZENE_CONTROL_REAL_BINARY=$PWD/build-release/zene \
#   ZENE_GDB_LOG=$PWD/tests/integration-logs-undo-rel/gdb-undo-release-bt.log \
#   ZENE_CONTROL_BINARY=$PWD/tests/integration-logs-undo-rel/gdb-wrapper.sh \
#     python3 tests/integration-logs-undo-rel/repro-undo-release.py under-gdb
set -uo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
REAL="${ZENE_CONTROL_REAL_BINARY:-$ROOT/build/zene}"
LOG="${ZENE_GDB_LOG:-$HERE/gdb-undo-release-bt.log}"
exec gdb -batch -nx \
	-ex "set pagination off" \
	-ex "set confirm off" \
	-ex "set print frame-arguments all" \
	-ex "run" \
	-ex "echo \n===== SIGNAL / STOP REASON =====\n" \
	-ex "info program" \
	-ex "echo \n===== CURRENT THREAD =====\n" \
	-ex "thread" \
	-ex "echo \n===== CRASH FRAME =====\n" \
	-ex "bt 40" \
	-ex "frame 0" \
	-ex "info line" \
	-ex "info locals" \
	-ex "info registers rip" \
	-ex "echo \n===== BACKTRACE (all threads, 40 frames) =====\n" \
	-ex "thread apply all bt 40" \
	-ex "quit" \
	--args "$REAL" "$@" > "$LOG" 2>&1
