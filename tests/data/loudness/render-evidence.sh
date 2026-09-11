#!/usr/bin/env bash
#
# render-evidence.sh - the evidence run behind docs/LUFS-WIRING.md.
#
# Renders three fixture projects through the real CLI render path and shows:
#   * a -23 dBFS tone (EBU Tech 3341 case 1, -23.0 LUFS) reporting its own level,
#   * the same tone 10 dB quieter (case 2, -33.0 LUFS) reporting 10 LU less,
#   * digital silence reporting the -inf sentinel and no verdict,
#   * the same project rendered with and without the report, compared byte for
#     byte (passivity), and
#   * both keys re-measured by a second, independent BS.1770-4 implementation
#     (bs1770_reference.py, numpy) for a cross-check.
#
# Every exit code is printed unpiped (workspace rule 6). Usage:
#   bash tests/data/loudness/render-evidence.sh [worktree] [output-dir]
#
set -u

WT="${1:-$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)}"
OUT="${2:-/tmp/lufs-evidence}"
FIX="$OUT/fixtures"
BIN="$WT/build/lmms"
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

if [ ! -x "$BIN" ]; then
	echo "no built binary at $BIN - build the worktree first" >&2
	exit 2
fi

rm -rf "$OUT"
mkdir -p "$OUT" "$FIX"
export QT_QPA_PLATFORM=offscreen

python3 "$HERE/make-fixtures.py" "$FIX" > "$OUT/fixtures.log" 2>&1
echo "make-fixtures.py EXIT=$?  (log: $OUT/fixtures.log)"

render() { # <fixture-name> <output-name> [extra lmms args...]
	local fixture="$1" output="$2"
	shift 2
	echo "--- lmms render $fixture.mmp -f wav -s 48000 -a $* -o $output ---"
	( cd "$WT" && "./build/lmms" render "$FIX/$fixture.mmp" -f wav -s 48000 -a "$@" -o "$OUT/$output" ) \
		> "$OUT/$output.log" 2>&1
	echo "render EXIT=$?"
	grep -E "^(Loudness:|Loudness report written)" "$OUT/$output.log"
}

echo
echo "=== A: -23 dBFS tone (EBU Tech 3341 case 1 -> -23.0 LUFS), report requested ==="
render tone-23 tone-23.wav --loudness-report
echo
echo "=== B: the same tone 10 dB quieter (case 2 -> -33.0 LUFS), report requested ==="
render tone-33 tone-33.wav --loudness-report
echo
echo "=== C: digital silence, report requested (negative control) ==="
render silent silent.wav --loudness-report
echo
echo "=== D: passivity control - the same project without the report ==="
render tone-23 tone-23-noreport.wav

echo
echo "=== sidecar reports, as written beside the renders ==="
for report in "$OUT"/*.loudness.txt; do
	echo "--- $report"
	cat "$report"
done

echo
echo "=== independent BS.1770-4 implementation (numpy) on the rendered files ==="
for wav in tone-23.wav tone-33.wav silent.wav; do
	echo "--- bs1770_reference.py $wav"
	python3 "$HERE/bs1770_reference.py" "$OUT/$wav"
	echo "bs1770_reference.py EXIT=$?"
done

echo
echo "=== passivity: report on vs report off ==="
python3 "$HERE/passivity-check.py" "$OUT/tone-23.wav" "$OUT/tone-23-noreport.wav"
echo "passivity-check.py EXIT=$?"
