Verdict: FIXED

# new-p10-453: FFmpeg frame and packet allocation failures are unchecked

Logs: round 1 in `runs/new-p10-453.r1/logs/`, round 2 in `runs/new-p10-453.r2/logs/`, round 3 (this rework) in `runs/new-p10-453.r3/logs/`, all under `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/`.

## Evidence (pre-fix, base `639e1d23e`)

- `src/core/VideoDecoder.cpp:69-71`: `VideoDecoder::Impl` stored `av_frame_alloc()` (twice) and `av_packet_alloc()` with no null check. `open()` then went on to `media.open()`, and the decode path used the pointers: `advanceTo()` passes `pending`/`packet` to `media.receive()`, and `scaled()` reads `shown->width`/`shown->height`, so a null pointer was dereferenced on the first frame request.
- `src/core/VideoAudioExtract.cpp:164-165` (pre-fix, inside `decodeAll`, after `media.open()`): the same unchecked `av_frame_alloc()` / `av_packet_alloc()` pair, used directly by `media.receive()` and `frame->extended_data`.

Both paths are real. Neither is on the audio thread: the decoder runs on `VideoFramePump`'s worker, and the extraction runs in the `video.extract_audio` task.

## Fix

- `src/core/VideoDecoder.cpp:160-165`: `open()` checks the three `Impl` allocations before touching the file. On a null, it sets `error` to "the video cannot be decoded: out of memory" (only when `error` is non-null, as the rest of `open()` does) and returns `false`. `isOpen()` stays false.
- `src/core/VideoAudioExtract.cpp:162-177`: a small RAII holder `DecodeBuffers` owns the frame and packet and frees them on every return path.
- `src/core/VideoAudioExtract.cpp:196-197`: `extract()` builds the buffers first, before `media.open()`, and returns "the video's audio cannot be decoded: out of memory" if either is null. Nothing is written to `wav` in that case.
- `src/core/VideoAudioExtract.cpp:180`, `:205`: `decodeAll()` now receives the caller's buffers instead of allocating its own.
- Success path: the same buffers do the same decode. The only change is where the audio path allocates (before the file is opened, not after).

## Regression test (five independent cases, origin-targeted seam)

Round 2 found that the audio frame case was consumed by FFmpeg's own `av_frame_alloc()` during stream probing (`avformat_find_stream_info`, called from `VideoFfmpeg.cpp:48-49`), before the extractor's allocation ran. The hooks were order-based (fail the next call of any caller). Round 3 replaces them with origin-based targeting:

- `tests/src/core/VideoTrackTest.cpp:170-181`: `calledFromThisExecutable()` uses `dladdr` to check that the caller's return address lies in the same object as the test executable. The product sources are statically linked into the test binary, so product calls are counted. Calls made from FFmpeg's shared libraries (`libavformat`, `libavcodec`) are not.
- `:185-190`: `failsTargetedCall()` counts only calls of the armed kind (frame or packet) from the executable, and fails the call whose ordinal matches. In the parent, nothing is armed, so nothing is counted.
- `:192`, `:229`, `:236`: the `av_frame_alloc` and `av_packet_alloc` interposers pass `__builtin_return_address(0)` to that check and forward to the real symbol otherwise. The persistent `failFrameAllocation` flag used by the existing `failedAllocationsAreReportedNotCrashed` test is unchanged.
- `:213-222`: `ownAllocationFailureIsReported(check, kind, ordinal)` forks a child, arms the targeted failure there, and runs the check. Crashes stay in the child.
- `:285`, `:293`: shared helpers `decoderOpenReportsOutOfMemory()` and `extractionReportsOutOfMemory()`: each requires the out-of-memory reason, and a closed decoder or no WAV written.

Five independent slots (each runs even when another fails):

| Slot (`VideoTrackTest.cpp`) | Target | Covers |
| --- | --- | --- |
| `decoderFrameAllocationFailureIsReportedNotDereferenced` (:830) | product frame, call 1 | decoder shown frame |
| `decoderPendingFrameAllocationFailureIsReportedNotDereferenced` (:844) | product frame, call 2 | decoder pending frame, after the shown frame exists |
| `decoderPacketAllocationFailureIsReportedNotDereferenced` (:858) | product packet, call 1 | decoder packet, after both frames exist |
| `audioExtractFrameAllocationFailureIsReportedNotDereferenced` (:872) | product frame, call 1 | extractor frame, before the file is opened |
| `audioExtractPacketAllocationFailureIsReportedNotDereferenced` (:886) | product packet, call 1 | extractor packet, after the frame exists |

The two packet cases run after earlier allocations succeeded, so they exercise the partial-allocation path (the decoder's `Impl` and the extractor's `DecodeBuffers` free what exists). The tests assert the error and the closed or unwritten state; they do not measure the frees themselves (no leak assertion, ASan not run).

## Round 3: reversal proof, origin-targeted

