#!/bin/bash
# Repeat the A (plugin) and C (no-FX) renders N times and measure the
# noise-only passage of each, to quantify render-to-render spread of the
# post-fix denoising result.
set -u
cd "$(dirname "$0")/../../../build" || exit 1
TD=../plugins/RnnoiseDenoiser/testdata
OUT=/tmp/rnnoise-fix-repeat
mkdir -p "$OUT"
N=${1:-5}
for i in $(seq 1 "$N"); do
  QT_QPA_PLATFORM=offscreen ./lmms render "$TD/rnnoise_test_A.mmp" -f wav -s 48000 -a -o "$OUT/A_$i.wav" >/dev/null 2>&1
  echo "A_$i EXIT=$?"
done
for i in 1 2 3; do
  QT_QPA_PLATFORM=offscreen ./lmms render "$TD/rnnoise_test_C.mmp" -f wav -s 48000 -a -o "$OUT/C_$i.wav" >/dev/null 2>&1
  echo "C_$i EXIT=$?"
done
python3 "$TD/repeat_fix_measure.py" "$OUT"
