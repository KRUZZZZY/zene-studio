# Cluster new-p10-36: NaN strip settings pass range checks

**Verdict: FIXED**

`mixer.set_strip` previously let NaN values pass the range comparisons and reach `applySettings`. The new `finiteStripSettings` check rejects nonfinite trim, pan, right pan, and width values before the range checks, undo step, or model writes.

## Evidence

- [src/core/ControlCommandsMixerStrip.cpp:210](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p10-36/src/core/ControlCommandsMixerStrip.cpp:210) checks all four numeric strip settings with `std::isfinite`.
- [src/core/ControlCommandsMixerStrip.cpp:240](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p10-36/src/core/ControlCommandsMixerStrip.cpp:240) rejects nonfinite settings before applying the update.
- [tests/src/core/MixerStripTest.cpp:157](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p10-36/tests/src/core/MixerStripTest.cpp:157) checks NaN for all four fields and expects `InvalidArgs`.

## Verification

All build and test output was written to new files in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-36.r1/logs`. Build and test commands below were run with output redirected to their named log and an `EXIT` line appended.

| Phase | Exact command | Exit | Result |
| --- | --- | ---: | --- |
| Original-code RED build | `cmake --build build --target zene MixerStripTest` (`build-2.log`) | 0 | Both executable targets rebuilt before trusting the failure. |
| Original-code RED test | `ctest -R MixerStripTest --output-on-failure` from `build/tests` (`test-2.log`) | 8 | 1/1 CTest test failed: NaN `trim_db` returned error kind 0 instead of `InvalidArgs`. |
| Reversal proof RED | `git diff -- src/core/ControlCommandsMixerStrip.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-36.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-36.r1/logs/fix.patch` (`proof-3.log`); then `cmake --build build --target zene MixerStripTest` (`build-6.log`); then `ctest -R MixerStripTest --output-on-failure` from `build/tests` (`test-6.log`) | 0; 0; 8 | With the production patch removed, the regression failed again; 1/1 CTest test executed. |
| Reapply proof GREEN | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-36.r1/logs/fix.patch` (`proof-4.log`); then `cmake --build build --target zene MixerStripTest` (`build-7.log`); then `ctest -R MixerStripTest --output-on-failure` from `build/tests` (`test-7.log`) | 0; 0; 0 | The regression passed; affected family count: 1/1 CTest test. |
| Complexity gate | `bash tests/complexity-gate.sh --check` (`gate-5.log`) | 0 | Passed. |
| File-length gate | `bash tests/file-length-gate.sh --check` (`gate-6.log`) | 0 | Passed. |

The first complexity-gate run (`gate-1.log`) exited 1 because inline checks raised `setStrip` to CCN 13. The finite checks were moved to the helper at line 210; the final complexity-gate run passed. The fff grep tool was unavailable in this environment, so logs were searched with `rg -n` using the requested terms.

## Files included in the commit

- `src/core/ControlCommandsMixerStrip.cpp`
- `tests/src/core/MixerStripTest.cpp`
- `docs/qwen-fixes-2026-10/cluster-new-p10-36.md`
