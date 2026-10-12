# Cluster new-p10-419

Verdict: **NEEDS-CONTEXT**

## Evidence

`src/core/audio/TrackRecorder.cpp:302` clamps each writer scratch sample, then line 305 passes the buffer to `sf_writef_float`. Since ordered comparisons with NaN are false, `std::clamp` returns the NaN unchanged. The source-level pass-through described by the finding is present.

A temporary regression sent `quiet_NaN()` through `TrackRecorder` and asserted that the decoded PCM sample was zero. It passed on the configured build without a production change: `test-2.log` records `RecordClipTest::nonFiniteTakeIsSilenced()` passing, and QtTest reports 7 passed, 0 failed. This run shows the current libsndfile conversion produces the proposed safe value for this input. The temporary test was removed because it did not fail on the unfixed code. No production change was made.

The available evidence does not show corrupted PCM in this runtime, while the unchecked NaN still reaches the codec. Context is needed on whether a defensive sanitization change is required despite the current output, or on a supported libsndfile/runtime where the conversion produces an incorrect sample. A regression that fails before the change is needed to validate a behavior fix.

## Validation evidence

All build and test output is in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-419.r1/logs/`.

| Check | Exact command | Result |
|---|---|---|
| Build the temporary regression | `cmake --build build --target RecordClipTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-419.r1/logs/build-1b.log 2>&1; status=$?; echo EXIT=$status` | EXIT=0 |
| Attempted RED run | `ctest -R '^RecordClipTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-419.r1/logs/test-1.log 2>&1; status=$?; echo EXIT=$status` | EXIT=0; not RED, 1/1 CTest test passed |
| Confirm regression case and affected family | `ctest -R '^RecordClipTest$' -V > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-419.r1/logs/test-2.log 2>&1; status=$?; echo EXIT=$status` | EXIT=0; 1/1 CTest test passed; QtTest reports 7 passed, 0 failed, including the temporary NaN case |
| Green run | Not run; no production change was made after the attempted RED passed |
| Complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-419.r1/logs/gate-1.log 2>&1; status=$?; echo EXIT=$status` | EXIT=0 |
| File-length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-419.r1/logs/gate-2.log 2>&1; status=$?; echo EXIT=$status` | EXIT=0 |

## Files committed

- `docs/qwen-fixes-2026-10/cluster-new-p10-419.md` — this NEEDS-CONTEXT report.
