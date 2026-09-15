#!/usr/bin/env bash
# dawproject-proof.sh — run the DAWproject interchange proofs that do NOT need a
# full build (feature row 37, docs/DAWPROJECT-INTERCHANGE.md section 9).
#
#   bash tools/dawproject-proof.sh
#
# Two measurements, both with the project's own compile flags, taken from
# <build>/compile_commands.json:
#
#   1. the round trip that compares the MODEL — authors the same model
#      tests/src/core/DawProjectInterchangeRoundTripTest.cpp authors, writes a
#      real .dawproject container, reads it back and compares the models with
#      their own operator==, then checks the digest moves when the model moves and
#      that the three refusals are typed. Links only the five engine-free
#      translation units, so it needs no Engine, no GUI and no link of the
#      product. Exit 0 = the models are equal.
#
#   2. the SPEC A16 histogram — measured off the tree's own four table blocks the
#      way ReversibilityTable's constructor assembles them, for comparison with
#      the constant ReversibilityContractTest asserts.
#
# It needs a configured build directory (for compile_commands.json and the
# generated headers); it does not need that build to be complete. Override with
# DAWPROJECT_PROOF_BUILD=/path/to/build.
#
# This is a SUPPLEMENT to the registered ctest, not a replacement: the ctest also
# drives the session half and the control surface, which need the Engine.

set -uo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/.." && pwd)"
BUILD="${DAWPROJECT_PROOF_BUILD:-$ROOT/build}"
WORK="${DAWPROJECT_PROOF_WORK:-$(mktemp -d)}"
CXX="${CXX:-/usr/bin/c++}"

if [[ ! -f "$BUILD/compile_commands.json" ]]; then
	echo "dawproject-proof: no $BUILD/compile_commands.json - configure a build directory first" >&2
	exit 2
fi

# The flags of one translation unit of the product, reused for every probe: same
# standard, same defines, same generated headers. The object-export define is
# dropped because these probes are their own program, not the shared library.
FLAGS_FILE="$WORK/flags.sh"
python3 - "$BUILD" > "$FLAGS_FILE" <<'PY'
import json, shlex, sys
build = sys.argv[1]
entries = json.load(open(build + '/compile_commands.json'))
for name in ('DawProjectModel.cpp', 'ControlReversibilityTableDawProject.cpp'):
    match = [e for e in entries if e['file'].endswith(name)]
    if not match:
        continue
    tokens = shlex.split(match[0]['command'])
    cut = tokens.index('-o')
    flags = [t for t in tokens[1:cut] if t != '-c' and not t.startswith('-Dlmmsobjs_EXPORTS')]
    print('FLAGS=( ' + ' '.join(shlex.quote(f) for f in flags) + ' )')
    break
else:
    sys.exit('no DAWproject translation unit in compile_commands.json')
PY
if [[ ! -s "$FLAGS_FILE" ]]; then
	echo "dawproject-proof: could not read the project's flags from $BUILD/compile_commands.json" >&2
	exit 2
fi
# shellcheck source=/dev/null
source "$FLAGS_FILE"

rc=0
compile() { # compile <source> <object>
	"$CXX" "${FLAGS[@]}" -c "$1" -o "$2" > "$2.log" 2>&1
	local code=$?
	if [[ $code -ne 0 ]]; then
		echo "dawproject-proof: COMPILE FAILED ($1) EXIT=$code" >&2
		tail -20 "$2.log" >&2
		rc=1
	fi
	return $code
}

echo "=== 1. the round trip that compares the MODEL ==="
ENGINE_TUS=(DawProjectModel DawProjectZip DawProjectWrite DawProjectRead DawProjectReadTracks)
OBJECTS=()
for tu in "${ENGINE_TUS[@]}"; do
	compile "$ROOT/src/core/$tu.cpp" "$WORK/$tu.o" || break
	OBJECTS+=("$WORK/$tu.o")
done
if [[ $rc -eq 0 ]]; then
	compile "$HERE/dawproject-roundtrip-proof.cpp" "$WORK/roundtrip.o" &&
		"$CXX" "${FLAGS[@]}" "${OBJECTS[@]}" "$WORK/roundtrip.o" -o "$WORK/dawproject-roundtrip-proof" -lQt6Core -lQt6Xml > "$WORK/link.log" 2>&1 ||
	{ echo "dawproject-proof: LINK FAILED" >&2; tail -20 "$WORK/link.log" >&2; rc=1; }
fi
if [[ $rc -eq 0 ]]; then
	# The round trip constructs no widgets; the offscreen platform keeps Qt from
	# looking for a display.
	QT_QPA_PLATFORM=offscreen "$WORK/dawproject-roundtrip-proof"
	code=$?
	echo "round-trip EXIT=$code"
	[[ $code -eq 0 ]] || rc=$code
fi

echo "=== 2. the SPEC A16 histogram, measured off the tree ==="
HIST_OBJECTS=()
for table in "$ROOT"/src/core/ControlReversibilityTable*.cpp; do
	# ControlReversibility.cpp's undo machinery needs the Engine, and the table
	# TUs do not include it: the tables are data. Only the two undo entry points
	# it exports are referenced, and the probe defines them as no-ops.
	compile "$table" "$WORK/$(basename "$table" .cpp).o" || break
	HIST_OBJECTS+=("$WORK/$(basename "$table" .cpp).o")
done
if [[ $rc -eq 0 ]]; then
	compile_defines=(-DDAWPROJECT_PROOF_LINK)
	"$CXX" "${FLAGS[@]}" "${compile_defines[@]}" -c "$HERE/dawproject-a16-histogram.cpp" \
		-o "$WORK/histogram.o" > "$WORK/histogram.log" 2>&1 &&
		"$CXX" "${FLAGS[@]}" "${HIST_OBJECTS[@]}" "$WORK/histogram.o" -o "$WORK/dawproject-a16-histogram" -lQt6Core > "$WORK/link-hist.log" 2>&1 ||
	{ echo "dawproject-proof: LINK FAILED (histogram)" >&2; tail -20 "$WORK/link-hist.log" >&2; rc=1; }
fi
if [[ $rc -eq 0 ]]; then
	"$WORK/dawproject-a16-histogram"
	code=$?
	echo "histogram EXIT=$code"
	[[ $code -eq 0 ]] || rc=$code
fi

echo "dawproject-proof: work dir $WORK"
exit $rc
