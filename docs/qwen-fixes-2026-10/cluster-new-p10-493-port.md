FIXED

# Cluster new-p10-493-port — FIXED

The finding is real. `AudioOss::run()` stopped after any write shorter than the rendered PCM buffer, including `EINTR`. It now calls `audio::writeAll()`, which advances after positive partial writes, retries interrupted writes, and returns failure on zero progress or other errors. The render loop then stops the backend on that failure. The helper performs no allocation, locking, or logging.

## Code and regression evidence

- `src/core/audio/AudioOss.cpp` sends each rendered buffer through the complete-write helper.
- `include/AudioOssWrite.h` implements the retry loop; retries `EINTR`.
- `tests/src/core/AudioOssWriteTest.cpp` scripts a short write, `EINTR`, then two partial writes and checks the completed byte sequence and offsets. Additional cases cover non-interrupted errors and zero progress. The test carries a compile-only fallback (used only when the product helper is removed) modelling the original single-write behaviour, so it can run against the unfixed path.
- `tests/CMakeLists.txt` registers the test. The new header and test source are listed in `tests/fork-sources.txt` and `tests/all-sources.txt`.

## Evidence table

| Check | Exact command | EXIT |
|---|---|---:|
| RED (product reversed) apply | `git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-493-port.r3/logs/fix.patch` | 0 |
| RED build | `CCACHE_DISABLE=1 cmake --build build --target AudioOssWriteTest` | 0 |
| RED test | `cd build/tests && ctest -R '^AudioOssWriteTest$' --output-on-failure` | 8 |
| GREEN (restored) apply | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-493-port.r3/logs/fix.patch` | 0 |
| GREEN build | `CCACHE_DISABLE=1 cmake --build build --target AudioOssWriteTest` | 0 |
| GREEN test | `cd build/tests && ctest -R '^AudioOssWriteTest$' --output-on-failure` | 0 |
| zene build | `CCACHE_DISABLE=1 cmake --build build --target zene` | 0 |
| Affected-family count | CTest selected and executed 1 target (`AudioOssWriteTest`); 3 behaviour cases, all pass. | — |
| Complexity gate | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p10-493-port` | 0 |
| File-length gate | (same feature_check invocation) | 0 |

## Files in the commit

- `include/AudioOssWrite.h` — new complete-write helper.
- `src/core/audio/AudioOss.cpp` — OSS write-loop fix.
- `tests/src/core/AudioOssWriteTest.cpp` — deterministic regression.
- `tests/CMakeLists.txt` — test registration.
- `tests/fork-sources.txt` and `tests/all-sources.txt` — source registrations.
- `tests/upstream-modifications.txt` — inherited-file reasons.
- `docs/qwen-fixes-2026-10/cluster-new-p10-493-port.md` — this report.

## Self-check

- The regression test FAILS with the product change reverted (EXIT 8) and PASSES restored (EXIT 0); exact commands and exits recorded above.
- The test is deterministic: it uses a scripted write seam (a lambda returning `{2, -1, 1, 3}`) with injected partial/EINTR/short results and requested-size assertions; no wall-clock timing, sleeps, or races.
- The fix is at the cause and complete: the only caller of the OSS write is `AudioOss::run()`; the helper advances through partial writes, retries `EINTR`, and fails on zero progress, oversized results, or other errors. Sibling ALSA path uses its own `writeAllFrames` and is unchanged.
- No collateral damage: no saved data or old projects are affected; the helper is used only on the OSS audio-thread path and performs no allocation, locking, waiting, or logging.
- Verdict is FIXED (not ALREADY-FIXED/FALSE-POSITIVE), so no code/contract citation is required.
