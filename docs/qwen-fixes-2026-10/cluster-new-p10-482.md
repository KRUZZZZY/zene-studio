# Cluster new-p10-482: ALSA playback short writes

**Verdict: FIXED**

## Evidence

`src/core/audio/AudioAlsa.cpp:282-298` renders one period, then routes the ALSA write through `alsa::detail::writeAllFrames`. Negative results still go through the existing `handleError` path. `include/AudioAlsaWrite.h:32-58` repeats writes until no frames remain, advancing the interleaved sample pointer by `writtenFrames * channels` and reducing the remaining frame count. A zero-progress or impossible overlong result returns `-EIO` to avoid spinning or underflowing.

`tests/src/core/AudioAlsaWriteTest.cpp:36-60` injects writes of 2, 1, and 2 frames for a five-frame stereo period. It checks the total, each advanced pointer, and each remaining request.

## Regression and verification

| Evidence | Exact command | Exit | Result |
| --- | --- | ---: | --- |
| RED build | `cmake --build build --target zene AudioAlsaWriteTest -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-482.r1/logs/build-2-red-confirm.log 2>&1; status=$?; echo EXIT=$status` | 0 | Rebuilt `zene` and the regression target against the one-call implementation. |
| RED test | `cd build/tests && ctest -R '^AudioAlsaWriteTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-482.r1/logs/test-1-red.log 2>&1; status=$?; echo EXIT=$status` | 8 | 1 test ran and failed: actual 2 frames, expected 5. |
| GREEN build | `cmake --build build --target zene AudioAlsaWriteTest -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-482.r1/logs/build-3-green.log 2>&1; status=$?; echo EXIT=$status` | 0 | Rebuilt `zene` and the regression target with the loop. |
| GREEN test | `cd build/tests && ctest -R '^AudioAlsaWriteTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-482.r1/logs/test-2-green.log 2>&1; status=$?; echo EXIT=$status` | 0 | 1/1 passed. |
| Reverse-patch proof | `git diff -- include/AudioAlsaWrite.h src/core/audio/AudioAlsa.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-482.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-482.r1/logs/fix.patch; status=$?; echo EXIT=$status` | 0 | Removed only the loop change while keeping the test seam and registration. |
| Reverse-patch rebuild | `cmake --build build --target zene AudioAlsaWriteTest -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-482.r1/logs/build-4-reversal.log 2>&1; status=$?; echo EXIT=$status` | 0 | Rebuilt `zene` and the test with the loop removed. |
| Reverse-patch test | `cd build/tests && ctest -R '^AudioAlsaWriteTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-482.r1/logs/test-3-reversal.log 2>&1; status=$?; echo EXIT=$status` | 8 | Failed again: actual 2 frames, expected 5. |
| Restore patch | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-482.r1/logs/fix.patch; status=$?; echo EXIT=$status` | 0 | Restored the loop. |
| Restore rebuild | `cmake --build build --target zene AudioAlsaWriteTest -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-482.r1/logs/build-5-restored.log 2>&1; status=$?; echo EXIT=$status` | 0 | Rebuilt after restoring the loop. |
| Restore test | `cd build/tests && ctest -R '^AudioAlsaWriteTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-482.r1/logs/test-4-restored.log 2>&1; status=$?; echo EXIT=$status` | 0 | 1/1 passed. |
| Final build | `cmake --build build --target zene AudioAlsaWriteTest -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-482.r1/logs/build-6-final.log 2>&1; status=$?; echo EXIT=$status` | 0 | Final target rebuild passed. |
| Final test | `cd build/tests && ctest -R '^AudioAlsaWriteTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-482.r1/logs/test-5-final.log 2>&1; status=$?; echo EXIT=$status` | 0 | Affected family: 1 test executed, 1/1 passed. |
| Complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-482.r1/logs/gate-1-complexity.log 2>&1; status=$?; echo EXIT=$status` | 0 | Passed. |
| File-length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-482.r1/logs/gate-2-file-length.log 2>&1; status=$?; echo EXIT=$status` | 0 | Passed. |

The `fff` grep tool was not available in this session; build, test, and gate logs were searched with `rg` using the required error/failure terms.

## Files committed

- `include/AudioAlsaWrite.h` — added the allocation-free complete-period write helper.
- `src/core/audio/AudioAlsa.cpp` — used the helper in ALSA playback.
- `tests/src/core/AudioAlsaWriteTest.cpp` — added deterministic short-write regression.
- `tests/CMakeLists.txt` — registered the test target.
- `tests/fork-sources.txt` — registered both new source files.
- `tests/upstream-modifications.txt` — recorded reasons for the inherited source and test-registration changes.
- `docs/qwen-fixes-2026-10/cluster-new-p10-482.md` — this evidence report.
