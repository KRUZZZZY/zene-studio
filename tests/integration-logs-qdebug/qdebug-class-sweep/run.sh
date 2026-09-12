#!/usr/bin/env bash
# run.sh -- sweep the whole tree for the Qt5 "<QDebug> not included" compile-error class
# and fail if any translation unit still depends on a Qt6-only path for the class.
#
# Why this exists.  The v0.2.0-alpha tag's linux-x86_64 / linux-arm64 jobs died at
# src/core/ConfigManager.cpp:700 with
#
#   error: invalid use of incomplete type 'class QDebug'
#
# because <QtGlobal> only forward-declares QDebug and Qt5's widget headers never pull
# qdebug.h in.  A `make` build stops at the first offender, so one CI cycle names one
# file.  This sweep compiles every translation unit of the build under a Qt5 emulation
# (Qt6 headers with the seven qdebug.h includes removed -- Qt5.15's counterparts do not
# have them) and names all of them at once.
#
# Usage (from the repo root, with a configured build tree):
#   bash tests/integration-logs-qdebug/qdebug-class-sweep/run.sh [build-dir]
#
# Exit 0 only when the self-test passes AND no TU in the class fails.  Logs land in
# tests/integration-logs-qdebug/; exit codes are echoed unpiped (workspace rule 6).

set -uo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/../../.." && pwd)"
cd "$ROOT" || exit 2

BUILD_DIR="${1:-build}"
LOGS="$ROOT/tests/integration-logs-qdebug"
mkdir -p "$LOGS"

if [[ ! -f "$BUILD_DIR/compile_commands.json" ]]; then
	echo "error: $BUILD_DIR/compile_commands.json not found -- configure the tree first" >&2
	exit 2
fi

overall=0

echo "--- shadow: Qt6 headers minus the seven <QtCore/qdebug.h> includes ---"
python3 "$HERE/qdebug_emulate.py" --make-shadow "$BUILD_DIR/qt5-shadow" \
	> "$LOGS/00-shadow.log" 2>&1
rc=$?; echo "shadow EXIT=$rc"; cat "$LOGS/00-shadow.log"
[[ $rc -eq 0 ]] || overall=1

echo
echo "--- self-test: the emulation must reproduce the CI error ---"
python3 "$HERE/qdebug_emulate.py" --shadow "$BUILD_DIR/qt5-shadow" --build-dir "$BUILD_DIR" \
	--selftest > "$LOGS/01-selftest.log" 2>&1
rc=$?; echo "selftest EXIT=$rc"; cat "$LOGS/01-selftest.log"
[[ $rc -eq 0 ]] || overall=1

echo
echo "--- sweep: every TU in the build inside the class ---"
python3 "$HERE/qdebug_emulate.py" --shadow "$BUILD_DIR/qt5-shadow" --build-dir "$BUILD_DIR" \
	--all > "$LOGS/11-class-sweep.log" 2>&1
rc=$?; echo "sweep EXIT=$rc"
grep -E 'rc=[1-9]|^summary|NEEDS|INCONCLUSIVE' "$LOGS/11-class-sweep.log" || true
[[ $rc -eq 0 ]] || overall=1

echo
if [[ $overall -eq 0 ]]; then
	echo "PASS: the self-test reproduces the CI error and no TU in the class needs <QDebug>"
else
	echo "FAIL: see the logs above ($LOGS)"
fi
exit "$overall"
