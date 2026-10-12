# cluster new-p10-438 — Failed FFmpeg allocations are dereferenced

Verdict: **FIXED**

## Finding and product fix

The allocation failures in `tests/src/core/VideoTestClip.h` were real. `allocateFrame` now
returns false at line 100 when `av_frame_alloc()` returns null. `addStream` returns false at line
116 if either `avformat_new_stream()` or `avcodec_alloc_context3()` returns null, before either
pointer is dereferenced. These production checks landed in the preceding round 1 commit
`2861e4e67`.

## Round 2 regression changes

`tests/src/core/VideoTrackTest.cpp:53` now guards the POSIX dynamic symbol hooks and fork isolation
to Linux and macOS; other platforms, including FFmpeg-enabled Windows, compile the test without
those headers or APIs and skip only this allocation-injection case. The test interposes
`av_frame_alloc` and `avcodec_alloc_context3`. Its `avformat_new_stream` hook arms codec failure
only after the real call returns a non-null stream. The codec assertion at line 445 requires
`addStream` to return false with `stream != nullptr` and `codec == nullptr`. Each fault case runs in
a child so the reversed-check crash becomes a normal test failure.

`tests/CMakeLists.txt:3789` keeps `ENABLE_EXPORTS` on `VideoTrackTest` for the hooks. Its inherited
file reason is recorded in `tests/upstream-modifications.txt:764`.

## Evidence

The fix patch was extracted from the already committed production change because the product file
was not modified in round 2:

| Command | Exit |
|---|---:|
| `git show HEAD -- tests/src/core/VideoTestClip.h > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-438.r2/logs/fix.patch` | 0 |
| `git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-438.r2/logs/fix.patch` | 0 |
| `cmake --build build --target zene VideoTrackTest -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-438.r2/logs/build-9.log 2>&1; echo BUILD_EXIT=$?` | 0 |
| From `build/tests`: `QT_QPA_PLATFORM=offscreen ctest -R '^VideoTrackTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-438.r2/logs/test-8.log 2>&1; echo EXIT=$?` | **8 (RED)** |
| `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-438.r2/logs/fix.patch` | 0 |
| `cmake --build build --target zene VideoTrackTest -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-438.r2/logs/build-10.log 2>&1; echo BUILD_EXIT=$?` | 0 |
| From `build/tests`: `QT_QPA_PLATFORM=offscreen ctest -R '^VideoTrackTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-438.r2/logs/test-9.log 2>&1; echo EXIT=$?` | **0 (GREEN)** |
| From `build/tests`: `QT_QPA_PLATFORM=offscreen ctest -R '^VideoTrackTest$' --output-on-failure -V > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-438.r2/logs/test-10.log 2>&1; echo EXIT=$?` | 0 |
| `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-438.r2/logs/gate-3.log 2>&1; echo EXIT=$?` | 0 |
| `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-438.r2/logs/gate-4.log 2>&1; echo EXIT=$?` | 0 |

With the product guards reversed, the codec-first regression crashed in the child and failed with
the assertion “codec allocation failure was not reported after creating its stream.” After
restoration, the verbose run reports 13 passed, 0 failed, 1 skipped inside the single registered
`VideoTrackTest` ctest. The skip is the existing no-FFmpeg case; FFmpeg is enabled in this build.

The early fail-all hook iteration was discarded because it also caused stream creation to fail;
the final hook injects the codec failure only after stream creation succeeds. Intermediate logs
remain in the same log directory. The `fff` grep tool was unavailable in this environment, so logs
were searched with `rg`.

## Files in the round 2 commit

- `tests/src/core/VideoTrackTest.cpp` — platform guard and independent FFmpeg allocation failures.
- `tests/CMakeLists.txt` — accurate hook comment; retains `ENABLE_EXPORTS`.
- `tests/upstream-modifications.txt` — updated reason for the inherited CMake file.
- `docs/qwen-fixes-2026-10/cluster-new-p10-438.md` — this evidence report.

The production null checks remain in `tests/src/core/VideoTestClip.h`, already committed in round 1.
