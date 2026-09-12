#!/usr/bin/env bash
# unregistered-tests-gate.sh — Gate 10: every test source in the tree is either
# registered in tests/CMakeLists.txt or declared below with a reason.
#
# Why this gate exists. Three defects of one class were found by hand in a single
# night, in a tree whose gates were all green:
#
#   * tests/src/core/PhaseDSidechainTest.cpp referenced PART_D_COMPRESSOR_LIBRARY,
#     which nothing defined - it could not compile, so it was silently left out of
#     tests/CMakeLists.txt and looked like Phase D sidechain coverage while being
#     unable to run in any way;
#   * tests/src/core/MixerRoutingBackwardCompatTest.cpp and
#     tests/src/core/PhaseFChannelScaleTest.cpp were never registered;
#   * a fourth unregistered test was found by another lane the same evening.
#
# Gate 9 (fork-sources) checks that a file is in a *scope manifest*. It says
# nothing about whether the file is ever built, and a test that is in
# tests/all-sources.txt but in no CMake target is invisible to every other gate:
# no coverage ratchet sees it (it never runs), no complexity ratchet sees it (it
# never compiles), and the suite stays green without it. This gate closes that:
# a test source that is neither registered nor declared here is a violation.
#
# The DECLARED table below is the whole point - an exclusion has to be written
# down and justified, so a reader can tell "deliberately not built" from
# "accidentally not built". Adding a line is a deliberate act; forgetting one is
# a red gate.
#
# Usage:
#   bash tests/unregistered-tests-gate.sh            # report violations only
#   bash tests/unregistered-tests-gate.sh --verbose  # print every source's verdict
#
# Exit codes: 0 = every test source is registered or declared, 1 = undeclared
# source(s) found, 2 = setup error.

set -uo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/.." && pwd)"
cd "$ROOT" || exit 2

VERBOSE=0
for arg in "$@"; do
	case "$arg" in
		--verbose|-v) VERBOSE=1 ;;
		*) echo "usage: $0 [--verbose]" >&2; exit 2 ;;
	esac
done

CMAKE_FILE="$HERE/CMakeLists.txt"
[[ -f "$CMAKE_FILE" ]] || { echo "error: tests/CMakeLists.txt not found" >&2; exit 2; }

# ---------------------------------------------------------------------------
# DECLARED: test sources deliberately not registered in tests/CMakeLists.txt.
# One per line: <repo-relative path><TAB><reason>
#
# A helper source (something a test links or a standalone probe, not a QTest
# class) is not a test and needs no declaration here - it is listed in the
# HELPER table below instead, so the difference is visible rather than implied.
# ---------------------------------------------------------------------------
DECLARED=$(cat <<'EOF'
tests/src/core/PhaseDPerfBench.cpp	A CPU-cost benchmark (SPEC v1.2 decision D3: "<5% single-core CPU per active sidechain send"), measured with CLOCK_PROCESS_CPUTIME_ID. Not registered because a suite that runs while sibling builds compile on the same box measures the machine's noise, not the code's cost: this file's own header documents bracketed twin windows precisely because the machine is not quiet. Run it by hand: cmake --build build --target PhaseDPerfBench && build/tests/PhaseDPerfBench. Its numbers are the evidence in PART-D-SIDECHAIN.md.
tests/src/core/TwoTrackAlsaCaptureProbe.cpp	Not a QTest class: a standalone probe with its own main() that opens a real ALSA capture device (default hw:1,0) and measures capture-thread allocations. It cannot run on a machine with no capture hardware and is not a unit test, so it has no home in ctest. Run by hand: build/tests/TwoTrackAlsaCaptureProbe <device> <periods> <outdir>.
tests/src/plugins/Vst3BusMapTest.cpp	VST3 host test, never wired into CMake. The CLAP equivalents are registered under if(WANT_CLAP) (ClapBusMapTest/ClapHostTest/ClapEffectIntegrationTest); there is no VST3 counterpart block, so these three files compile nowhere. Wiring them needs the VST3 SDK target and fixture-bundle plumbing (see the WANT_VST3_TEST_INSTRUMENT block below) - an open item recorded in docs/TEST-HYGIENE.md, NOT a deliberate exclusion.
tests/src/plugins/Vst3HostTest.cpp	See Vst3BusMapTest.cpp above: VST3 host test, no CMake registration, no VST3 equivalent of the CLAP test block. Open item, not a deliberate exclusion.
tests/src/plugins/Vst3EffectIntegrationTest.cpp	See Vst3BusMapTest.cpp above: VST3 effect test, no CMake registration. Its CLAP twin (ClapEffectIntegrationTest) is registered and runs. Open item, not a deliberate exclusion.
EOF
)

