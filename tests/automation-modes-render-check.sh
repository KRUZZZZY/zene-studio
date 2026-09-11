#!/usr/bin/env bash
#
# automation-modes-render-check.sh - the headless render proof for the
# Automation modes lane (post-alpha/automation-modes).
#
# What it proves, in one command, against real renders of a real project:
#
#   1. BYTE-IDENTITY.  In Read - the status quo, and the default for every
#      control - an automation-bearing project renders byte-identically before
#      and after the automation-modes change. The baseline binary is passed in
#      by the caller (built from the base commit); the candidate defaults to
#      build/lmms.
#   2. DETERMINISM.  The same binary renders the same project byte-identically
#      twice, so a difference in (1) would be a real difference and not render
#      noise. Without this leg the identity claim is unfalsifiable.
#   3. SENSITIVITY.  The comparison can see a change at all: the same project
#      with the written automation at a different value, and with the
#      automation clip removed entirely, must both render differently from the
#      reference. (Two renders of two silent files are also byte-identical -
#      this is the leg that rules that out.)
#
# The fixtures are generated here from the tracked demo project
# data/projects/shorties/Crunk(Demo).mmp, so nothing new is added to the repo
# and the proof is reproducible from a clean checkout.
#
# Usage:
#   tests/automation-modes-render-check.sh --baseline <lmms-binary> [options]
#     --baseline <path>   binary built from the base commit (required)
#     --candidate <path>  binary to test (default: build/lmms)
#     --work <dir>        scratch dir for fixtures and renders
#                         (default: build/automation-modes-render)
#     --help
#
# Exit 0 only if every leg passes. Each render's exit code is checked unpiped.

set -uo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/.." && pwd)"

BASELINE=""
CANDIDATE="$ROOT/build/lmms"
WORK="$ROOT/build/automation-modes-render"

usage() { sed -n '2,45p' "$HERE/$(basename "${BASH_SOURCE[0]}")" | sed 's/^# \{0,1\}//'; }

while [ $# -gt 0 ]; do
	case "$1" in
		--baseline)  BASELINE="${2:?--baseline needs a path}"; shift 2 ;;
		--candidate) CANDIDATE="${2:?--candidate needs a path}"; shift 2 ;;
		--work)      WORK="${2:?--work needs a dir}"; shift 2 ;;
		-h|--help)   usage; exit 0 ;;
		*) echo "automation-modes-render-check: unknown argument: $1" >&2; exit 2 ;;
	esac
done

[ -n "$BASELINE" ] || { echo "automation-modes-render-check: --baseline <lmms-binary> is required" >&2; exit 2; }
[ -x "$BASELINE" ] || { echo "automation-modes-render-check: not executable: $BASELINE" >&2; exit 2; }
[ -x "$CANDIDATE" ] || { echo "automation-modes-render-check: not executable: $CANDIDATE" >&2; exit 2; }

DEMO="$ROOT/data/projects/shorties/Crunk(Demo).mmp"
[ -f "$DEMO" ] || { echo "automation-modes-render-check: missing fixture source: $DEMO" >&2; exit 2; }

mkdir -p "$WORK" || exit 2

echo "=== automation modes: headless render check ==="
echo "baseline  : $BASELINE"
echo "candidate : $CANDIDATE"
echo "work dir  : $WORK"
echo "fixtures  : generated from $DEMO"
echo

# --- fixtures -----------------------------------------------------------------
# The project automates the Master *mixer channel* fader (mixerchannel num="0",
# whose volume model is `FloatModel(1, 0, 2, 0.001)` - a linear range, unlike the
# song's own "Master volume" model in percent) with a real automation clip whose
# <object id> resolves to that channel's volume model. That is the shape a saved
# project has once a fader is automated, and it is the model a mixer fader rides.
python3 - "$DEMO" "$WORK" <<'PY' || exit 2
import os, sys

demo_path, work = sys.argv[1], sys.argv[2]
with open(demo_path, encoding="utf-8") as fh:
    demo = fh.read()

CHANNEL_ATTR = '      <mixerchannel muted="0" num="0" name="Master" volume="1" soloed="0">\n'
CHANNEL_ELEM = ('      <mixerchannel muted="0" num="0" name="Master" soloed="0">\n'
                '        <volume id="9000001" value="1"/>\n')
TRACK_END = '    </track>\n    <mixer '

def clip(values):
    nodes = "".join(
        '        <time pos="%d" value="%s" outValue="%s"/>\n' % (pos, value, value)
        for pos, value in values)
    return ('      <automationclip prog="1" tens="0" mute="0" name="Master fader"'
            ' pos="0" len="768" off="0" autoresize="1">\n'
            + nodes
            + '        <object id="9000001"/>\n'
            '      </automationclip>\n')

def with_fader(doc, values):
    out = doc.replace(CHANNEL_ATTR, CHANNEL_ELEM)
    assert out != doc, "Master mixer channel element not found"
    assert "<volume id=" not in doc
    out = out.replace(TRACK_END, clip(values) + TRACK_END)
    assert out.count("<automationclip") == 1, "automation clip not inserted"
    return out

# Values are inside the channel volume model's own range [0, 2] - an out-of-range
# value would be clamped and two different curves would render identically.
fader_down = with_fader(demo, [(0, "0.4"), (384, "0.9"), (768, "0.4")])
fader_flat = with_fader(demo, [(0, "0.9"), (384, "0.9"), (768, "0.9")])
# The same project with the fader model carrying the same saved id but with no
# automation clip at all: the causal control for "the written automation is what
# changes the audio".
no_clip = demo.replace(CHANNEL_ATTR, CHANNEL_ELEM)