Only the product hunks were reversed (`git diff 639e1d23e -- src/core/VideoDecoder.cpp src/core/VideoAudioExtract.cpp` saved as `rework2-fix.patch`, 77 lines). The test file was not touched by the reversal.

| Case | Failing assertion with the product change reversed |
| --- | --- |
| decoder frame | `VideoDecoder::open did not report a failed shown-frame av_frame_alloc` |
| decoder pending frame | `VideoDecoder::open did not report a failed pending-frame av_frame_alloc` |
| decoder packet | `VideoDecoder::open did not report a failed av_packet_alloc` |
| extraction frame | `the audio extraction did not report a failed av_frame_alloc` |
| extraction packet | `the audio extraction did not report a failed av_packet_alloc` |

The run totals 24 passed, 5 failed, 1 skipped. The `Failed to open codec in avformat_find_stream_info` line from round 2 no longer appears: stream probing is no longer counted.

Why the reversed audio frame case fails: a temporary stderr diagnostic in the helper (run on the reversed build, then removed; the test file was checked byte-identical to a saved copy afterwards) reported the child as `signaled=1 sig=11` (SIGSEGV). With the fix reverted, `decodeAll()` dereferences the null extraction frame. That is the unchecked allocation the finding describes.

## Commands and exit codes

Round 3 (`runs/new-p10-453.r3/logs/`):

| Step | Command (worktree, or `build/tests` for ctest) | Exit | Result |
| --- | --- | --- | --- |
| GREEN build | `CCACHE_TEMPDIR=/home/kruzzzzy/.cache/zene-tmp cmake --build build --target zene VideoTrackTest -j4 > rework2-9-final-green-build.log 2>&1` | 0 | |
| **GREEN test** | `ctest -R '^VideoTrackTest$' -V > rework2-10-final-green-test.log 2>&1` | **0** | 29 passed, 0 failed, 1 skipped; all five new cases `PASS` |
| Reversal | `git diff 639e1d23e -- src/core/VideoDecoder.cpp src/core/VideoAudioExtract.cpp > rework2-fix.patch && git apply -R rework2-fix.patch` | 0 / 0 | |
| Reversed build | `cmake --build build --target zene VideoTrackTest -j4 > rework2-11-final-reverted-build.log 2>&1` | 0 | |
| **Reversed test** | `ctest -R '^VideoTrackTest$' -V > rework2-12-final-reverted-test.log 2>&1` | **8** | 24 passed, 5 failed (the five cases above) |
| Diagnostic (reversed build, removed afterwards) | `./VideoTrackTest audioExtractFrameAllocationFailureIsReportedNotDereferenced > rework2-6-diag-audio-frame.log 2>&1` | 1 | child `signaled=1 sig=11` (SIGSEGV); the case fails as expected |
| Restore | `git apply rework2-fix.patch` ; build `rework2-13-final-restored-build.log` (zene, VideoTrackTest, VideoFfmpegTest, VideoTestClipTest, ArrangerOverlaysVideoLaneTest) | 0 / 0 | |
| **Restored family** | `ctest -R 'Video' -V > rework2-14-final-family.log 2>&1` | **0** | 4/4 test binaries passed: VideoTrackTest 29/0 (1 skip), VideoFfmpegTest 4/0, VideoTestClipTest 5/0, ArrangerOverlaysVideoLaneTest 10/0 |
| Gate | feature check via the copy script (see note), `rework2-15-gate-copy.log` | 0 | 8/8 PASS |
| Stock gate, retried | `python3 -I .../feature_check.py <worktree> > rework2-16-gate-stock.log 2>&1` | 1 | `PermissionError` writing `zene-qwen-new-p10-453-feature-check/fork-sources.log` (see note) |

Round 2 and round 1 (`r2`, `r1` log folders), kept as history: round 2 split the packet check into its own slot and reversed the product hunks (4 cases failing, the audio-frame case probing-related); round 1 had the two-slot version with the reversal at 24 passed / 2 failed. Those logs are referenced in the round-2 and round-1 notes of this report's history and are unchanged.

Note on the gate run: `feature_check.py` writes its logs to `<worktree>/../zene-qwen-new-p10-453-feature-check/`, outside my write-jail. I ran a copy of the same script from `$TMPDIR` with exactly two lines changed: the log directory points at the run folder's `gate-logs/`, and `ROOT` points at the real `qwen/` directory so the cached base listings (`gate-baselines/639e1d23e….json`) are reused. The checks are the same.

Note on the stock gate (updated): the sibling directory now exists (the stock script created it on this run). The stock script still exits 1 in my lane because it cannot write its log files into that directory (`PermissionError` on `fork-sources.log`, `rework2-16-gate-stock.log`). Earlier in round 1 the failure was at `mkdir`. The review reports the stock script completing with exit 0 in its own environment, so this is an environment difference in write permission, not a gate failure. The copy run above shows all gate rows passing.

