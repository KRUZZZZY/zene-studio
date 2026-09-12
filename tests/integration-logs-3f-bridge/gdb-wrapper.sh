#!/usr/bin/env bash
# gdb wrapper for the undo crash probe. The harness starts an instance with
# `Popen([binary, --config, ..])`; pointing ZENE_CONTROL_BINARY at this script makes
# that instance run under gdb so the SIGSEGV's backtrace is captured.
#
#   ZENE_CONTROL_BINARY=$PWD/tests/integration-logs-3f-bridge/gdb-wrapper.sh \
#     python3 tests/integration-logs-3f-bridge/probe-undo.py
#
# Set ZENE_CONTROL_REAL_BINARY to the built product binary; default is build/zene
# of this worktree. The backtrace lands in gdb-undo-bt.log next to this script.
set -uo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
REAL="${ZENE_CONTROL_REAL_BINARY:-$ROOT/build/zene}"
LOG="$HERE/gdb-undo-bt.log"
exec gdb -batch -nx \
	-ex "set pagination off" \
	-ex "set confirm off" \
	-ex "run" \
	-ex "echo \n===== BACKTRACE (all threads) =====\n" \
	-ex "thread apply all bt 25" \
	-ex "quit" \
	--args "$REAL" "$@" > "$LOG" 2>&1
