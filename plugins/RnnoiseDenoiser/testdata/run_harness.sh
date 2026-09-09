#!/bin/bash
# Build and run the library-level RNNoise harness at both input scales.
# Usage: bash run_harness.sh
# Requires: gcc, python3+numpy. Does NOT modify any project source.
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../../.." && pwd)"   # lmms-rnnoise/
cd "$ROOT"

echo "=== build ==="
gcc -O2 -I plugins/RnnoiseDenoiser/rnnoise \
    -o /tmp/rnn_harness \
    plugins/RnnoiseDenoiser/testdata/rnn_harness.c \
    plugins/RnnoiseDenoiser/rnnoise/denoise.c \
    plugins/RnnoiseDenoiser/rnnoise/rnn.c \
    plugins/RnnoiseDenoiser/rnnoise/pitch.c \
    plugins/RnnoiseDenoiser/rnnoise/kiss_fft.c \
    plugins/RnnoiseDenoiser/rnnoise/celt_lpc.c \
    plugins/RnnoiseDenoiser/rnnoise/nnet.c \
    plugins/RnnoiseDenoiser/rnnoise/nnet_default.c \
    plugins/RnnoiseDenoiser/rnnoise/parse_lpcnet_weights.c \
    plugins/RnnoiseDenoiser/rnnoise/rnnoise_data.c \
    plugins/RnnoiseDenoiser/rnnoise/rnnoise_tables.c \
    -lm 2>&1 | grep -v '#warning' || true
echo "build exit: $?"
ls -la /tmp/rnn_harness

echo
echo "=== run at both scales ==="
python3 plugins/RnnoiseDenoiser/testdata/run_harness.py
