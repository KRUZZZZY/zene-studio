Verdict: FIXED

# new-p10-453: FFmpeg frame and packet allocation failures are unchecked

Logs: round 1 in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-453.r1/logs/`; round 2 (review rework) in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-453.r2/logs/`.

## Evidence (pre-fix, base `639e1d23e`)

- `src/core/VideoDecoder.cpp:69-71`: `VideoDecoder::Impl` stored `av_frame_alloc()` (twice) and `av_packet_alloc()` with no null check. `open()` then went on to `media.open()`, and the decode path used the pointers: `advanceTo()` passes `pending`/`packet` to `media.receive()`, and `scaled()` reads `shown->width`/`shown->height`, so a null pointer was dereferenced on the first frame request.
- `src/core/VideoAudioExtract.cpp:164-165` (pre-fix, inside `decodeAll`): the same unchecked `av_frame_alloc()` / `av_packet_alloc()` pair, used directly by `media.receive()` and `frame->extended_data`.

Both paths are real. Neither is on the audio thread: the decoder runs on `VideoFramePump`'s worker, and the extraction runs in the `video.extract_audio` task.

## Fix

- `src/core/VideoDecoder.cpp:160-165`: `open()` checks the three `Impl` allocations before touching the file. On a null, it sets `error` to "the video cannot be decoded: out of memory" (only when `error` is non-null, as the rest of `open()` does) and returns `false`. `isOpen()` stays false.
- `src/core/VideoAudioExtract.cpp:162-177`: a small RAII holder `DecodeBuffers` owns the frame and packet and frees them on every return path.
- `src/core/VideoAudioExtract.cpp:196-197`: `extract()` builds the buffers first, before `media.open()`, and returns "the video's audio cannot be decoded: out of memory" if either is null. Nothing is written to `wav` in that case.
- `src/core/VideoAudioExtract.cpp:180`, `:205`: `decodeAll()` now receives the caller's buffers instead of allocating its own.
- Success path: the same buffers do the same decode. The only change is where the audio path allocates (before the file is opened, not after).

## Regression test (four independent cases)

`tests/src/core/VideoTrackTest.cpp` already interposes FFmpeg allocators in its own binary for a fork-based allocation-failure harness (`allocationFailureIsReported`). I extended it with one-shot hooks and four independent QtTest slots:

- `:164-165`: `failNextFrameAllocation`, `failNextPacketAllocation`. Each is consumed by the next call only, so FFmpeg's own calls inside `avformat_*` do not consume it when the product code allocates first.
- `:192`, `:197-200`: the `av_frame_alloc` and new `av_packet_alloc` interposers, forwarding to the real symbol via `dlsym(RTLD_NEXT)` when not armed.
- `:248`, `:256`: shared helpers `decoderOpenReportsOutOfMemory()` and `extractionReportsOutOfMemory()`. Each requires the out-of-memory reason, a closed decoder or no WAV written.
- `:793` `decoderFrameAllocationFailureIsReportedNotDereferenced`, `:807` `decoderPacketAllocationFailureIsReportedNotDereferenced`, `:821` `audioExtractFrameAllocationFailureIsReportedNotDereferenced`, `:835` `audioExtractPacketAllocationFailureIsReportedNotDereferenced`. Each is its own slot, so a failure in one cannot stop the others from running. Each arms exactly one failure in a forked child.

Round 1 had the frame and packet checks inside one slot per product path. The packet check ran only after the frame assertion, so a frame failure stopped the slot before the packet case ran. Round 2 split them as above. The reviewer's version of this split was not reused; the split was written here.

## Round 2: independent reversal proof

Product hunks only were reversed (`git diff 639e1d23e -- src/core/VideoDecoder.cpp src/core/VideoAudioExtract.cpp` saved to `rework-fix.patch`, 77 lines). The test file was not touched by the reversal.

| Case | Failing assertion with the product change reversed |
| --- | --- |
| decoder frame | `VideoDecoder::open did not report a failed av_frame_alloc` |
| decoder packet | `VideoDecoder::open did not report a failed av_packet_alloc` |
| extraction frame | `the audio extraction did not report a failed av_frame_alloc` |
| extraction packet | `the audio extraction did not report a failed av_packet_alloc` |

All four fail on their own; the run totals 24 passed, 4 failed, 1 skipped.

## Commands and exit codes

Round 2 (`logs/` in `runs/new-p10-453.r2`):

| Step | Command (in worktree, or `build/tests` for ctest) | Exit | Result |
| --- | --- | --- | --- |
| GREEN build | `CCACHE_TEMPDIR=/home/kruzzzzy/.cache/zene-tmp cmake --build build --target zene VideoTrackTest -j4 > rework-1-green-build.log 2>&1` | 0 | |
| **GREEN test** | `ctest -R '^VideoTrackTest$' -V > rework-2-green-test.log 2>&1` | **0** | 28 passed, 0 failed, 1 skipped; all four new cases `PASS` |
| Reversal | `git diff 639e1d23e -- src/core/VideoDecoder.cpp src/core/VideoAudioExtract.cpp > rework-fix.patch && git apply -R rework-fix.patch` | 0 / 0 | |
| Reversed build | `cmake --build build --target zene VideoTrackTest -j4 > rework-3-reverted-build.log 2>&1` | 0 | |
| **Reversed test** | `ctest -R '^VideoTrackTest$' -V > rework-4-reverted-test.log 2>&1` | **8** | 24 passed, 4 failed (the four cases above) |
| Restore | `git apply rework-fix.patch`, then build `rework-5-restored-build.log` (zene, VideoTrackTest, VideoFfmpegTest, VideoTestClipTest, ArrangerOverlaysVideoLaneTest) | 0 / 0 | |
| **Restored family** | `ctest -R 'Video' -V > rework-6-restored-family.log 2>&1` | **0** | 4/4 test binaries passed: VideoTrackTest 28/0 (1 skip), VideoFfmpegTest 4/0, VideoTestClipTest 5/0, ArrangerOverlaysVideoLaneTest 10/0 |
| Gate | feature check via the copy script (see note), `rework-7-gate.log` | 0 | 8/8 PASS |

