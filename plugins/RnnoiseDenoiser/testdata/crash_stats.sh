#!/usr/bin/env bash
# Crash-rate statistics for RUNTIME-TEST.md §9.1: 10 headless renders each of
#   edge      (96100-frame clip + denoiser)
#   edge_nofx (96100-frame clip, no FX at all)   <- the decisive control
#   A         (96000-frame clip + denoiser)
# Records exit codes and any QThread/abort text. Usage: bash crash_stats.sh [outdir]
set -u
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../../.." && pwd)"        # lmms-rnnoise/
OUT="${1:-/tmp/rnnoise_crashstats}"
mkdir -p "$OUT"
cd "$ROOT/build" || exit 1
TD=../plugins/RnnoiseDenoiser/testdata

for pair in edge:rnnoise_edge edge_nofx:rnnoise_edge_nofx A:rnnoise_test_A; do
    tag="${pair%%:*}"
    proj="${pair##*:}"
    codes=""
    n_bad=0
    for r in $(seq 1 10); do
        QT_QPA_PLATFORM=offscreen ./lmms render "$TD/$proj.mmp" -f wav -s 48000 -a \
            -o "$OUT/${tag}_$r.wav" >"$OUT/${tag}_$r.log" 2>&1
        c=$?
        codes="$codes $c"
        [ "$c" -ne 0 ] && n_bad=$((n_bad + 1))
    done
    echo "$tag exit codes:$codes   (nonzero: $n_bad/10)"
    grep -h 'QThread\|Aborted\|not found' "$OUT"/${tag}_*.log 2>/dev/null | sort | uniq -c | sed 's/^/    /'
    echo "    WAV sizes: $(stat -c%s "$OUT"/${tag}_*.wav 2>/dev/null | sort -u | tr '\n' ' ')"
done