for name, text in (("fader-down.mmp", fader_down),
                   ("fader-flat.mmp", fader_flat),
                   ("no-automation.mmp", no_clip)):
    with open(os.path.join(work, name), "w", encoding="utf-8") as fh:
        fh.write(text)
    print("fixture  ", name)
PY

# --- hashing ------------------------------------------------------------------
# sha256 of the WAV *data* chunk (not the file): the header is not audio, and
# comparing it would let a metadata difference masquerade as an audio one.
data_sha256() {
	python3 - "$1" <<'PY'
import hashlib, struct, sys
with open(sys.argv[1], "rb") as fh:
    blob = fh.read()
assert blob[0:4] == b"RIFF" and blob[8:12] == b"WAVE", "not RIFF/WAVE"
off, data, fmt = 12, None, None
while off + 8 <= len(blob):
    cid = blob[off:off + 4]
    size = struct.unpack("<I", blob[off + 4:off + 8])[0]
    chunk = blob[off + 8:off + 8 + size]
    if cid == b"fmt ": fmt = chunk
    elif cid == b"data": data = chunk
    off += 8 + size + (size & 1)
assert fmt and data is not None, "missing fmt/data chunk"
tag, ch, rate = struct.unpack("<HHI", fmt[:8])
assert len(data) > 0, "empty audio data chunk"
print("%s %s %d %d %d" % (hashlib.sha256(data).hexdigest(), "frames=%d" % (len(data)//(4*ch)), tag, ch, rate))
PY
}

render() { # render <binary> <project> <out.wav>
	# The binary crashes on exit intermittently (~1 run in 10, "QThread:
	# Destroyed while thread is still running") *after* writing a complete WAV.
	# A render counts as successful when it produced a non-empty file; the exit
	# code is reported either way, and a run that produced nothing is retried
	# once before it is called a failure.
	local bin="$1" project="$2" out="$3"
	local attempt rc
	for attempt in 1 2; do
		rm -f "$out"
		QT_QPA_PLATFORM=offscreen "$bin" render "$project" -f wav -a -s 48000 -o "$out" > "$out.log" 2>&1
		rc=$?
		if [ "$rc" -eq 0 ] && [ -s "$out" ]; then
			return 0
		fi
		if [ -s "$out" ]; then
			echo "note: render exited $rc but produced $(stat -c%s "$out") bytes (attempt $attempt): $project" >&2
			return 0
		fi
		echo "note: render attempt $attempt failed (exit $rc, no output): $project" >&2
	done
	echo "render FAILED (exit $rc): $bin render $project" >&2
	tail -20 "$out.log" >&2
	return 1
}

FAIL=0
ref_hash=""

run_leg() { # run_leg <label> <binary> <project> <tag>
	local label="$1" bin="$2" project="$3" tag="$4"
	local out="$WORK/$tag.wav"
	render "$bin" "$project" "$out" || { echo "$label: RENDER FAILED"; FAIL=1; return 1; }
	local info; info="$(data_sha256 "$out")" || { echo "$label: HASH FAILED"; FAIL=1; return 1; }
	local hash="${info%% *}"
	echo "${label}: ${hash}"
	echo "           (${info#* })"
	if [ -z "$ref_hash" ]; then ref_hash="$hash"; fi
	LAST_HASH="$hash"
	return 0
}

echo
echo "--- leg 1+2: byte-identity of the Read render, and render determinism ---"
run_leg "baseline  fader-down "  "$BASELINE"  "$WORK/fader-down.mmp"    "base-down-1" || true
base_down="$LAST_HASH"
run_leg "candidate fader-down "  "$CANDIDATE" "$WORK/fader-down.mmp"    "cand-down-1" || true
cand_down="$LAST_HASH"
run_leg "candidate fader-down2"  "$CANDIDATE" "$WORK/fader-down.mmp"    "cand-down-2" || true
cand_down2="$LAST_HASH"

if [ "$base_down" = "$cand_down" ]; then
	echo "PASS: Read render byte-identical baseline -> candidate"
else
	echo "FAIL: Read render differs baseline=$base_down candidate=$cand_down"; FAIL=1
fi
if [ "$cand_down" = "$cand_down2" ]; then
	echo "PASS: the same binary renders the same project byte-identically twice"
else
	echo "FAIL: renders are not deterministic ($cand_down vs $cand_down2) - leg 1 is unfalsifiable"; FAIL=1
fi

echo
echo "--- leg 3: sensitivity (the comparison can see a difference) ---"
run_leg "candidate fader-flat"   "$CANDIDATE" "$WORK/fader-flat.mmp"    "cand-flat"  || true
cand_flat="$LAST_HASH"
run_leg "candidate no-auto   "   "$CANDIDATE" "$WORK/no-automation.mmp" "cand-noauto" || true
cand_noauto="$LAST_HASH"

if [ -n "$cand_flat" ] && [ "$cand_flat" != "$cand_down" ]; then
	echo "PASS: a different written automation value renders differently (automation is applied)"
else
	echo "FAIL: changing the written automation did not change the render"; FAIL=1
fi
if [ -n "$cand_noauto" ] && [ "$cand_noauto" != "$cand_down" ]; then
	echo "PASS: removing the automation clip renders differently"
else
	echo "FAIL: removing the automation clip did not change the render"; FAIL=1
fi

echo
if [ "$FAIL" -eq 0 ]; then
	echo "automation-modes-render-check: PASS (all legs)"
	exit 0
fi
echo "automation-modes-render-check: FAIL"
exit 1
