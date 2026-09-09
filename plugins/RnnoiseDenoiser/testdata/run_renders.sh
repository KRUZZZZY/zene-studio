#!/usr/bin/env bash
# Reproducible render suite for the RNNoise denoiser runtime test (AI-KOS #559).
#
# Run from this directory:  bash run_renders.sh
# All renders are headless (QT_QPA_PLATFORM=offscreen); no GUI is launched.
# Every render is wrapped in `timeout 300` so a hang fails loudly instead of
# blocking. The exact command and its exit code are echoed for every render.

set -u
TD="$(cd "$(dirname "$0")" && pwd)"
BUILD="$TD/../../../build"
LMMS="$BUILD/lmms"

render() {
    local name="$1" project="$2" out="$3"
    echo "=== RENDER $name ==="
    echo "\$ cd $BUILD"
    echo "\$ QT_QPA_PLATFORM=offscreen ./lmms render ../plugins/RnnoiseDenoiser/testdata/$project -f wav -s 48000 -a -o ../plugins/RnnoiseDenoiser/testdata/$out"
    ( cd "$BUILD" && timeout 300 env QT_QPA_PLATFORM=offscreen ./lmms render \
        "../plugins/RnnoiseDenoiser/testdata/$project" -f wav -s 48000 -a \
        -o "../plugins/RnnoiseDenoiser/testdata/$out" )
    local rc=$?
    echo "EXIT=$rc"
    ls -la "$TD/$out" 2>&1
    echo
}

render "A  plugin active  (int16 sample, +/-1.0 scale)" rnnoise_test_A.mmp        out_A_denoised.wav
render "B  plugin bypassed on=0 (int16 sample)"        rnnoise_test_B.mmp        out_B_bypassed.wav
render "C  no FX (int16 sample)"                       rnnoise_test_C.mmp        out_C_nofx.wav
render "D  no FX (float sample x32768)"                rnnoise_test_D_float_nofx.mmp  out_D_float_nofx.wav
render "E  plugin active (float sample x32768)"        rnnoise_test_E_float_denoise.mmp out_E_float_denoised.wav
render "EDGE plugin active (96100-frame clip)"         rnnoise_edge.mmp          out_edge_denoised.wav
render "EDGE no FX (96100-frame clip)"                 rnnoise_edge_nofx.mmp     out_edge_nofx.wav
render "F  bogus plugin name (DummyEffect control)"    rnnoise_test_F_bogus.mmp  out_F_bogus.wav
