# Cluster new-p10-41 — FIXED

## Evidence

`src/core/ControlCommandsAudioAnalysis.cpp:124-133` now mixes stereo samples while filling each 2048-frame FFT input. `:154-164` derives the available frame count and row positions from the clip's `SampleWindow`, so `sample.spectrogram` no longer creates a mono vector for the full clip. `windowMono()` remains for audio-to-MIDI at `:84`.

`tests/src/core/AudioAnalysisTest.cpp:158-175` requests two rows from a one-minute clip and asserts that no single allocation reaches 1 MiB. With the original code the assertion failed at 10,584,000 bytes. Removing the production patch reproduced the same failure; restoring it passed.

Each `ctest -R AudioAnalysisTest` run selected one CTest test, `AudioAnalysisTest` (Test #186): 1/1 executed. The suite's original-code and inverse-patch runs failed as expected; both fixed-code runs passed.

## Commands and results

`EXIT` below is the value printed by each command's `echo EXIT=$?`.

| Check | Exact command | Result |
|---|---|---|
| Build original-code regression | `cmake --build build --target AudioAnalysisTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-41.r1/logs/build-1.log 2>&1; echo EXIT=$?` | 0 |
| RED on original code | `ctest -R AudioAnalysisTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-41.r1/logs/test-1.log 2>&1; echo EXIT=$?` | 8; assertion measured 10,584,000 bytes |
| Build fixed code | `cmake --build build --target zene AudioAnalysisTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-41.r1/logs/build-2.log 2>&1; echo EXIT=$?` | 0 |
| GREEN with fix | `ctest -R AudioAnalysisTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-41.r1/logs/test-2.log 2>&1; echo EXIT=$?` | 0; 1/1 passed |
| Remove production fix | `git diff -- src/core/ControlCommandsAudioAnalysis.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-41.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-41.r1/logs/fix.patch; echo EXIT=$?` | 0 |
| Rebuild without fix | `cmake --build build --target zene AudioAnalysisTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-41.r1/logs/build-3.log 2>&1; echo EXIT=$?` | 0 |
| RED after removing fix | `ctest -R AudioAnalysisTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-41.r1/logs/test-3.log 2>&1; echo EXIT=$?` | 8; same 10,584,000-byte assertion |
| Restore production fix | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-41.r1/logs/fix.patch; echo EXIT=$?` | 0 |
| Rebuild restored fix | `cmake --build build --target zene AudioAnalysisTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-41.r1/logs/build-4.log 2>&1; echo EXIT=$?` | 0 |
| GREEN after restoring fix | `ctest -R AudioAnalysisTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-41.r1/logs/test-4.log 2>&1; echo EXIT=$?` | 0; 1/1 passed |
| Complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-41.r1/logs/gate-1.log 2>&1; echo EXIT=$?` | 0 |
| File length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-41.r1/logs/gate-2.log 2>&1; echo EXIT=$?` | 0 |

## Files committed

- `src/core/ControlCommandsAudioAnalysis.cpp`
- `tests/src/core/AudioAnalysisTest.cpp`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p10-41.md`
