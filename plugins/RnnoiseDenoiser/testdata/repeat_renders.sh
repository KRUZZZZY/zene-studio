#!/bin/bash
# Regenerate the repeat-render evidence quoted in RUNTIME-TEST.md §4.2, §5, §9:
#   - tail-burst stability + noise-only RMS stability across three renders (A)
#   - render-to-render nondeterminism (C_1 vs C_3, A vs C vs D vs E hashes)
#   - E (native-scale) spread across runs
#
# Usage: bash repeat_renders.sh [outdir]        (default /tmp/rnnoise_repeat)
# Then:  RNNOISE_REPEAT_DIR=<outdir> python3 final_probe.py
#        RNNOISE_REPEAT_DIR=<outdir> python3 probe_bypass_and_delay.py
set -u
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../../.." && pwd)"        # lmms-rnnoise/
OUT="${1:-/tmp/rnnoise_repeat}"
mkdir -p "$OUT"
cd "$ROOT/build"

# tag:project pairs -- tag becomes the WAV basename the probes read
for pair in A:rnnoise_test_A C:rnnoise_test_C D:rnnoise_test_D_float_nofx E:rnnoise_test_E_float_denoise; do
    tag="${pair%%:*}"
    proj="${pair##*:}"
    for i in 1 2 3; do
        wav="$OUT/${tag}_$i.wav"
        QT_QPA_PLATFORM=offscreen ./lmms render "../plugins/RnnoiseDenoiser/testdata/$proj.mmp" \
            -f wav -s 48000 -a -o "$wav" >/dev/null 2>&1
        echo "render $proj run $i -> $wav  EXIT=$?  size=$(stat -c%s "$wav" 2>/dev/null)"
    done
done

echo
echo "=== final_probe.py (RNNOISE_REPEAT_DIR=$OUT) ==="
RNNOISE_REPEAT_DIR="$OUT" python3 "$HERE/final_probe.py"
echo
echo "=== probe_bypass_and_delay.py (RNNOISE_REPEAT_DIR=$OUT) ==="
RNNOISE_REPEAT_DIR="$OUT" python3 "$HERE/probe_bypass_and_delay.py"
