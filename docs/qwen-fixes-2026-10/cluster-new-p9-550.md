# Cluster new-p9-550

Verdict: **FIXED**

## Finding and change

- [include/AudioBufferView.h:232](../../include/AudioBufferView.h#L232): iterator subtraction now divides the sample-pointer difference by the iterator's channel count. The count is converted to the signed `difference_type`, so reverse distances stay negative; zero-channel iterators return zero without dividing by zero.
- [AudioResamplerRatioTest.cpp:134](../../tests/src/core/AudioResamplerRatioTest.cpp#L134): regression checks `std::ranges::distance` for three stereo frames in both directions with static and dynamic channel counts.
- [tests/upstream-modifications.txt:1054](../../tests/upstream-modifications.txt#L1054): records the inherited header change and its reason. The regression file is already registered in `tests/fork-sources.txt`.

## Regression evidence

The RED assertion reported actual distance `6`, expected `3`.

| Run | Command | Exit |
| --- | --- | ---: |
| Baseline build | `cmake --build build --target zene AudioResamplerRatioTest` (log `build-2.log`) | 0 |
| RED | From `build/tests`: `ctest -R '^AudioResamplerRatioTest$' --output-on-failure` (log `test-1.log`) | 8 |
| Fixed build | `cmake --build build --target zene AudioResamplerRatioTest` (log `build-3.log`) | 0 |
| GREEN | From `build/tests`: `ctest -R '^AudioResamplerRatioTest$' --output-on-failure` (log `test-2.log`) | 0 |

The required reverse-patch proof also reproduced RED, then passed after restoring the patch:

| Step | Command | Exit |
| --- | --- | ---: |
| Save and reverse product patch, rebuild | `git diff -- include/AudioBufferView.h > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-550.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-550.r1/logs/fix.patch && cmake --build build --target zene AudioResamplerRatioTest --parallel 4` (log `build-4.log`) | 0 |
| Reversed RED | From `build/tests`: `ctest -R '^AudioResamplerRatioTest$' --output-on-failure` (log `test-3.log`) | 8 |
| Restore product patch and rebuild | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-550.r1/logs/fix.patch && cmake --build build --target zene AudioResamplerRatioTest --parallel 4` (log `build-5.log`) | 0 |
| Restored GREEN | From `build/tests`: `ctest -R '^AudioResamplerRatioTest$' --output-on-failure` (log `test-4.log`) | 0 |

The affected CTest family ran **1/1** registered test (`AudioResamplerRatioTest`). The final test-source rebuild and rerun were `cmake --build build --target AudioResamplerRatioTest` (log `build-8.log`, exit 0) and the same `ctest` command (log `test-5.log`, exit 0; 100% passed).

## Build coverage and gates

- Generated dependency files identified `zene`, `zene_api`, and `AudioResamplerRatioTest` as named targets depending on `AudioBufferView.h`; `zene` also builds its `lmmsobjs` dependency. The changed production header was rebuilt with `zene`, `zene_api` was built with `cmake --build build --target zene_api` (log `build-7.log`, exit 0), and the test target was rebuilt as recorded above.
- `cmake --build build --parallel 4` (log `build-6.log`) returned **97** because the workspace bunny guard rejects unscoped full builds and requires named targets. The identified named targets were built instead.
- `bash tests/complexity-gate.sh --check` (log `gate-3.log`): **0**.
- `bash tests/file-length-gate.sh --check` (log `gate-4.log`): **0**.

An initial regression draft used dynamic-iterator `begin + 3`; `cmake --build build --target zene AudioResamplerRatioTest` (log `build-1.log`) exited 2 because that separately instantiated the existing dynamic `operator+` path's invalid `Base::channels()` call. The test now constructs its dynamic endpoint directly, so it isolates subtraction. That unrelated code path was not changed.

The requested `fff` grep tool was not present in the available tools. I searched the log folder with targeted `rg -n -i` searches for `error`, `FAILED`, and `Assertion` instead.

All build, test, and gate logs are in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-550.r1/logs`.

## Files committed

- `include/AudioBufferView.h`
- `tests/src/core/AudioResamplerRatioTest.cpp`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p9-550.md`
