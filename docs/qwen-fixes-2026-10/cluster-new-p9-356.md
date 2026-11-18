# Cluster new-p9-356: pitch onset timestamps

Verdict: **FIXED**

`src/core/PitchTrack.cpp:130` adds a periodicity error measure and `:149` scans candidate boundaries around a key change, matching the previous and next pitches on opposite sides. `Segmenter::onsetFor` at `:185` keeps the most recent pitch estimate across a short unvoiced hop and accepts a boundary only when audio is active on both sides. This lets legato changes use their transition time while the existing gated scan still handles silence-to-note starts.

The regression at `tests/src/core/AudioAnalysisTest.cpp:124` switches from a phase-continuous 220 Hz tone to 330 Hz at frame 24255 and checks the second note within 5 ms. The silence-to-note regression remains at `:111`.

## Verification evidence

| Check | Command / result |
|---|---|
| Red proof build | `cmake --build build --target zene AudioAnalysisTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-356.r2/logs/build-12.log 2>&1; rc=$?; echo EXIT=$rc; exit $rc` returned **0**. |
| Product-only revert red | With the regression present and the product patch reversed, `ctest -R '^AudioAnalysisTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-356.r2/logs/test-12.log 2>&1; rc=$?; echo EXIT=$rc; exit $rc` from `build/tests` returned **8**. The legato assertion reported start 24576 vs. transition 24255; the suite reported 9 passed and 1 failed. |
| Restore product patch | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-356.r2/logs/fix.patch; rc=$?; echo EXIT=$rc; exit $rc` returned **0**. |
| Green build | `cmake --build build --target zene AudioAnalysisTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-356.r2/logs/build-13.log 2>&1; rc=$?; echo EXIT=$rc; exit $rc` returned **0**. |
| Green test | `ctest -R '^AudioAnalysisTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-356.r2/logs/test-13.log 2>&1; rc=$?; echo EXIT=$rc; exit $rc` from `build/tests` returned **0**: **1/1** affected CTest test passed, including both onset regressions. |
| Complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-356.r2/logs/gate-3.log 2>&1; rc=$?; echo EXIT=$rc; exit $rc` returned **0**. |
| File-length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-356.r2/logs/gate-4.log 2>&1; rc=$?; echo EXIT=$rc; exit $rc` returned **0**. |

The final proof patch was generated with `git diff -- src/core/PitchTrack.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-356.r2/logs/fix.patch; rc=$?; echo EXIT=$rc; exit $rc` (**0**) and reversed with `git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-356.r2/logs/fix.patch; rc=$?; echo EXIT=$rc; exit $rc` (**0**) before the red build and test above.

## Files in this fix commit

- `src/core/PitchTrack.cpp`
- `tests/src/core/AudioAnalysisTest.cpp`
- `docs/qwen-fixes-2026-10/cluster-new-p9-356.md`
