# cluster new-p10-438 — Failed FFmpeg allocations are dereferenced

Verdict: **FIXED**

## The bug

`tests/src/core/VideoTestClip.h` makes the two-second test clip with libavcodec when the
ffmpeg program is absent. Three allocation results were used without being checked:

- `allocateFrame` (VideoTestClip.h:99): `e.frame = av_frame_alloc();` then immediately
  `e.frame->format = ...` — `av_frame_alloc()` returns null on OOM, so the null is
  dereferenced and the test process crashes instead of reporting the clip could not be made.
- `addStream` (VideoTestClip.h:113-114): `e.stream = avformat_new_stream(...)` and
  `e.codec = avcodec_alloc_context3(codec)` are used (`setup(e.codec)`,
  `e.stream->codecpar`) without checking either result.

## The fix

Null-check each allocation result and return failure before using it:

- `tests/src/core/VideoTestClip.h:100` — `if (e.frame == nullptr) { return false; }` after
  `av_frame_alloc()`.
- `tests/src/core/VideoTestClip.h:116` — `if (e.stream == nullptr || e.codec == nullptr) { return false; }`
  after `avformat_new_stream` / `avcodec_alloc_context3`.

Behaviour-preserving otherwise: the success path is unchanged; only the failure path now
returns false instead of dereferencing null.

## The regression test

`tests/src/core/VideoTrackTest.cpp` — new case `failedAllocationsAreReportedNotCrashed()`.
It interposes libc's `posix_memalign` from the test executable (default visibility, exported
via the `ENABLE_EXPORTS` line added to the `VideoTrackTest` target in `tests/CMakeLists.txt`),
so libavutil's `av_mallocz` resolves through it and every libavutil allocation fails — the
way an out-of-memory run would. The failing call runs in a forked child so a buggy null
dereference shows up as a clean assertion failure rather than killing the test binary.

## Evidence

| Step | Command (from worktree / build/tests) | EXIT |
|---|---|---|
| RED — fix reverted | `git diff -- tests/src/core/VideoTestClip.h > .../fix.patch && git apply -R .../fix.patch` | 0 |
| rebuild | `cmake --build build --target VideoTrackTest > .../build-8.log 2>&1` | 0 |
| run | `QT_QPA_PLATFORM=offscreen ctest -R '^VideoTrackTest$' --output-on-failure > .../test-8.log 2>&1` | **8** |
| GREEN — fix re-applied | `git apply .../fix.patch` | 0 |
| rebuild | `cmake --build build --target VideoTrackTest > .../build-9.log 2>&1` | 0 |
| run | `QT_QPA_PLATFORM=offscreen ctest -R '^VideoTrackTest$' --output-on-failure > .../test-9.log 2>&1` | **0** |

RED log (test-8.log) failing assertion:
`FAIL! : VideoTrackTest::failedAllocationsAreReportedNotCrashed() 'failedAllocationIsReported([&frameCase] { return videotest::allocateFrame(frameCase); })' returned FALSE. (allocateFrame dereferenced the failed av_frame_alloc)`

GREEN log (test-9.log): `100% tests passed, 0 tests failed out of 1`.

Affected-family count: `ctest -R '^VideoTrackTest$'` runs 1 ctest; the binary holds 14 cases
(RED: 12 passed, 1 failed, 1 skipped; GREEN: 13 passed, 1 skipped — the skip is the
no-FFmpeg-build case, which this build has FFmpeg so it skips).

Gates:
- `bash tests/complexity-gate.sh --check` → EXIT=0
- `bash tests/file-length-gate.sh --check` → EXIT=0

## Files committed

- `tests/src/core/VideoTestClip.h` — the fix (null checks).
- `tests/src/core/VideoTrackTest.cpp` — the regression test + interposition helper.
- `tests/CMakeLists.txt` — `ENABLE_EXPORTS` on the `VideoTrackTest` target (test link flag only).
- `tests/upstream-modifications.txt` — reason for the `tests/CMakeLists.txt` change.
- `docs/qwen-fixes-2026-10/cluster-new-p10-438.md` — this report.

`VideoTestClip.h` and `VideoTrackTest.cpp` were already registered in `tests/fork-sources.txt`
(fork-new); `tests/CMakeLists.txt` is inherited and now carries the new reason in
`tests/upstream-modifications.txt`.