Note on ccache: the environment's ccache temp directory is not writable from this sandbox; `CCACHE_TEMPDIR` was set to `/home/kruzzzzy/.cache/zene-tmp` for the builds that succeeded. Compiler flags did not change.

## Portability and realtime coverage

- **Linux with FFmpeg (this lane):** all rows above.
- **macOS with FFmpeg:** not run (no macOS host here). The hook code uses `dladdr` and `dli_fbase`, which exist on macOS, but this has not been built or run there.
- **No-FFmpeg build:** not built in this lane (it needs a separate configured tree). By construction, every new identifier (the hook block at `:159-260`, the helpers at `:283-293`, and the slot branches under `#ifdef ZENE_HAVE_VIDEO_ALLOC_FAILURE_HOOKS`, starting at `:800`) sits under the hook guard, and the non-hook branches only `QSKIP`. The `<dlfcn.h>` include (`:70`) is inside the same guard. This is a reading of the code, not a compile.
- **FFmpeg on a platform without the POSIX hooks:** same as above; the guard excludes it and the cases skip. Not built.
- **Realtime:** the allocations run on worker or task threads (`VideoFramePump`'s worker, the `video.extract_audio` task), not the audio callback. The existing frame-pump and background-extraction tests (`thePumpAnswersOnTheInterfaceThread`, `extractAudioLandsTheSoundOnANewTrackInOneStep`, `cancelledDirectExtractionPreservesExistingDestination`) are part of the 29 passing cases.

## Self-check

- **Regression test FAILS with the product change reverted and PASSES with it.** Round 3: each of the five cases fails with the product hunks reversed (`rework2-12-final-reverted-test.log`, exit 8, one `FAIL!` line per case at its own assertion), and all five pass when restored (`rework2-10-final-green-test.log` exit 0, `rework2-14-final-family.log` exit 0). The audio frame case no longer fails during stream probing; the diagnostic shows it fails on the unchecked allocation (SIGSEGV).
- **Deterministic: no timing, sleeps or races.** Each case runs in a `fork()`ed child, so a crash cannot take down the parent. The seam is the origin-targeted interposer (`VideoTrackTest.cpp:170-229`): the caller's return address must lie in the test executable, and the call with the given ordinal fails. FFmpeg's internal calls from shared libraries are never counted. The fixture is the deterministic clip from `videotest::makeClip`. The only state is atomics set before the child runs.
- **Fix at the cause and complete.** Callers of `VideoDecoder::open()`: `VideoDecoder::probe()` (`src/core/VideoDecoder.cpp:231-236`), whose caller `VideoReference.cpp:119` treats a failed probe as "no duration" with the reason in `why`; `VideoFramePump.cpp:75`, where a failed open answers null images and retries on the next request (test `thePumpRetriesAPathAfterItsFirstOpenFails` passes); and the test. The only caller of `decodeAll()` is `extract()` (`VideoAudioExtract.cpp:205`). `extractVideoAudio()` is called by the `video.extract_audio` path and by the test wrapper at `VideoAudioExtract.cpp:237`; they receive the error text as before. Sibling allocations: `MediaStream::open()` already checks `avformat_open_input`, `avformat_find_stream_info`, `avcodec_alloc_context3` (ENOMEM) and `avcodec_open2` (`VideoFfmpeg.cpp:48-58`); `scaled()` checks the `sws_getCachedContext` result (`VideoDecoder.cpp:140-141`). The Qt `QImage` allocation in `scaled()` is not changed: it is Qt, not FFmpeg, and outside this finding.
- **No collateral damage.** No limit or size check was added; no other caller is affected. Saved projects and the project format are untouched. On the success path nothing changes in what is decoded or written. In the decoder, the three allocations already happened before `media.open()`; the fix only adds the null check. In the audio path, the buffers are allocated before `media.open()` instead of after it, and the same buffers run the same decode.
- **Realtime / undo / commands.** Both paths run on worker threads (`VideoFramePump`, the extraction task), not the audio thread. No logging or locking was added. No undo step or control command was added.
- **Verdict basis.** FIXED is supported by `VideoDecoder.cpp:69-71` and `VideoAudioExtract.cpp:164-165` (the unchecked allocations before this change), and by the round-1, round-2 and round-3 RED/GREEN runs above.

## Gates and registration

- No new files. The changed files are already listed in `tests/fork-sources.txt` (`VideoAudioExtract.cpp`, `VideoDecoder.cpp`, `tests/src/core/VideoTrackTest.cpp`), and they are not inherited LMMS files, so `tests/upstream-modifications.txt` needs no entry. Neither registry file is changed by this work.
- `feature_check` (copy, see note): 8/8 PASS, exit 0 (`rework2-15-gate-copy.log`).
- The stock `feature_check.py` exits 1 in my lane (see note above). `submit_check.py` calls the stock script, so its `gates` row reads FAIL for that environmental reason only.
