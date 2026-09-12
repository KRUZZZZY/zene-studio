#!/usr/bin/env bash
set -uo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
REAL="${ZENE_CONTROL_REAL_BINARY:-$ROOT/build-release/zene}"
LOG="${ZENE_GDB_LOG:-$HERE/gdb-model.log}"
exec gdb -batch -nx -x "$HERE/gdb-model.gdb" --args "$REAL" "$@" > "$LOG" 2>&1
