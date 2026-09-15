#!/usr/bin/env bash
# measure-a16.sh - PART 2 of tools/dawproject-proof.sh on its own: build the
# SPEC A16 histogram probe against THIS tree's table TUs and print MEASURED.
# Same flags, same order, same probe - only the dawproject round trip is skipped.
#
# usage: bash .merge-logs-030w4/measure-a16.sh [workdir]
set -uo pipefail

HERE="$(cd "$(dirname "$0")/.." && pwd)"
ROOT="$HERE"
BUILD="${DAWPROJECT_PROOF_BUILD:-$ROOT/build}"
WORK="${1:-$ROOT/.merge-logs-030w4/a16-work}"
mkdir -p "$WORK"
CXX="${CXX:-/usr/bin/c++}"

[ -f "$BUILD/compile_commands.json" ] || { echo "no compile_commands.json" >&2; exit 2; }

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
source "$FLAGS_FILE"

rc=0
HIST_OBJECTS=()
for table in "$ROOT"/src/core/ControlReversibilityTable*.cpp; do
	o="$WORK/$(basename "$table" .cpp).o"
	if [ ! -f "$o" ] || [ "$table" -nt "$o" ]; then
		"$CXX" "${FLAGS[@]}" -c "$table" -o "$o" > "$o.log" 2>&1
		code=$?
		if [ $code -ne 0 ]; then
			echo "COMPILE FAILED ($table) EXIT=$code"; tail -15 "$o.log"; rc=1; break
		fi
	fi
	HIST_OBJECTS+=("$o")
done
if [ $rc -eq 0 ]; then
	"$CXX" "${FLAGS[@]}" -DDAWPROJECT_PROOF_LINK -c "$ROOT/tools/dawproject-a16-histogram.cpp" \
		-o "$WORK/histogram.o" > "$WORK/histogram.log" 2>&1
	code=$?
	[ $code -eq 0 ] || { echo "COMPILE FAILED (probe) EXIT=$code"; tail -15 "$WORK/histogram.log"; rc=1; }
fi
if [ $rc -eq 0 ]; then
	"$CXX" "${FLAGS[@]}" "${HIST_OBJECTS[@]}" "$WORK/histogram.o" \
		-o "$WORK/dawproject-a16-histogram" -lQt6Core > "$WORK/link-hist.log" 2>&1
	code=$?
	[ $code -eq 0 ] || { echo "LINK FAILED EXIT=$code"; tail -20 "$WORK/link-hist.log"; rc=1; }
fi
if [ $rc -eq 0 ]; then
	"$WORK/dawproject-a16-histogram"
	code=$?
	echo "histogram EXIT=$code"
	[ $code -eq 0 ] || rc=$code
fi
exit $rc
