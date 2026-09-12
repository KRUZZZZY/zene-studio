#!/usr/bin/env bash
# vst3-instrument-render-proof.sh — behaviour-preservation evidence for the
# VST3 instrument-hosting slice (branch post-alpha/instrument-hosting-impl).
#
# WHAT IT MEASURES, AND WHY IT IS NOT A SHA256
# --------------------------------------------
# Renders in this tree are NOT bit-reproducible (independently confirmed three
# times; upstream-inherited). A hash comparison would therefore be noise, not
# evidence. What this script measures instead is the largest sample difference
# between two renders, in 16-bit LSB and in dB, and it always measures a
# SAME-BUILD run-to-run floor first:
#
#   floor      = max |delta| between repeated renders by the same binary
#   subject    = max |delta| between the new binary and the pre-change binary
#   control    = max |delta| between the project and the project with ONE
#                audible edit (the sensitivity control: if the measurement
#                cannot see this, it cannot see anything)
#
# The subject must land at the floor; the control must land far above it.
#
# Usage:
#   bash tests/vst3-instrument-render-proof.sh <lmms-new> <lmms-baseline> [project]
#
# Defaults: project = data/projects/tutorials/editing_note_volumes.mmp, which
# uses one built-in instrument (tripleoscillator) and no third-party plug-in,
# so the whole path is fork-independent apart from the code under test.
#
# Plugins are found through LMMS_PLUGIN_DIR, so a binary may live anywhere.

set -uo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
LMMS_NEW="${1:-$REPO_ROOT/build/lmms}"
LMMS_BASELINE="${2:-}"
PROJECT="${3:-$REPO_ROOT/data/projects/tutorials/editing_note_volumes.mmp}"
REPEATS="${REPEATS:-3}"
WORK="$(mktemp -d /tmp/zene-instrhost-render-XXXXXX)"

export LMMS_PLUGIN_DIR="${LMMS_PLUGIN_DIR:-$REPO_ROOT/build/plugins}"
# An offscreen X server is not needed: `lmms render` runs headless, but the
# Qt platform plugin is still loaded, so name the offscreen one explicitly.
export QT_QPA_PLATFORM="${QT_QPA_PLATFORM:-offscreen}"

echo "== VST3 instrument-hosting render proof =="
echo "project       : $PROJECT"
echo "lmms (new)    : $LMMS_NEW"
echo "lmms (baseline): ${LMMS_BASELINE:-<none>}"
echo "plugins       : $LMMS_PLUGIN_DIR"
echo "repeats       : $REPEATS"
echo "work          : $WORK"

[[ -f "$PROJECT" ]] || { echo "FAIL: no project at $PROJECT" >&2; exit 2; }
[[ -x "$LMMS_NEW" ]] || { echo "FAIL: no lmms binary at $LMMS_NEW" >&2; exit 2; }

render() { # <binary> <project> <output wav>
	local binary="$1" project="$2" out="$3"
	"$binary" render "$project" -f wav -s 44100 -o "$out" > "$out.log" 2>&1
	echo "  render exit=$? -> $out ($(stat -c '%s bytes' "$out" 2>/dev/null || echo missing))"
}

# --- the sensitivity control: the same project, one audible edit ------------
CONTROL_PROJECT="$WORK/control.mmp"
# The edit must not change the render's LENGTH, or the comparison would be
# rejected as incomparable: the master volume is the one setting that changes
# the samples and nothing else.
sed -e 's/mastervol="100"/mastervol="60"/' "$PROJECT" > "$CONTROL_PROJECT"
if cmp -s "$PROJECT" "$CONTROL_PROJECT"; then
	echo "FAIL: the sensitivity control could not be produced (the project does not"
	echo "      carry the mastervol attribute this script edits)" >&2
	exit 2
fi

echo
echo "--- renders ---"
for i in $(seq 1 "$REPEATS"); do
	echo "floor render $i:"
	render "$LMMS_NEW" "$PROJECT" "$WORK/floor_$i.wav"
done
echo "subject (baseline binary):"
if [[ -n "$LMMS_BASELINE" && -x "$LMMS_BASELINE" ]]; then
	render "$LMMS_BASELINE" "$PROJECT" "$WORK/baseline.wav"
else
	echo "  skipped: no baseline binary given"
fi
echo "control (one audible edit):"
render "$LMMS_NEW" "$CONTROL_PROJECT" "$WORK/control.wav"

echo
echo "--- measurement (16-bit LSB and dBFS) ---"
python3 - "$WORK" "$REPEATS" <<'PY'
import array
import glob
import math
import os
import sys
import wave

work, repeats = sys.argv[1], int(sys.argv[2])


def read(path):
    with wave.open(path, "rb") as handle:
        if handle.getsampwidth() != 2:
            raise SystemExit(f"{path}: not 16-bit PCM")
        frames = handle.readframes(handle.getnframes())
    samples = array.array("h")
    samples.frombytes(frames)
    return samples


def compare(a_path, b_path):
    a, b = read(a_path), read(b_path)
    if len(a) != len(b):
        return None
    worst = 0
    diff_sq = 0
    ref_sq = 0
    for x, y in zip(a, b):
        d = x - y
        if abs(d) > worst:
            worst = abs(d)
        diff_sq += d * d
        ref_sq += x * x
    rms_ref = math.sqrt(ref_sq / len(a)) if a else 0.0
    rms_diff = math.sqrt(diff_sq / len(a)) if a else 0.0
    if rms_diff == 0.0:
        db = float("-inf")
    elif rms_ref == 0.0:
        db = float("inf")
    else:
        db = 20.0 * math.log10(rms_diff / rms_ref)
    return worst, db


floor_files = sorted(glob.glob(os.path.join(work, "floor_*.wav")))
worst_floor = (0, float("-inf"))
for i in range(len(floor_files)):
    for j in range(i + 1, len(floor_files)):
        result = compare(floor_files[i], floor_files[j])
        if result is None:
            raise SystemExit("floor renders differ in length")
        if result[0] > worst_floor[0]:
            worst_floor = result
print(f"same-build run-to-run floor : max|delta| = {worst_floor[0]} LSB, "
      f"{worst_floor[1]:+.3f} dB")

baseline = os.path.join(work, "baseline.wav")
if os.path.exists(baseline):
    result = compare(floor_files[0], baseline)
    if result is None:
        print("baseline vs new            : NOT COMPARABLE (different length)")
    else:
        print(f"baseline vs new (subject)   : max|delta| = {result[0]} LSB, "
              f"{result[1]:+.3f} dB")

control = os.path.join(work, "control.wav")
if os.path.exists(control):
    result = compare(floor_files[0], control)
    if result is None:
        print("sensitivity control         : NOT COMPARABLE (different length)")
    else:
        print(f"sensitivity control         : max|delta| = {result[0]} LSB, "
              f"{result[1]:+.3f} dB")
PY

echo
echo "logs and renders kept in $WORK"
