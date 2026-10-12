# Cluster new-p10-150

Verdict: **FIXED**

`FoldbackDistortion::nextSample` in `include/DspEffectLibrary.h:291` now returns
the gain-scaled input when the threshold is exactly zero. This bypass avoids
calling `std::fmod` with a zero divisor; nonzero thresholds continue through
the original foldback calculation (`include/DspEffectLibrary.h:297`). The
inherited header is recorded in `tests/upstream-modifications.txt:1054`.

The regression is
`DeviceDspTest::zeroThresholdFoldbackDistortionBypassesSafely`
(`tests/src/core/DeviceDspTest.cpp:208`). On the original implementation it
failed because the output was not finite. Removing the production patch after
the fix reproduced the same failure, and reapplying it made the suite pass.
This configured tree registers `DeviceDspTest`; `Lb302` is a reference module,
not a standalone CTest name.

## Evidence

Commands below ran in the worktree unless noted. Build and test stdout/stderr
went to the named log in
`/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-150.r1/logs`;
the unpiped shell exit status was appended to each log.

| Phase | Exact command | EXIT |
|---|---|---:|
| Initial test compile (const mismatch in the new test; corrected before regression run) | `cmake --build build --target zene DeviceDspTest` (`build-red-1.log`) | 2 |
| Original-code build | `cmake --build build --target zene DeviceDspTest` (`build-red-2.log`) | 0 |
| Original-code regression | `ctest -R '^DeviceDspTest$' --output-on-failure` from `build/tests` (`test-red-1.log`) | 8 |
| Fixed-code build | `cmake --build build --target zene DeviceDspTest` (`build-green-1.log`) | 0 |
| Fixed-code regression | `ctest -R '^DeviceDspTest$' --output-on-failure` from `build/tests` (`test-green-1.log`) | 0 |
| Remove the production fix | `git diff -- include/DspEffectLibrary.h > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-150.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-150.r1/logs/fix.patch` (`proof-remove-1.log`) | 0 |
| Rebuild with fix removed | `cmake --build build --target zene DeviceDspTest kicker lb302 stereoenhancer bassbooster partc_ref_kicker partc_ref_lb302 partc_ref_stereoenhancer partc_ref_bassbooster` (`build-proof-red-1.log`) | 0 |
| Regression with fix removed | `ctest -R '^DeviceDspTest$' --output-on-failure` from `build/tests` (`test-proof-red-1.log`) | 8 |
| Reapply the production fix | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-150.r1/logs/fix.patch` (`proof-restore-1.log`) | 0 |
| Rebuild with fix restored | `cmake --build build --target zene DeviceDspTest kicker lb302 stereoenhancer bassbooster partc_ref_kicker partc_ref_lb302 partc_ref_stereoenhancer partc_ref_bassbooster` (`build-proof-green-1.log`) | 0 |
| Regression with fix restored | `ctest -R '^DeviceDspTest$' --output-on-failure` from `build/tests` (`test-proof-green-1.log`) | 0 |
| Complexity gate | `bash tests/complexity-gate.sh --check` (`gate-complexity-2.log`) | 0 |
| File-length gate | `bash tests/file-length-gate.sh --check` (`gate-file-length-2.log`) | 0 |

The affected CTest family ran **1 test** (`DeviceDspTest`): 1/1 passed on each
fixed-code run. Both original-code runs failed at
`zeroThresholdFoldbackDistortionBypassesSafely()` on
`QVERIFY(std::isfinite(output))`.

The `fff` grep tool was unavailable in this session; the log folder was
searched with `rg` for `error`, `FAILED`, and `Assertion`.
