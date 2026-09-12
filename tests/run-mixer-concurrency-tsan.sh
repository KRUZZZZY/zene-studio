#!/usr/bin/env bash
# run-mixer-concurrency-tsan.sh — per-slot ThreadSanitizer evidence for
# tests/src/core/MixerConcurrencyTest.cpp (post-alpha/mixer-concurrency lane).
#
# Runs every slot of MixerConcurrencyTest on its own, so a crash in one slot
# cannot mask the others, and records for each: the exit code, the PASS/FAIL
# verdict, the MIXCONC_* evidence line and every ThreadSanitizer report summary.
#
# Usage:
#   cmake -S . -B build-tsan -DCMAKE_BUILD_TYPE=Debug -DWANT_DEBUG_TSAN=ON -DWANT_QT6=ON -DUSE_WERROR=OFF
#   cmake --build build-tsan --target MixerConcurrencyTest -j4
#   bash tests/run-mixer-concurrency-tsan.sh build-tsan [<out-dir>]
#
# `setarch -R` is required on kernels whose ASLR entropy (vm.mmap_rnd_bits) is
# too high for libtsan's shadow layout, which otherwise aborts with
# "FATAL: ThreadSanitizer: unexpected memory mapping" before the test starts.
#
# Exit status is always 0: this is an evidence collector, not a gate. The
# summary it prints (and writes to <out-dir>/SUMMARY.txt) is the deliverable.
set -uo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/.." && pwd)"
BUILD="${1:?usage: $0 <build-dir> [<out-dir>]}"
OUT="${2:-/tmp/mixer-concurrency-tsan}"
BIN="$ROOT/$BUILD/tests/MixerConcurrencyTest"
SUPP="$HERE/mixer-concurrency-tsan.supp"

[[ -x "$BIN" ]] || { echo "no test binary at $BIN — build it first (see the header)" >&2; exit 2; }
mkdir -p "$OUT"

SLOTS=(
	muteLatchIsDecidedBeforeDependenciesAreCounted
	mutedSenderDoesNotReplayItsStaleSidechainTap
	mutedSenderStopsFeedingADeferredReceiver
	topologyChangesWaitForTheRenderPeriodToEnd
	creatingAChannelIsSerialisedWithTheRenderPeriod
	movingAChannelIsSerialisedWithTheRenderPeriod
	reorderingEffectsIsSerialisedWithTheRenderPeriod
	addingAPlayHandleIsSerialisedWithTheIterator
)

SUMMARY="$OUT/SUMMARY.txt"
: > "$SUMMARY"
echo "build: $BUILD   out: $OUT"
for slot in "${SLOTS[@]}"; do
	log="$OUT/$slot.log"
	( cd "$(dirname "$BIN")" && QT_QPA_PLATFORM=offscreen setarch -R \
		env TSAN_OPTIONS="halt_on_error=0 suppressions=$SUPP log_exe_name=1" \
		timeout 600 "./$(basename "$BIN")" "$slot" > "$log" 2>&1 )
	rc=$?
	races=$(grep -c "^WARNING: ThreadSanitizer: data race" "$log" || true)
	verdict=$(grep -oE "^(FAIL!|PASS)" "$log" | tr '\n' ' ')
	evidence=$(grep -oE "MIXCONC_[A-Za-z0-9]+ .*" "$log" | tr '\n' ' ')
	printf '%-52s EXIT=%-4s races=%-3s %s\n' "$slot" "$rc" "$races" "$verdict" | tee -a "$SUMMARY"
	[ -n "$evidence" ] && echo "  evidence: $evidence" | tee -a "$SUMMARY"
	if [ "$races" != "0" ]; then
		{
			echo "  --- report summaries ---"
			grep -E "^SUMMARY: ThreadSanitizer" "$log" | sed 's/^/  /'
			echo "  --- frames naming the code under test ---"
			grep -oE "(Mixer|EffectChain|AudioBusHandle|AudioEngineWorkerThread)\.cpp:[0-9]+" "$log" \
				| sort | uniq -c | sed 's/^/  /'
		} >> "$SUMMARY"
	fi
	if grep -q "FAIL!" "$log"; then
		{ echo "  --- failure ---"; sed -n '/^FAIL!/,/^$/p' "$log" | head -10 | sed 's/^/  /'; } >> "$SUMMARY"
	fi
done
echo
echo "=== $SUMMARY ==="
cat "$SUMMARY"