Round 1 (`logs/` in `runs/new-p10-453.r1`), kept as history:

| Step | Exit | Result |
| --- | --- | --- |
| RED build (first attempt, ccache temp dir not writable) | 2 | environment, not code (`build-1-red.log`) |
| RED build (round-1 test, fix absent) | 0 | `build-2-red.log` |
| RED test (round-1 test) | 8 | 24 passed, 2 failed (`test-1-red.log`) |
| GREEN test | 0 | 26 passed (`test-3-green-verbose.log`) |
| Reversal test | 8 | 24 passed, 2 failed (`test-4-revert-red.log`) |
| Re-apply test | 0 | 26 passed (`test-5-reapply-green.log`) |
| Family | 0 | 4/4 (`test-8-final-family.log`) |
| Gate | 0 | 8/8 (`gate-2-final.log`) |
| Completion check, pre-commit | 1 | expected: nothing committed yet (`submit-1-precommit.log`) |

Note on the gate run: `feature_check.py` writes its logs to `<worktree>/../zene-qwen-new-p10-453-feature-check/`, which is outside my write-jail (the stock script exits 1 with `PermissionError`, `logs/gate-3-stock-script.log` in round 1). I ran a copy of the same script from `$TMPDIR` with exactly two lines changed: the log directory points at the run folder's `gate-logs/`, and `ROOT` points at the real `qwen/` directory so the cached base listings (`gate-baselines/639e1d23e….json`) are reused. The checks are the same.

Note on ccache: the environment's ccache temp directory is not writable from this sandbox; `CCACHE_TEMPDIR` was set to `/home/kruzzzzy/.cache/zene-tmp` for the builds that succeeded. Compiler flags did not change.

## Self-check

- **Regression test FAILS with the product change reverted and PASSES with it.** Round 2: each of the four cases fails with the product hunks reversed (`rework-4-reverted-test.log`, exit 8, one `FAIL!` line per case, each at its own assertion), and all four pass when restored (`rework-2-green-test.log` exit 0 and `rework-6-restored-family.log` exit 0).
- **Deterministic: no timing, sleeps or races.** Each case runs in a `fork()`ed child (the existing `allocationFailureIsReported` helper), so a crash cannot take down the parent. The seam is the one-shot interposed `av_frame_alloc` / `av_packet_alloc` (`VideoTrackTest.cpp:192`, `:197-200`), which fails exactly the next call. The fixture is the deterministic clip from `videotest::makeClip`. The one-shot design keeps the failure on the product's own allocation: FFmpeg's internal `avformat_find_stream_info` calls go through the same symbols and would otherwise consume a persistent failure.
- **Fix at the cause and complete.** Callers of `VideoDecoder::open()`: `VideoDecoder::probe()` (`src/core/VideoDecoder.cpp:231-236`), whose caller `VideoReference.cpp:119` treats a failed probe as "no duration" with the reason in `why`; `VideoFramePump.cpp:75`, where a failed open answers null images and retries on the next request (test `thePumpRetriesAPathAfterItsFirstOpenFails` passes); and the test. The only caller of `decodeAll()` is `extract()` (`VideoAudioExtract.cpp:205`). `extractVideoAudio()` is called by the `video.extract_audio` path and by the test wrapper at `VideoAudioExtract.cpp:237`; they receive the error text as before. Sibling allocations: `MediaStream::open()` already checks `avformat_open_input`, `avformat_find_stream_info`, `avcodec_alloc_context3` (ENOMEM) and `avcodec_open2` (`VideoFfmpeg.cpp:48-58`); `scaled()` checks the `sws_getCachedContext` result (`VideoDecoder.cpp:140-141`). The Qt `QImage` allocation in `scaled()` is not changed: it is Qt, not FFmpeg, and outside this finding.
- **No collateral damage.** No limit or size check was added; no other caller is affected. Saved projects and the project format are untouched. On the success path nothing changes in what is decoded or written. In the decoder, the three allocations already happened before `media.open()`; the fix only adds the null check. In the audio path, the buffers are now allocated before `media.open()` instead of after it, and the same buffers run the same decode.
- **Realtime / undo / commands.** Both paths run on worker threads (`VideoFramePump`, the extraction task), not the audio thread. No logging or locking was added. No undo step or control command was added.
- **Verdict basis.** FIXED is supported by `VideoDecoder.cpp:69-71` and `VideoAudioExtract.cpp:164-165` (the unchecked allocations before this change), and by the round-1 and round-2 RED/GREEN runs above.

## Gates and registration

- No new files. The changed files are already listed in `tests/fork-sources.txt` (`VideoAudioExtract.cpp`, `VideoDecoder.cpp`, `tests/src/core/VideoTrackTest.cpp`), and they are not inherited LMMS files, so `tests/upstream-modifications.txt` needs no entry. Neither registry file is changed by this work.
- `feature_check` (copy, see note): 8/8 PASS, exit 0 (`rework-7-gate.log`).
- The stock `feature_check.py` exits 1 in this sandbox before running any gate: `PermissionError` creating `zene-qwen-new-p10-453-feature-check/` beside the worktree. `submit_check.py` calls the stock script, so its `gates` row reads FAIL for that environmental reason only. It is not a gate failure; the same gates pass when the log directory is redirected (above). The write-jail blocks the sibling directory, so I did not work around it further.
