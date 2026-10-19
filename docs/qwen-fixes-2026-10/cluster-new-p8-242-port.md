# Cluster new-p8-242-port: failed video seeks

**Verdict: FIXED**

This is the port of the accepted fix on `bugs/qwen-new-p8-242` (commit `e6012232f`, "Fix failed video seek handling
(cluster new-p8-242)"), which could not be merged into the newest line because its regression test reached for
`AVInputFormat::read_seek`, a private FFmpeg field since FFmpeg 7 (this tree builds against libavformat 63.1.102,
FFmpeg 9). The production change is re-applied by hand against the current code; the test is ported to a
**public-API failed seek**.

## The bug

`MediaStream::seek` ignored the result of `av_seek_frame`, flushed the decoder and continued as though the seek had
worked ([src/core/VideoFfmpeg.cpp:118](../../src/core/VideoFfmpeg.cpp#L118) before the fix). A source that cannot seek,
or a timestamp FFmpeg rejects, left the demuxer at its old position while the decoder was wiped - the next
`frameAt` request then decoded forward from that old position and returned a frame unrelated to the time asked for.

## The fix (re-applied by hand)

- [include/VideoFfmpeg.h:76](../../include/VideoFfmpeg.h#L76) - `MediaStream::seek` returns `bool` now.
- [src/core/VideoFfmpeg.cpp:108](../../src/core/VideoFfmpeg.cpp#L108) - the seek result is checked: on a refused seek the
  function returns `false` **before** `avcodec_flush_buffers`, so the decoder keeps its state and the demuxer keeps its
  position. The guards this tree already had (non-finite time, non-finite or non-positive time base, offset overflow)
  now report the same failure instead of a silent success.
- [src/core/VideoDecoder.cpp:102](../../src/core/VideoDecoder.cpp#L102) - `Impl::seek` returns the stream's answer and
  remembers a refused seek; [src/core/VideoDecoder.cpp:173](../../src/core/VideoDecoder.cpp#L173) - `frameAt` returns an
  empty image when the demuxer refused the seek instead of decoding on from the old position, and the next request
  retries the seek rather than taking the failed one for a done deal. A first request at time zero decodes from the
  stream's start without a seek, so a source that cannot seek still shows its first frames.

## The ported regression test

[tests/src/core/VideoFfmpegTest.cpp](../../tests/src/core/VideoFfmpegTest.cpp) (new, registered in
`tests/CMakeLists.txt`, `tests/all-sources.txt` and `tests/fork-sources.txt`).

The old test built a fake `AVInputFormat` with its own `read_seek`/`read_seek2` callbacks - private fields in FFmpeg 7+,
so the file did not compile. The port produces the refused seek through the public API only:

1. The clip is made at test time by `videotest::makeClip` in the **AVI** container: FFmpeg's AVI demuxer reports a
   refused seek, where the other demuxers (matroska, mov, mpegts) fall back to a forward scan and report success.
2. The source is then made unseekable through the public `AVIOContext`: `media.format->pb->seek = nullptr`. That is the
   documented way to give FFmpeg a source that cannot seek; `avio_seek` then fails and the demuxer's seek is refused.
3. The test asserts the refusal itself (`av_seek_frame(...) < 0`), so a fixture that stops failing loudly is a test
   failure, not a silent pass.

`failedSeekDoesNotRestartTheStream` then calls `media.seek(1.0)` and requires the stream to keep its state
(`media.draining` stays true - the decoder was not flushed and the stream was not restarted).
`successfulSeekRestartsTheStream` is the control on the same clip: a seek that is accepted still restarts the stream,
so the first case fails because of the refused seek and not because of a broken fixture.

The test compiles against the unfixed sources (it does not use the new `bool` return), so it is a real RED/GREEN
regression and not a compile error.

## Evidence

All logs are in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-242-port.r1/logs`. `ctest` was run from
`build/tests`; every build and test command was written to its own numbered log file.

| Evidence | Exact command | EXIT / result |
|---|---|---|
| Register the new test target (`build-2.log`, `build-3.log`) | `cmake --build build --target zene` then `cmake --build build --target VideoFfmpegTest` | 0 / 0 |
| **RED on the original code** (`test-1.log`) | `cd build/tests && ctest -R '^VideoFfmpegTest$' --output-on-failure` | **8**; 1/1 failed: `FAIL! : VideoFfmpegTest::failedSeekDoesNotRestartTheStream() 'media.draining' returned FALSE. (a refused seek must not flush the decoder nor restart the stream)` at `tests/src/core/VideoFfmpegTest.cpp(86)`; the control case passed |
| Fixed build (`build-4.log`) | `cmake --build build --target zene VideoFfmpegTest VideoTrackTest VideoTestClipTest` | 0 |
| **GREEN after the fix** (`test-2.log`) | `cd build/tests && ctest -R '^VideoFfmpegTest$' --output-on-failure` | **0**; 1/1 passed |
| Video family, first run (`test-3.log`) | `cd build/tests && ctest -R 'Video' --output-on-failure` | 8; 3/4 passed, `ArrangerOverlaysVideoLaneTest` was **Not Run** - its binary had never been built in this tree |
| Build the missing family member (`build-5.log`) | `cmake --build build --target ArrangerOverlaysVideoLaneTest` | 0 |
| **Video family** (`test-4.log`) | `cd build/tests && ctest -R 'Video' --output-on-failure` | **0**; 4/4 passed: `VideoTrackTest`, `VideoFfmpegTest`, `VideoTestClipTest`, `ArrangerOverlaysVideoLaneTest` |
| Reverse the production patch (`fix.patch`) | `git diff -- include/VideoFfmpeg.h src/core/VideoFfmpeg.cpp src/core/VideoDecoder.cpp > .../logs/fix.patch && git apply -R .../logs/fix.patch` | 0 |
| Reversed build (`build-6.log`) | `cmake --build build --target zene VideoFfmpegTest VideoTrackTest` | 0 |
| **Reversed RED** (`test-5.log`) | `cd build/tests && ctest -R '^VideoFfmpegTest$' --output-on-failure` | **8**; the same assertion failed again (`'media.draining' returned FALSE`) |
| Restore the production patch | `git apply .../logs/fix.patch` | 0 |
| Restored build (`build-7.log`) | `cmake --build build --target zene VideoFfmpegTest VideoTrackTest` | 0 |
| **Restored GREEN** (`test-6.log`) | `cd build/tests && ctest -R '^VideoFfmpegTest$' --output-on-failure` | **0**; 1/1 passed |
| The clip made in-process, without the ffmpeg program (`test-7.log`) | `cd build/tests && PATH=/tmp/opencode/emptybin ./VideoFfmpegTest` | 0; 4/4 passed (the AVI fixture also round-trips through the in-process libavcodec encoder) |

### Gates

| Gate | Exact command | EXIT / result |
|---|---|---|
| Source registration (`gate-fork-2.log`) | `bash tests/fork-sources-gate.sh` | **0**; 1791 fork-NEW, 1212 all-sources, 0 stale |
| Whole-tree manifest (`gate-allsources-2.log`) | `bash tests/all-sources-reproduce.sh` | **0**; `REPRODUCES` |
| Test registration (`gate-unregistered-1.log`) | `bash tests/unregistered-tests-gate.sh` | **0**; 516 test sources, 514 registered |
| Duplication (`gate-duplication-1.log`) | `bash tests/duplication-gate.sh` | **0**; 0.33% of a 5% budget |
| Upstream divergence (`gate-upstream-1.log`) | `bash tests/no-upstream-regression-gate.sh` | **0**; the three product files are fork-NEW (already in `tests/fork-sources.txt`), `tests/**` and `CMakeLists.txt` are allowed by construction |
| Complexity ratchet (`gate-complexity-2.log`) | `bash tests/complexity-gate.sh --check` | **1** - see below |
| File-length ratchet (`gate-filelength-2.log`) | `bash tests/file-length-gate.sh --check` | **1** - see below |

**The two ratchet gates fail on this branch for reasons that predate this change, and this change adds no new
regression to either.** Proof: with the two scope manifests temporarily reverted to their HEAD content
(`git show HEAD:tests/fork-sources.txt`, `git show HEAD:tests/all-sources.txt`), the same two gates report exactly the
same regression lists - `gate-complexity-pristine.log` (EXIT 1, 6 regressions) and `gate-filelength-pristine.log`
(EXIT 1, 14 regressions) - and `diff` of the two lists against the final run is empty. Every reported file
(`src/core/ProjectRevisions.cpp`, `src/gui/ControlCommandsShortcuts.cpp`, `tests/src/core/DeviceDspTest.cpp`,
`tests/src/core/VideoTrackTest.cpp`, ...) is untouched by this change and is absent from the corresponding baseline, so
the gate calls it "new". None of the reported paths is one of this change's files
(`include/VideoFfmpeg.h`, `src/core/VideoFfmpeg.cpp`, `src/core/VideoDecoder.cpp`,
`tests/src/core/VideoFfmpegTest.cpp`). No gate limit, threshold or baseline was changed, and the gates were not
re-anchored.

## Files committed

- `include/VideoFfmpeg.h`
- `src/core/VideoFfmpeg.cpp`
- `src/core/VideoDecoder.cpp`
- `tests/CMakeLists.txt`
- `tests/all-sources.txt`
- `tests/fork-sources.txt`
- `tests/src/core/VideoFfmpegTest.cpp` (new)
- `docs/qwen-fixes-2026-10/cluster-new-p8-242-port.md` (this report)