# Sources that are not test classes: helpers linked into something else, or
# standalone probes with their own main(). Declared so that the distinction is
# explicit and a new orphan cannot hide behind "it is probably a helper".
HELPER=$(cat <<'EOF'
tests/src/plugins/FakeRemotePluginClient.cpp	Helper executable for RemotePluginClientE2ETest (no add_test: it is spawned by the test).
tests/src/plugins/PluginPortsMigrationReference.cpp	Reference executable for the Part C migration comparison (paired with PluginPortsMigrationTest).
tests/src/plugins/SyntheticAudioPlugin.cpp	Helper plugin module linked into AudioPluginTest.
tests/src/plugins/Vst3InstrumentFixtureProbe.cpp	Helper probe for the VST3 instrument fixture, registered only under WANT_VST3_TEST_INSTRUMENT (default OFF: an ordinary build gains no test instrument).
EOF
)

declare -A DECLARED_REASON=()
declare -A HELPER_REASON=()
while IFS=$'\t' read -r path reason; do
	[[ -z "$path" ]] && continue
	[[ -z "$reason" ]] && { echo "error: declaration without a reason: $path" >&2; exit 2; }
	DECLARED_REASON["$path"]="$reason"
done < <(grep -vE '^[[:space:]]*(#|$)' <<< "$DECLARED")
while IFS=$'\t' read -r path reason; do
	[[ -z "$path" ]] && continue
	[[ -z "$reason" ]] && { echo "error: helper declaration without a reason: $path" >&2; exit 2; }
	HELPER_REASON["$path"]="$reason"
done < <(grep -vE '^[[:space:]]*(#|$)' <<< "$HELPER")
[[ ${#DECLARED_REASON[@]} -gt 0 ]] || { echo "error: the DECLARED table is empty" >&2; exit 2; }

# Registered = the file's path appears as a source in tests/CMakeLists.txt.
mapfile -t REGISTERED < <(grep -oE 'src/[A-Za-z0-9_/]+\.cpp' "$CMAKE_FILE" | sort -u)
declare -A IS_REGISTERED=()
for p in "${REGISTERED[@]}"; do IS_REGISTERED["tests/$p"]=1; done

mapfile -t ON_DISK < <(git ls-files 'tests/src/*.cpp' 'tests/src/**/*.cpp' | LC_ALL=C sort)

scanned=0
registered=0
declare -a VIOLATIONS=()
printf '%-56s %s\n' "test source" "verdict"
for f in "${ON_DISK[@]}"; do
	scanned=$((scanned + 1))
	if [[ -n "${IS_REGISTERED[$f]:-}" ]]; then
		registered=$((registered + 1))
		[[ $VERBOSE -eq 1 ]] && printf '%-56s %s\n' "$f" "registered in tests/CMakeLists.txt (built and run)"
	elif [[ -n "${HELPER_REASON[$f]:-}" ]]; then
		printf '%-56s %s\n' "$f" "helper (not a test class) -> ${HELPER_REASON[$f]}"
	elif [[ -n "${DECLARED_REASON[$f]:-}" ]]; then
		printf '%-56s %s\n' "$f" "DECLARED not-built -> ${DECLARED_REASON[$f]}"
	else
		VIOLATIONS+=("$f")
		printf '%-56s %s\n' "$f" "VIOLATION: registered in no CMake target and declared nowhere"
	fi
done

echo
echo "test sources scanned: $scanned (registered: $registered, declared-not-built: ${#DECLARED_REASON[@]}, helpers: ${#HELPER_REASON[@]})"
if [[ ${#VIOLATIONS[@]} -gt 0 ]]; then
	echo "FAIL: ${#VIOLATIONS[@]} test source(s) are in no CMake target and declared nowhere."
	echo "      Either register it in tests/CMakeLists.txt, or add it to the DECLARED"
	echo "      table in this script with the reason it must not be built."
	exit 1
fi
echo "PASS: every test source under tests/src/ is registered, or declared with a reason"
exit 0
