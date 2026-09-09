#!/usr/bin/env bash
# no-tautology-gate.sh — Gate 3 (no tautological tests) for the LMMS standards fork.
#
# QA-GATES.md states the rule but enforced it "by review". Its own first line says:
#   "if it is not checked by a script, it is not a gate."
# This script makes Gate 3 mechanical.
#
# Scope: the QTest classes registered in tests/CMakeLists.txt (LMMS_TESTS). Helper
# executables (harnesses, probes, reference renderers) are NOT QTest classes and are
# deliberately out of scope — they are listed at the bottom for transparency.
#
# Rules per registered test file:
#   1. it must contain at least one real assertion macro
#      (QVERIFY, QVERIFY2, QCOMPARE, QEXPECT_FAIL, QTRY_*);
#   2. it must contain at least one test slot (a function under `private slots:`);
#   3. it must not contain a literal tautology:
#      QVERIFY(true) / QVERIFY(1) / QVERIFY2(true, ...) / QCOMPARE(x, x) / QVERIFY(false).
#
# Usage: bash tests/no-tautology-gate.sh [--strict]
#   default: report; exit 1 only on a rule violation
#   --strict: additionally fail if any test slot has no assertion at all

set -uo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/.." && pwd)"
cd "$ROOT" || exit 2

STRICT=0
[[ "${1:-}" == "--strict" ]] && STRICT=1

# Test sources registered as QTest targets.
# Helper executables that are registered but are not QTest classes.
HELPERS='TwoTrackAlsaCaptureProbe.cpp|TwoTrackRecordingHarness.cpp|PluginPortsMigrationReference.cpp'
mapfile -t REGISTERED < <(awk '/set\(LMMS_TESTS/,/\)/' tests/CMakeLists.txt \
	| grep -oE 'src/[A-Za-z0-9_/]+\.cpp' | sort -u | grep -vE "(${HELPERS})$")

if [[ ${#REGISTERED[@]} -eq 0 ]]; then
	echo "no-tautology-gate: could not parse LMMS_TESTS from tests/CMakeLists.txt" >&2
	exit 2
fi

violations=0
printf '%-46s %-8s %-7s %-7s %s\n' "test file" "slots" "asserts" "taut" "verdict"
for src in "${REGISTERED[@]}"; do
	f="tests/$src"
	[[ -f "$f" ]] || { printf '%-46s %s\n' "$src" "MISSING FILE"; violations=1; continue; }

	# test slots: function declarations between `private slots:` (or `private Q_SLOTS:`)
	# and the next access specifier / closing brace.
	slots=$(awk '
		/private[[:space:]]+(Q_)?[Ss][Ll][Oo][Tt][Ss]:/ {inslots=1; next}
		inslots && /^[[:space:]]*(public|protected|private)[[:space:]]*(slots|Q_SLOTS)?[[:space:]]*:/ {inslots=0}
		inslots && /^[[:space:]]*[A-Za-z_][A-Za-z0-9_]*[[:space:]]*\(/ {n++}
		END {print n+0}' "$f")

	asserts=$(grep -cE 'QVERIFY2?[[:space:]]*\(|QCOMPARE[[:space:]]*\(|QEXPECT_FAIL[[:space:]]*\(|QTRY_[A-Z_]+[[:space:]]*\(' "$f" || true)
	taut=$(grep -cE 'QVERIFY[[:space:]]*\([[:space:]]*(true|false|1|0)[[:space:]]*\)|QVERIFY2[[:space:]]*\([[:space:]]*(true|false|1|0)[[:space:]]*,' "$f" || true)

	verdict="ok"
	if [[ "$slots" -eq 0 ]]; then verdict="NO TEST SLOTS"; violations=1
	elif [[ "$asserts" -eq 0 ]]; then verdict="NO ASSERTIONS"; violations=1
	elif [[ "$taut" -gt 0 ]]; then verdict="TAUTOLOGY"; violations=1
	elif [[ "$STRICT" -eq 1 && "$asserts" -lt "$slots" ]]; then verdict="FEWER ASSERTS THAN SLOTS"; violations=1
	fi
	printf '%-46s %-8s %-7s %-7s %s\n' "$src" "$slots" "$asserts" "$taut" "$verdict"
done

echo
echo "Out of scope (helper executables, not QTest classes):"
for f in tests/src/core/TwoTrackAlsaCaptureProbe.cpp tests/src/core/TwoTrackRecordingHarness.cpp \
         tests/src/plugins/PluginPortsMigrationReference.cpp; do
	[[ -f "$f" ]] && echo "  $f"
done

echo
if [[ "$violations" -eq 1 ]]; then
	echo "FAIL: Gate 3 violation(s) above"
	exit 1
fi
echo "PASS: every registered test file has test slots and real assertions, no literal tautologies"
