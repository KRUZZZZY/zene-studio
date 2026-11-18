# Cluster new-p12-289

Verdict: **FIXED**

`MediaStream::open()` now writes the FFmpeg error text only when its optional `error` pointer is non-null. It still closes the partially opened stream and returns `false` on failure. The regression uses a missing file and a null error pointer.

## File evidence

- `src/core/VideoFfmpeg.cpp:57-61` — failure path guards the error assignment, closes the stream, and returns false.
- `tests/src/core/VideoTrackTest.cpp:155-164` — `failedOpenAcceptsNullErrorOutput()` covers a failed open with `nullptr` and verifies the stream remains closed.

## Evidence

The affected ctest family is `VideoTrackTest`: one registered CTest test was run per invocation. The original implementation and the proof run with the fix removed both segfaulted; the fixed implementation passed.

| Evidence | Command (working directory) | Exit |
| --- | --- | ---: |
| RED on original code (`test-1.log`) | `ctest -R VideoTrackTest --output-on-failure` (`build/tests`) | 8 |
| GREEN with fix (`test-2.log`) | `ctest -R VideoTrackTest --output-on-failure` (`build/tests`) | 0 |
| RED after reversing `fix.patch` (`test-3.log`) | `ctest -R VideoTrackTest --output-on-failure` (`build/tests`) | 8 |
| GREEN after reapplying `fix.patch` (`test-4.log`) | `ctest -R VideoTrackTest --output-on-failure` (`build/tests`) | 0 |
| Complexity gate (`gate-1.log`) | `bash tests/complexity-gate.sh --check` (worktree root) | 0 |
| File length gate (`gate-2.log`) | `bash tests/file-length-gate.sh --check` (worktree root) | 0 |

Each of the four builds used `cmake --build build --target VideoTrackTest zene` from the worktree root and exited 0. Their logs are `build-1.log` through `build-4.log`; this rebuilt both the affected test target and `zene` before each ctest run.

The fix-removal command was `git diff -- src/core/VideoFfmpeg.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-289.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-289.r1/logs/fix.patch` (exit 0). The restoration command was `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-289.r1/logs/fix.patch` (exit 0).

The `fff` grep tool was unavailable in this environment, so the required `error`, `FAILED`, and `Assertion` log searches used `rg -n -i` separately. The red test logs show `VideoTrackTest` segfaulted and `0% tests passed, 1 tests failed out of 1`; the green test logs show `100% tests passed, 0 tests failed out of 1`. No `Assertion` matches were found.

## Files committed

- `src/core/VideoFfmpeg.cpp`
- `tests/src/core/VideoTrackTest.cpp`
- `docs/qwen-fixes-2026-10/cluster-new-p12-289.md`
