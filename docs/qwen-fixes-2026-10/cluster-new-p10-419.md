# Cluster new-p10-419

Verdict: **FIXED**

## Evidence

`src/core/audio/TrackRecorder.cpp:48` now maps NaN to zero in
`recorder_detail::prepareSampleForPcmWrite()`. Every writer scratch sample passes
through that function at line 318 immediately before `sf_writef_float()` at line
321. The existing `std::clamp` still handles finite values and infinities, so
positive and negative infinity become +1 and -1 respectively.

`tests/src/core/RecordClipTest.cpp:175` tests that exact pre-write helper with a
quiet NaN, both infinities, out-of-range finite samples, and an in-range sample.
It does not depend on libsndfile's PCM conversion behavior.

The affected CTest family contains one registered test, `RecordClipTest`. The
RED runs failed that test on the new NaN assertion; the green run passed it.

## Verification commands and results

Each CTest command ran from `build/tests`. Build and test logs are in
`/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-419.r2/logs/`.

| Check | Exact command | Exit |
|---|---|---:|
| RED build, clamp-only helper | `cmake --build build --target zene RecordClipTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-419.r2/logs/build-3.log 2>&1; cmd_exit=$?; printf 'EXIT=%s\n' "$cmd_exit" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-419.r2/logs/build-3.log; exit "$cmd_exit"` | 0 |
| RED test | `ctest -R '^RecordClipTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-419.r2/logs/test-red-1.log 2>&1; cmd_exit=$?; printf 'EXIT=%s\n' "$cmd_exit" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-419.r2/logs/test-red-1.log; exit "$cmd_exit"` | 8 (1/1 failed) |
| Save fix-only patch | `git diff -- src/core/audio/TrackRecorder.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-419.r2/logs/fix.patch` | 0 |
| Remove fix for sensitivity check | `git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-419.r2/logs/fix.patch` | 0 |
| No-fix rebuild | `cmake --build build --target zene RecordClipTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-419.r2/logs/build-4-proof-red.log 2>&1; cmd_exit=$?; printf 'EXIT=%s\n' "$cmd_exit" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-419.r2/logs/build-4-proof-red.log; exit "$cmd_exit"` | 0 |
| No-fix test | `ctest -R '^RecordClipTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-419.r2/logs/test-2-proof-red.log 2>&1; cmd_exit=$?; printf 'EXIT=%s\n' "$cmd_exit" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-419.r2/logs/test-2-proof-red.log; exit "$cmd_exit"` | 8 (1/1 failed) |
| Restore fix | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-419.r2/logs/fix.patch` | 0 |
| GREEN rebuild | `cmake --build build --target zene RecordClipTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-419.r2/logs/build-5-green.log 2>&1; cmd_exit=$?; printf 'EXIT=%s\n' "$cmd_exit" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-419.r2/logs/build-5-green.log; exit "$cmd_exit"` | 0 |
| GREEN test | `ctest -R '^RecordClipTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-419.r2/logs/test-3-green.log 2>&1; cmd_exit=$?; printf 'EXIT=%s\n' "$cmd_exit" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-419.r2/logs/test-3-green.log; exit "$cmd_exit"` | 0 (1/1 passed) |
| Complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-419.r2/logs/complexity-gate-final.log 2>&1; cmd_exit=$?; printf 'EXIT=%s\n' "$cmd_exit" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-419.r2/logs/complexity-gate-final.log; exit "$cmd_exit"` | 0 |
| File-length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-419.r2/logs/file-length-gate-final.log 2>&1; cmd_exit=$?; printf 'EXIT=%s\n' "$cmd_exit" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-419.r2/logs/file-length-gate-final.log; exit "$cmd_exit"` | 0 |

Two earlier builds were interrupted while removing an unnecessary public-header
test seam. Both returned exit 130 and are not counted as verification:

- `cmake --build build --target zene RecordClipTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-419.r2/logs/build-1.log 2>&1; cmd_exit=$?; printf 'EXIT=%s\n' "$cmd_exit" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-419.r2/logs/build-1.log; exit "$cmd_exit"` — exit 130.
- `cmake --build build --target zene RecordClipTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-419.r2/logs/build-2.log 2>&1; cmd_exit=$?; printf 'EXIT=%s\n' "$cmd_exit" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-419.r2/logs/build-2.log; exit "$cmd_exit"` — exit 130.

The completed builds above rebuilt `zene` and `RecordClipTest`.

## Files committed

- `src/core/audio/TrackRecorder.cpp` — NaN mapping before PCM write.
- `tests/src/core/RecordClipTest.cpp` — deterministic pre-write regression.
- `docs/qwen-fixes-2026-10/cluster-new-p10-419.md` — this report.
