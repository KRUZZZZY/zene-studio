# Cluster new-p10-452: Video seek time conversion

**Verdict: FIXED**

`src/core/VideoFfmpeg.cpp:108-117` now rejects non-finite or out-of-range seek offsets before converting them to `int64_t`. It also checks the addition of the stream start timestamp before forming the seek target. Invalid requests return without seeking or flushing the decoder.

The regression in `tests/src/core/VideoTrackTest.cpp:248-274` decodes 30 frames, calls `MediaStream::seek(DBL_MAX)`, and verifies decoding continues forward. The original implementation fails that assertion.

## Evidence

CTest commands below ran from `build/tests`; build commands ran from the worktree root. Each `EXIT` is the value printed by the command's trailing `echo` for the preceding build or test command.

| Check | Exact command | Exit / result |
|---|---|---|
| Original-code RED build | `cmake --build build --target zene VideoTrackTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-452.r1/logs/build-2.log 2>&1; echo EXIT=$?` | 0 |
| Original-code RED test | `ctest -R '^VideoTrackTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-452.r1/logs/test-2.log 2>&1; echo EXIT=$?` | 8; `next > previous` failed |
| Fixed GREEN build | `cmake --build build --target zene VideoTrackTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-452.r1/logs/build-3.log 2>&1; echo EXIT=$?` | 0 |
| Fixed GREEN test | `ctest -R '^VideoTrackTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-452.r1/logs/test-3.log 2>&1; echo EXIT=$?` | 0; 1/1 CTest target passed |
| Reverse fix for sensitivity check | `git diff -- src/core/VideoFfmpeg.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-452.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-452.r1/logs/fix.patch; echo EXIT=$?` | 0 |
| Reversed-code build | `cmake --build build --target zene VideoTrackTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-452.r1/logs/build-4.log 2>&1; echo EXIT=$?` | 0 |
| Reversed-code RED test | `ctest -R '^VideoTrackTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-452.r1/logs/test-4.log 2>&1; echo EXIT=$?` | 8; same `next > previous` failure |
| Restore fix | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-452.r1/logs/fix.patch; echo EXIT=$?` | 0 |
| Restored-code build | `cmake --build build --target zene VideoTrackTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-452.r1/logs/build-5.log 2>&1; echo EXIT=$?` | 0 |
| Restored-code GREEN test | `ctest -R '^VideoTrackTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-452.r1/logs/test-5.log 2>&1; echo EXIT=$?` | 0; 1/1 CTest target passed |

The initial draft also ran: `cmake --build build --target zene VideoTrackTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-452.r1/logs/build-1.log 2>&1; echo EXIT=$?` (0), then `ctest -R '^VideoTrackTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-452.r1/logs/test-1.log 2>&1; echo EXIT=$?` (8). That draft failed an invalid setup assumption (`previous > 1.0`) and is not counted as regression evidence; the corrected test produced the RED result above.

The affected family count is one CTest target (`VideoTrackTest`), one run per RED/GREEN check. Gate commands were also run before this report was written:

| Gate | Exact command | Exit |
|---|---|---|
| Complexity, initial | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-452.r1/logs/gate-1.log 2>&1; echo EXIT=$?` | 0 |
| File length, initial | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-452.r1/logs/gate-2.log 2>&1; echo EXIT=$?` | 0 |
| Complexity, final post-report | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-452.r1/logs/gate-3.log 2>&1; echo EXIT=$?` | 0 |
| File length, final post-report | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-452.r1/logs/gate-4.log 2>&1; echo EXIT=$?` | 0 |

## Files committed

- `src/core/VideoFfmpeg.cpp`
- `tests/src/core/VideoTrackTest.cpp`
- `docs/qwen-fixes-2026-10/cluster-new-p10-452.md`
