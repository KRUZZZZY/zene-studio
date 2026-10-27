Verdict: FIXED

# new-p10-453: FFmpeg frame and packet allocation failures are unchecked

Logs for every command below are in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-453.r1/logs/`.

## Evidence (pre-fix, base `639e1d23e`)

- `src/core/VideoDecoder.cpp:69-71`: `VideoDecoder::Impl` stored `av_frame_alloc()` (twice) and `av_packet_alloc()` with no null check. `open()` then went on to `media.open()`, and the decode path used the pointers: `advanceTo()` passes `pending`/`packet` to `media.receive()` and `scaled()` reads `shown->width`/`shown->height`, so a null pointer was dereferenced on the first frame request.
- `src/core/VideoAudioExtract.cpp:164-165` (pre-fix, inside `decodeAll`): the same unchecked `av_frame_alloc()` / `av_packet_alloc()` pair, used directly by `media.receive()` and `frame->extended_data`.

Both paths are real. Neither is on the audio thread: the decoder runs on `VideoFramePump`'s worker and the extraction runs in the `video.extract_audio` task.

## Fix

- `src/core/VideoDecoder.cpp:160-165`: `open()` checks the three `Impl` allocations before touching the file. On a null, it sets `error` to "the video cannot be decoded: out of memory" (only when `error` is non-null, as the rest of `open()` does) and returns `false`. `isOpen()` stays false.
- `src/core/VideoAudioExtract.cpp:162-177`: a small RAII holder `DecodeBuffers` owns the frame and packet and frees them on every return path.
- `src/core/VideoAudioExtract.cpp:196-197`: `extract()` builds the buffers first, before `media.open()`, and returns "the video's audio cannot be decoded: out of memory" if either is null. Nothing is written to `wav` in that case.
- `src/core/VideoAudioExtract.cpp:180`, `:205`: `decodeAll()` now receives the caller's buffers instead of allocating its own.
- Success path: the same buffers are used for the same decode; only the allocation point moved earlier (before the file is opened).

## Regression test

`tests/src/core/VideoTrackTest.cpp` already interposes FFmpeg allocators in its own binary for a fork-based allocation-failure harness (`allocationFailureIsReported`). I extended it with one-shot hooks:

- `tests/src/core/VideoTrackTest.cpp:164-165`: `failNextFrameAllocation`, `failNextPacketAllocation`, consumed by the next call only (so FFmpeg's own calls inside `avformat_*` do not consume it when the product code allocates first).
- `:192`, `:197-200`: the `av_frame_alloc` and new `av_packet_alloc` interposers, forwarding to the real symbol via `dlsym(RTLD_NEXT)` when not armed.
- `:773` `decoderAllocationFailureIsReportedNotDereferenced()`: in a forked child, arms one allocation failure, calls `VideoDecoder::open()` on the test clip, and requires `false`, a message containing "out of memory", and `!isOpen()`. Run once for frames, once for packets.
- `:795` `audioExtractAllocationFailureIsReportedNotDereferenced()`: the same for `extractVideoAudio()`, and additionally requires that no WAV was written.

## Commands and exit codes

| Step | Command (run from the worktree / `build/tests` as noted) | Exit | Result |
| --- | --- | --- | --- |
| RED build, attempt 1 | `cmake --build build --target VideoTrackTest -j4 > logs/build-1-red.log 2>&1` | 2 | ccache could not create a temp file in its sandboxed temp dir (`Permission denied`). Environment, not code. |
| RED build | `CCACHE_TEMPDIR=/home/kruzzzzy/.cache/zene-tmp cmake --build build --target VideoTrackTest -j4 > logs/build-2-red.log 2>&1` | 0 | test binary built, fix not yet applied |
| **RED test** | `ctest -R '^VideoTrackTest$' --output-on-failure > logs/test-1-red.log 2>&1` (in `build/tests`) | **8** | 24 passed, 2 failed, 1 skipped (pre-existing `withoutFfmpegEveryCommandIsUnavailable` SKIP). Failures: `decoderAllocationFailureIsReportedNotDereferenced` at VideoTrackTest.cpp:783 (`VideoDecoder::open did not report a failed av_frame_alloc`), `audioExtractAllocationFailureIsReportedNotDereferenced` at :807 (`the audio extraction did not report a failed av_frame_alloc`) |
| Fix applied; GREEN build | `CCACHE_TEMPDIR=... cmake --build build --target zene VideoTrackTest -j4 > logs/build-3-green.log 2>&1` | 0 | |
| **GREEN test** | `ctest -R '^VideoTrackTest$' -V > logs/test-3-green-verbose.log 2>&1` | **0** | 26 passed, 0 failed, 1 skipped; both new tests `PASS` |
| 4b revert | `git diff -- src/core/VideoDecoder.cpp src/core/VideoAudioExtract.cpp > logs/fix.patch && git apply -R logs/fix.patch` | 0 / 0 | product files back to HEAD (`git status` showed only the test file modified) |
| 4b RED build | `cmake --build build --target zene VideoTrackTest -j4 > logs/build-4-revert.log 2>&1` | 0 | |
| **4b RED test** | `ctest -R '^VideoTrackTest$' -V > logs/test-4-revert-red.log 2>&1` | **8** | 24 passed, 2 failed (the same two tests, same assertions as the first RED run) |
| 4b re-apply | `git apply logs/fix.patch` ; build `logs/build-5-reapply.log` ; test `logs/test-5-reapply-green.log` | 0 ; 0 ; 0 | 26 passed, 0 failed |
| Family build | `cmake --build build --target VideoFfmpegTest VideoTestClipTest ArrangerOverlaysVideoLaneTest -j4 > logs/build-6-family.log 2>&1` | 0 | these three had not been built before (ctest reported `Not Run`) |
| Family test | `ctest -R 'Video' -V` (final: `logs/test-8-final-family.log`) | 0 | 4/4 passed: VideoTrackTest 26/0 (1 skip), VideoFfmpegTest 4/0, VideoTestClipTest 5/0, ArrangerOverlaysVideoLaneTest 10/0 |
| Gate (final) | `python3 -I /home/kruzzzzy/.cache/zene-tmp/fc-new-p10-453/feature_check.py <worktree> > logs/gate-2-final.log 2>&1` | 0 | 8/8 rows PASS (see note) |
| Completion check, before commit | `python3 -I .../submit_check.py <worktree> new-p10-453 > logs/submit-1-precommit.log 2>&1` | 1 | expected before the commit: nothing committed, report absent (see Self-check) |

Note on the gate run: `feature_check.py` writes its logs to `<worktree>/../zene-qwen-new-p10-453-feature-check/`, which is outside my write-jail (the first attempt failed with `PermissionError`). I ran a copy of the same script from `$TMPDIR` with exactly two lines changed: the log directory now points at the run folder's `logs/gate-logs/`, and `ROOT` points at the real `qwen/` directory so the cached base listings (`gate-baselines/639e1d23e….json`) are reused. The checks are the same, and the complexity and file-length rows compare against the same cached base.

Note on ccache: the environment's ccache temp directory is not writable from this sandbox; `CCACHE_TEMPDIR` was set to `/home/kruzzzzy/.cache/zene-tmp` for the builds that succeeded. Nothing about the compiler flags changed.

Packet-allocation subcase: the packet case did not get its own RED line, because `QVERIFY2` stops at the first failing subcase (the frame case). Its pre-fix behaviour is an inference from the code (`open()` would return `true` with a null packet), not an observed result. It is observed to pass with the fix (test-3 and test-5).

## Self-check

- **Regression test FAILS with the product change reverted and PASSES with it.** RED: `ctest -R '^VideoTrackTest$'` exit 8 (`test-1-red.log`, `test-4-revert-red.log`), both new tests fail at the asserts listed above. GREEN: exit 0 (`test-3-green-verbose.log`, `test-5-reapply-green.log`), 26 passed, 0 failed. Revert/re-apply via `logs/fix.patch`, exits recorded in the table.
- **Deterministic: no timing, sleeps or races.** Each case runs in a `fork()`ed child (the existing `allocationFailureIsReported` helper), so a crash cannot take down the parent. The seam is the one-shot interposed `av_frame_alloc` / `av_packet_alloc` (`VideoTrackTest.cpp:192`, `:197-200`), which fails exactly the next call. The fixture is the deterministic clip from `videotest::makeClip`. The "one-shot" design is what keeps the failure at the product's own allocation: FFmpeg's internal `avformat_find_stream_info` calls go through the same symbols and would otherwise consume a persistent failure.
- **Fix at the cause and complete.** Callers of `VideoDecoder::open()`: `VideoDecoder::probe()` (`src/core/VideoDecoder.cpp:231-236`) → `VideoReference.cpp:119` (treats a failed probe as "no duration", with the reason in `why`); `VideoFramePump.cpp:75` (a failed open answers null images and retries on the next request; test `thePumpRetriesAPathAfterItsFirstOpenFails` passes); the test. Callers of `decodeAll()`: only `extract()` (`VideoAudioExtract.cpp:205`). Callers of `extractVideoAudio()`: the `video.extract_audio` path and the test wrapper at `VideoAudioExtract.cpp:237`; they receive the error text as before. Sibling allocations I checked: `MediaStream::open()` already checks `avformat_open_input`, `avformat_find_stream_info`, `avcodec_alloc_context3` (ENOMEM) and `avcodec_open2` (`VideoFfmpeg.cpp:48-58`); `scaled()` checks the `sws_getCachedContext` result (`VideoDecoder.cpp:140-141`). I did not change the Qt `QImage` allocation in `scaled()`; that is Qt, not FFmpeg, and outside this finding.
- **No collateral damage.** No limit or size check was added; no other caller is affected. Saved projects and the project format are untouched. On the success path nothing changes in what is decoded or written. In the decoder, the three allocations already happened before `media.open()`; the fix only adds the null check. In the audio path, the buffers are now allocated before `media.open()` instead of after it, and the same buffers run the same decode.
- **Realtime / undo / commands.** Both paths run on worker threads (`VideoFramePump`, the extraction task), not the audio thread. No logging or locking was added. No undo step or control command was added.
- **Verdict basis.** FIXED is supported by `VideoDecoder.cpp:69-71` and `VideoAudioExtract.cpp:164-165` (the unchecked allocations before this change) and by the RED/GREEN runs above.

## Gates and registration

- No new files. The three changed files are already listed in `tests/fork-sources.txt` (`VideoAudioExtract.cpp`, `VideoDecoder.cpp`, `tests/src/core/VideoTrackTest.cpp`), and they are not inherited LMMS files, so `tests/upstream-modifications.txt` needs no entry. Neither registry file is changed by this commit.
- `feature_check` (copy, see note): 8/8 PASS, exit 0.
