#!/usr/bin/env bash
# run.sh -- reproduce the linux-x86_64 / linux-arm64 compile error of the
# v0.2.0-alpha tag and show the include that clears it.
#
#   src/core/ConfigManager.cpp:700:33: error: invalid use of incomplete type
#   'class QDebug'
#
# The statement is `qWarning() << title << message;` in saveConfigFile()'s
# unattended branch (the #625 headless-load divergence). <QtGlobal> declares the
# class and the qWarning() macro but only <QDebug> defines QDebug, so whether the
# expression compiles depends on which other Qt headers happen to be included
# first -- Qt5's widget headers do not bring QDebug in, Qt6's <QApplication>
# does, which is why this box (Qt6 6.4.2) built the same file while the CI's Qt5
# jobs could not.
#
# Two compilations of one probe with the same Qt headers, differing only in
# whether the fix's include is present:
#
#   without-qdebug  -> must FAIL with the CI's error text
#   with-qdebug     -> must compile
#
# The probe is generated into ./out/gen/ so tests/ carries no standalone
# translation unit. Qt5 is not installed here (the CI jobs use /opt/qt5*); the
# probe therefore reproduces the error CLASS with the minimal include set rather
# than the job's exact preprocessed input.
#
# Exit 0 only when both expectations hold.

set -uo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
WORKTREE="$(cd "$HERE/../../.." && pwd)"
GEN="$HERE/out/gen"
mkdir -p "$GEN"

CXX="${CXX:-g++}"
failures=0

report() {
	printf '%-16s %-46s %-16s %s\n' "$1" "$2" "$3" "$4"
	[[ "$4" == "ok" ]] || failures=$((failures + 1))
}

if ! pkg-config --exists Qt6Core; then
	echo "error: this probe needs Qt6Core (pkg-config Qt6Core)" >&2
	exit 2
fi
QT_CFLAGS=$(pkg-config --cflags Qt6Core)

cat > "$GEN/qdebug-incomplete.cpp" <<'PROBE_EOF'
// The statement from src/core/ConfigManager.cpp:700, with the include set
// reduced to what the compiler needs to reach it. -DHAVE_QDEBUG adds the line
// the fix adds.
#include <QtCore/qglobal.h>
#include <QString>
#ifdef HAVE_QDEBUG
#include <QDebug>
#endif

void probe() { QString title = QStringLiteral("t"), message = QStringLiteral("m"); qWarning() << title << message; }
PROBE_EOF

FAILMSG="invalid use of incomplete type"

"$CXX" -std=c++17 -fPIC $QT_CFLAGS -c "$GEN/qdebug-incomplete.cpp" -o "$GEN/without-qdebug.o" \
	> "$GEN/without-qdebug.log" 2>&1
rc=$?
if [[ $rc -eq 0 ]]; then
	report without-qdebug "must fail with the CI error" "compile exit 0" "FAIL: no error produced"
elif grep -q "$FAILMSG" "$GEN/without-qdebug.log"; then
	report without-qdebug "must fail with the CI error" "compile exit $rc" "ok"
else
	report without-qdebug "must fail with the CI error" "compile exit $rc, other" "FAIL: wrong error"
fi

"$CXX" -std=c++17 -fPIC -DHAVE_QDEBUG $QT_CFLAGS -c "$GEN/qdebug-incomplete.cpp" -o "$GEN/with-qdebug.o" \
	> "$GEN/with-qdebug.log" 2>&1
rc=$?
if [[ $rc -eq 0 ]]; then
	report with-qdebug "must compile" "compile exit 0" "ok"
else
	report with-qdebug "must compile" "compile exit $rc" "FAIL"
fi

echo
if [[ $failures -eq 0 ]]; then
	echo "PASS: the missing <QDebug> reproduces the CI error, the include clears it"
	exit 0
fi
echo "FAIL: $failures expectation(s) unmet"
exit 1
